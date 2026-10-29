/*=============================================================================
 Copyright (C) 2026 Herman van Hazendonk <github.com@herrie.org>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 =============================================================================*/

#define _DEFAULT_SOURCE

#include "fetch.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define NAME_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-+_"
#define MAX_NAME 200
#define MAX_URL 2048
#define MAX_HOST 253
#define MAX_CONF_LINE 4096

// A decompressed feed index larger than this is refused (a gzip bomb would
// otherwise fill the apps partition).
#define GUNZIP_MAX ((size_t)256 * 1024 * 1024)

static void set_err(char *err, size_t err_len, const char *msg) {
  if (err && err_len) snprintf(err, err_len, "%s", msg);
}

bool fetch_safe_name(const char *name) {
  if (!name) return false;
  size_t len = strlen(name);
  if (len == 0 || len > MAX_NAME) return false;
  if (strspn(name, NAME_CHARS) != len) return false;
  if (name[0] == '.' || name[0] == '-') return false;
  return true;
}

// Characters a URL may carry here: RFC 3986's unreserved and reserved ones
// and '%', less the ones that only ever matter to a shell or that curl gives
// a meaning of its own ({} [] are globbing, though --globoff is passed too).
static bool url_char_ok(unsigned char c) {
  if (c <= 0x20 || c >= 0x7f) return false;
  return strchr("\"'`\\<>^{}|;$[]", c) == NULL;
}

bool fetch_url_parse(const char *url, bool *https, char *host, size_t host_len) {
  if (!url) return false;
  size_t len = strlen(url);
  if (len == 0 || len > MAX_URL) return false;
  for (size_t i = 0; i < len; i++)
    if (!url_char_ok((unsigned char)url[i])) return false;

  const char *p;
  bool secure;
  if (!strncmp(url, "https://", 8)) { secure = true; p = url + 8; }
  else if (!strncmp(url, "http://", 7)) { secure = false; p = url + 7; }
  else return false;

  // The authority runs to the first '/', '?' or '#'.
  size_t alen = strcspn(p, "/?#");
  if (memchr(p, '@', alen)) return false;   // no user name or password

  const char *colon = memchr(p, ':', alen);
  size_t hlen = colon ? (size_t)(colon - p) : alen;
  if (hlen == 0 || hlen > MAX_HOST) return false;
  if (strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") < hlen)
    return false;
  if (p[0] == '.' || p[0] == '-' || p[hlen - 1] == '.' || p[hlen - 1] == '-') return false;
  for (size_t i = 1; i < hlen; i++)
    if (p[i] == '.' && p[i - 1] == '.') return false;

  if (colon) {
    size_t plen = alen - hlen - 1;
    if (plen == 0 || plen > 5) return false;
    unsigned long port = 0;
    for (size_t i = 0; i < plen; i++) {
      if (!isdigit((unsigned char)colon[1 + i])) return false;
      port = port * 10 + (unsigned long)(colon[1 + i] - '0');
    }
    if (port == 0 || port > 65535) return false;
  }

  // No "." or ".." path segments: curl would fold them, and a URL that
  // needs them is not one Preware makes.
  const char *path = p + alen;
  size_t path_len = strcspn(path, "?#");
  for (size_t i = 0; i < path_len; i++) {
    if (path[i] != '/') continue;
    size_t seg = strcspn(path + i + 1, "/?#");
    if ((seg == 1 && path[i + 1] == '.') || (seg == 2 && path[i + 1] == '.' && path[i + 2] == '.'))
      return false;
  }

  if (host) {
    if (hlen >= host_len) return false;
    for (size_t i = 0; i < hlen; i++) host[i] = (char)tolower((unsigned char)p[i]);
    host[hlen] = '\0';
  }
  if (https) *https = secure;
  return true;
}

// Is url the feed URL base, or below it?
static bool url_below(const char *url, const char *base) {
  size_t blen = strlen(base);
  while (blen > 0 && base[blen - 1] == '/') blen--;
  if (blen == 0 || strncmp(url, base, blen)) return false;
  return url[blen] == '\0' || url[blen] == '/';
}

// The URL of an opkg "src" or "src/gz" line, or NULL.
static const char *conf_src_url(char *line) {
  char *save = NULL;
  const char *kw = strtok_r(line, " \t\r\n", &save);
  if (!kw || (strcmp(kw, "src") && strcmp(kw, "src/gz"))) return NULL;
  if (!strtok_r(NULL, " \t\r\n", &save)) return NULL;  // feed name
  return strtok_r(NULL, " \t\r\n", &save);
}

bool fetch_http_configured(const char *url, const char *confdir) {
  bool https;
  if (!fetch_url_parse(url, &https, NULL, 0) || https) return false;

  DIR *dir = opendir(confdir);
  if (!dir) return false;
  int dfd = dirfd(dir);
  if (dfd < 0) {
    closedir(dir);
    return false;
  }
  bool found = false;
  struct dirent *de;
  while (!found && (de = readdir(dir))) {
    size_t n = strlen(de->d_name);
    if (n <= 5 || strcmp(de->d_name + n - 5, ".conf") || de->d_name[0] == '.') continue;

    int fd = openat(dfd, de->d_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) continue;
    struct stat st;
    FILE *f = NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || !(f = fdopen(fd, "r"))) {
      close(fd);
      continue;
    }
    char line[MAX_CONF_LINE];
    while (!found && fgets(line, sizeof(line), f)) {
      const char *src = conf_src_url(line);
      bool src_https;
      if (src && fetch_url_parse(src, &src_https, NULL, 0) && !src_https && url_below(url, src))
	found = true;
    }
    fclose(f);
  }
  closedir(dir);
  return found;
}

bool fetch_url_allowed(const char *url, const char *confdir, char *err, size_t err_len) {
  bool https;
  if (!fetch_url_parse(url, &https, NULL, 0)) {
    set_err(err, err_len, "Refusing an invalid or unsupported URL (https:// is needed)");
    return false;
  }
  if (!https && !fetch_http_configured(url, confdir)) {
    set_err(err, err_len, "Refusing an http:// URL that is not below a feed configured for http");
    return false;
  }
  return true;
}

bool fetch_developer_path(const char *url, char *path, size_t path_len) {
  static const char prefix[] = "file://" FETCH_DEVELOPER_DIR;
  if (!url || strncmp(url, prefix, sizeof(prefix) - 1)) return false;
  const char *name = url + sizeof(prefix) - 1;
  if (!fetch_safe_name(name)) return false;
  int n = snprintf(path, path_len, "%s%s", FETCH_DEVELOPER_DIR, name);
  return n > 0 && (size_t)n < path_len;
}

bool fetch_header_value_ok(const char *value) {
  if (!value) return false;
  size_t len = strlen(value);
  if (len == 0 || len > 255) return false;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    if (c < 0x20 || c >= 0x7f) return false;
  }
  return true;
}

size_t fetch_curl_argv(const char **argv, size_t max, fetch_headers *hdr, fetch_mode mode,
		       const char *url, const char *output, bool https,
		       bool send_auth, const char *device, const char *token) {
  const char *a[32];
  size_t n = 0;

  bool url_https;
  if (!fetch_url_parse(url, &url_https, NULL, 0) || url_https != https) return 0;

  a[n++] = "/usr/bin/curl";
  a[n++] = "--globoff";
  a[n++] = "--proto";
  a[n++] = https ? "=https" : "=http";
  a[n++] = "--proto-redir";
  a[n++] = https ? "=https" : "=http,https";
  a[n++] = "--user-agent";
  a[n++] = "Preware";
  if (send_auth) {
    if (!hdr || !fetch_header_value_ok(device) || !fetch_header_value_ok(token)) return 0;
    snprintf(hdr->device, sizeof(hdr->device), "Device-Id: %s", device);
    snprintf(hdr->token, sizeof(hdr->token), "Auth-Token: %s", token);
    a[n++] = "-H";
    a[n++] = hdr->device;
    a[n++] = "-H";
    a[n++] = hdr->token;
  }
  if (mode == FETCH_HEAD) {
    a[n++] = "-s";
    a[n++] = "-I";
  }
  else {
    if (!output || output[0] != '/') return 0;
    a[n++] = "--create-dirs";
    a[n++] = "--output";
    a[n++] = output;
  }
  // curl sends -H headers to every host a redirect leads to.
  if (!send_auth) a[n++] = "--location";
  a[n++] = "--fail";
  a[n++] = "--show-error";
  a[n++] = url;

  if (n + 1 > max) return 0;
  for (size_t i = 0; i < n; i++) argv[i] = a[i];
  argv[n] = NULL;
  return n;
}

void fetch_argv_display(const char *const *argv, char *buf, size_t len) {
  if (!len) return;
  buf[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; argv[i]; i++) {
    const char *s = argv[i];
    if (i > 0 && !strcmp(argv[i - 1], "-H")) {
      // keep the header name, leave out its value
      const char *colon = strchr(s, ':');
      int n = snprintf(buf + used, len - used, " %.*s: ...",
		       colon ? (int)(colon - s) : 0, s);
      if (n < 0 || (size_t)n >= len - used) return;
      used += (size_t)n;
      continue;
    }
    int n = snprintf(buf + used, len - used, "%s%s", i ? " " : "", s);
    if (n < 0 || (size_t)n >= len - used) return;
    used += (size_t)n;
  }
}

bool fetch_gunzip(const char *src, const char *dst, char *err, size_t err_len) {
  int in = open(src, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (in < 0) {
    set_err(err, err_len, "Cannot open the downloaded feed index");
    return false;
  }
  gzFile gz = gzdopen(in, "rb");
  if (!gz) {
    close(in);
    set_err(err, err_len, "Cannot read the downloaded feed index");
    return false;
  }
  int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
  if (out < 0) {
    gzclose(gz);
    set_err(err, err_len, "Cannot write the feed index");
    return false;
  }

  bool ok = true;
  size_t total = 0;
  char chunk[65536];
  for (;;) {
    int n = gzread(gz, chunk, sizeof(chunk));
    // A truncated stream shows only in gzerror(), and only right after the
    // read that returned its last bytes: the next read clears it.
    int zerr = Z_OK;
    (void)gzerror(gz, &zerr);
    if (n < 0 || (zerr != Z_OK && zerr != Z_STREAM_END)) {
      set_err(err, err_len, "The downloaded feed index is not valid gzip");
      ok = false;
      break;
    }
    if (n == 0) break;
    total += (size_t)n;
    if (total > GUNZIP_MAX) {
      set_err(err, err_len, "The feed index is too large");
      ok = false;
      break;
    }
    for (size_t off = 0; off < (size_t)n;) {
      ssize_t w = write(out, chunk + off, (size_t)n - off);
      if (w < 0) {
	if (errno == EINTR) continue;
	set_err(err, err_len, "Cannot write the feed index");
	ok = false;
	break;
      }
      off += (size_t)w;
    }
    if (!ok) break;
  }
  gzclose(gz);
  if (close(out) != 0 && ok) {
    set_err(err, err_len, "Cannot write the feed index");
    ok = false;
  }
  if (!ok) unlink(dst);
  return ok;
}
