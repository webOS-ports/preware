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
 =============================================================================*/

// Tests for src/fetch.c: which URLs and file names the service downloads,
// and the curl argument vector it builds for them.  Runs as any user;
// TEST_TMPDIR (default ".") holds its scratch files.

#define _DEFAULT_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "fetch.h"

static int failures, cases;

static void check(const char *what, int cond) {
  cases++;
  if (!cond) failures++;
  printf("[ %s ] %s\n", cond ? " OK " : "FAIL", what);
}

static void write_file(const char *path, const char *content) {
  FILE *f = fopen(path, "w");
  if (!f) { perror(path); abort(); }
  fputs(content, f);
  fclose(f);
}

static const char *tmpdir(void) {
  const char *t = getenv("TEST_TMPDIR");
  return t ? t : ".";
}

// ------------------------------------------------------------------ names

static void test_names(void) {
  check("plain package file name", fetch_safe_name("org.example.app_1.0.0_all.ipk"));
  check("feed name", fetch_safe_name("webosports"));
  check("empty name refused", !fetch_safe_name(""));
  check("NULL name refused", !fetch_safe_name(NULL));
  check("\".\" refused", !fetch_safe_name("."));
  check("\"..\" refused", !fetch_safe_name(".."));
  check("hidden name refused", !fetch_safe_name(".profile"));
  check("option-like name refused", !fetch_safe_name("-rf"));
  check("slash refused", !fetch_safe_name("a/b"));
  check("traversal refused", !fetch_safe_name("../../etc/passwd"));
  check("space refused", !fetch_safe_name("a b.ipk"));
  check("semicolon refused", !fetch_safe_name("a;reboot.ipk"));
  check("$() refused", !fetch_safe_name("$(reboot).ipk"));
  check("backticks refused", !fetch_safe_name("`id`.ipk"));
  check("quote refused", !fetch_safe_name("a'b.ipk"));
  check("newline refused", !fetch_safe_name("a\nb.ipk"));
  char longname[300];
  memset(longname, 'a', sizeof(longname) - 1);
  longname[sizeof(longname) - 1] = '\0';
  check("over-long name refused", !fetch_safe_name(longname));
}

// ------------------------------------------------------------------- URLs

static void test_urls(void) {
  bool https = false;
  char host[256];

  check("https URL parses", fetch_url_parse("https://Feeds.Example.org/webos-ports/all", &https, host, sizeof(host))
	&& https && !strcmp(host, "feeds.example.org"));
  check("http URL with port parses", fetch_url_parse("http://h.example:8080/p", &https, host, sizeof(host))
	&& !https && !strcmp(host, "h.example"));
  check("query string kept legal", fetch_url_parse("https://h.example/get?id=1&v=(2)", NULL, NULL, 0));

  static const char *const bad[] = {
    "https://h.example/a b.ipk",
    "https://h.example/a\".ipk",
    "https://h.example/a'.ipk",
    "https://h.example/$(reboot).ipk",
    "https://h.example/a;reboot",
    "https://h.example/`id`.ipk",
    "https://h.example/a\nb",
    "https://h.example/a\rb",
    "https://h.example/a\tb",
    "https://h.example/a|b",
    "https://h.example/a\\b",
    "https://h.example/a>b",
    "https://h.example/{a,b}.ipk",
    "https://h.example/[1-9].ipk",
    "https://h.example/feed/../../etc/x",
    "https://h.example/feed/./x",
    "https://h.example/feed/..",
    "file:///etc/shadow",
    "ftp://h.example/x",
    "gopher://h.example/x",
    "-https://h.example/",
    "--output=/etc/passwd",
    "https://",
    "https:///path",
    "https://user@h.example/",
    "https://good.example@evil.example/",
    "https://h.example:0/",
    "https://h.example:99999/",
    "https://h.example:/",
    "https://h.example:8a/",
    "https://.h.example/",
    "https://h..example/",
    "https://h.example./",
    "https://-h.example/",
    "https://h_x.example/",
    "https://h.example/\xc3\xa9",
    "",
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    char what[160];
    snprintf(what, sizeof(what), "crafted URL %zu refused", i);
    check(what, !fetch_url_parse(bad[i], NULL, NULL, 0));
  }
  check("NULL URL refused", !fetch_url_parse(NULL, NULL, NULL, 0));
  check("host buffer too small refused", !fetch_url_parse("https://h.example/", NULL, host, 4));
}

// ----------------------------------------------------------- feed config

static void test_http_configured(void) {
  char dir[512], path[600];
  snprintf(dir, sizeof(dir), "%s/fetch-conf-%d", tmpdir(), (int)getpid());
  if (mkdir(dir, 0700)) { perror(dir); abort(); }

  snprintf(path, sizeof(path), "%s/plain.conf", dir);
  write_file(path, "# a comment\nsrc/gz plain http://plain.example/feed\n");
  snprintf(path, sizeof(path), "%s/secure.conf", dir);
  write_file(path, "src secure https://secure.example/feed\n");
  snprintf(path, sizeof(path), "%s/off.conf.disabled", dir);
  write_file(path, "src off http://disabled.example/feed\n");
  snprintf(path, sizeof(path), "%s/slash.conf", dir);
  write_file(path, "src slash http://slash.example/feed/\n");
  snprintf(path, sizeof(path), "%s/dest.conf", dir);
  write_file(path, "dest root http://dest.example/\noption foo http://option.example/\n");
  char target[600];
  snprintf(target, sizeof(target), "%s/elsewhere", dir);
  write_file(target, "src link http://link.example/feed\n");
  snprintf(path, sizeof(path), "%s/link.conf", dir);
  if (symlink(target, path)) { perror(path); abort(); }

  check("http feed index below a configured http feed",
	fetch_http_configured("http://plain.example/feed/Packages.gz", dir));
  check("the configured http feed itself", fetch_http_configured("http://plain.example/feed", dir));
  check("configured URL with a trailing slash",
	fetch_http_configured("http://slash.example/feed/x.ipk", dir));
  check("sibling path of a configured feed refused",
	!fetch_http_configured("http://plain.example/feedX/Packages", dir));
  check("parent path of a configured feed refused",
	!fetch_http_configured("http://plain.example/Packages", dir));
  check("other host refused", !fetch_http_configured("http://other.example/feed/Packages", dir));
  check("disabled feed refused", !fetch_http_configured("http://disabled.example/feed/Packages", dir));
  check("an https feed does not allow http",
	!fetch_http_configured("http://secure.example/feed/Packages", dir));
  check("dest/option lines do not count", !fetch_http_configured("http://dest.example/x", dir) &&
	!fetch_http_configured("http://option.example/x", dir));
  check("a symlinked .conf does not count", !fetch_http_configured("http://link.example/feed/x", dir));
  check("https URL is not 'http configured'", !fetch_http_configured("https://plain.example/feed/x", dir));
  check("missing conf dir", !fetch_http_configured("http://plain.example/feed/x", "/nonexistent-conf-dir"));

  char err[256] = "";
  check("https allowed without configuration",
	fetch_url_allowed("https://any.example/feed/Packages", dir, err, sizeof(err)));
  check("configured http allowed",
	fetch_url_allowed("http://plain.example/feed/Packages", dir, err, sizeof(err)));
  check("unconfigured http refused",
	!fetch_url_allowed("http://evil.example/feed/Packages", dir, err, sizeof(err)) && err[0]);
  check("crafted https refused",
	!fetch_url_allowed("https://any.example/$(reboot)", dir, err, sizeof(err)));
  check("crafted configured-http refused",
	!fetch_url_allowed("http://plain.example/feed/;reboot", dir, err, sizeof(err)));

  unlink(path);
  unlink(target);
  static const char *const names[] = { "plain.conf", "secure.conf", "off.conf.disabled", "slash.conf", "dest.conf" };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
    unlink(path);
  }
  rmdir(dir);
}

// --------------------------------------------------------- developer files

static void test_developer_path(void) {
  char path[512];
  check("file directly in .developer",
	fetch_developer_path("file:///media/internal/.developer/app_1.0_all.ipk", path, sizeof(path)) &&
	!strcmp(path, "/media/internal/.developer/app_1.0_all.ipk"));
  check("traversal out of .developer refused",
	!fetch_developer_path("file:///media/internal/.developer/../../../etc/shadow", path, sizeof(path)));
  check("subdirectory refused",
	!fetch_developer_path("file:///media/internal/.developer/sub/x.ipk", path, sizeof(path)));
  check("other file refused", !fetch_developer_path("file:///etc/passwd", path, sizeof(path)));
  check("look-alike directory refused",
	!fetch_developer_path("file:///media/internal/.developerX/x.ipk", path, sizeof(path)));
  check("the directory itself refused",
	!fetch_developer_path("file:///media/internal/.developer/", path, sizeof(path)));
  check("shell characters refused",
	!fetch_developer_path("file:///media/internal/.developer/$(reboot).ipk", path, sizeof(path)));
  check("small buffer refused",
	!fetch_developer_path("file:///media/internal/.developer/x.ipk", path, 10));
}

// ------------------------------------------------------------ add config

static void test_write_feed_conf(void) {
  char dir[512], path[600], victim[600], got[256] = "", err[128];
  snprintf(dir, sizeof(dir), "%s/fetch-add-%d", tmpdir(), (int)getpid());
  if (mkdir(dir, 0700)) { perror(dir); abort(); }

  check("feed config written",
	fetch_write_feed_conf(dir, "mine.conf", true, "mine", "https://f.example/feed", err, sizeof(err)));
  snprintf(path, sizeof(path), "%s/mine.conf", dir);
  FILE *f = fopen(path, "r");
  if (f) { size_t r = fread(got, 1, sizeof(got) - 1, f); got[r] = '\0'; fclose(f); }
  check("one src/gz line", !strcmp(got, "src/gz mine https://f.example/feed\n"));
  check("written http feed counts as configured",
	fetch_write_feed_conf(dir, "plain.conf", false, "plain", "http://p.example/feed", err, sizeof(err)) &&
	fetch_http_configured("http://p.example/feed/Packages", dir));

  static const char *const bad_urls[] = {
    "https://f.example/x\"; reboot; echo \"",
    "https://f.example/$(reboot)",
    "https://f.example/`reboot`",
    "https://f.example/x\nsrc evil http://evil.example",
    "https://f.example/x evil",
    "file:///etc/shadow",
  };
  for (size_t i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); i++) {
    char what[64];
    snprintf(what, sizeof(what), "crafted config URL %zu refused", i);
    check(what, !fetch_write_feed_conf(dir, "x.conf", false, "x", bad_urls[i], err, sizeof(err)));
  }
  check("config name '..' refused", !fetch_write_feed_conf(dir, "..", false, "x", "https://f.example/", err, sizeof(err)));
  check("config name with slash refused",
	!fetch_write_feed_conf(dir, "../../etc/passwd", false, "x", "https://f.example/", err, sizeof(err)));
  check("feed name with a space refused",
	!fetch_write_feed_conf(dir, "x.conf", false, "a b", "https://f.example/", err, sizeof(err)));
  snprintf(path, sizeof(path), "%s/x.conf", dir);
  check("nothing written for refused input", access(path, F_OK) != 0);

  snprintf(victim, sizeof(victim), "%s/victim", dir);
  write_file(victim, "untouched");
  snprintf(path, sizeof(path), "%s/link.conf", dir);
  if (symlink(victim, path)) abort();
  check("symlinked config refused",
	!fetch_write_feed_conf(dir, "link.conf", false, "x", "https://f.example/", err, sizeof(err)));
  got[0] = '\0';
  f = fopen(victim, "r");
  if (f) { size_t r = fread(got, 1, sizeof(got) - 1, f); got[r] = '\0'; fclose(f); }
  check("symlink target untouched", !strcmp(got, "untouched"));

  unlink(path); unlink(victim);
  snprintf(path, sizeof(path), "%s/mine.conf", dir); unlink(path);
  snprintf(path, sizeof(path), "%s/plain.conf", dir); unlink(path);
  rmdir(dir);
}

// ----------------------------------------------------------- dir listing

static int open_fds(void) {
  int n = 0;
  DIR *d = opendir("/proc/self/fd");
  if (!d) return -1;
  while (readdir(d)) n++;
  closedir(d);
  return n;
}

static bool lists(int fd, const char *name) {
  if (fd < 0) return false;
  DIR *d = fdopendir(fd);
  if (!d) { close(fd); return false; }
  bool found = false;
  const struct dirent *de;
  while ((de = readdir(d)))
    if (!strcmp(de->d_name, name)) found = true;
  closedir(d);
  return found;
}

static void mkd(const char *base, const char *rel) {
  char p[700];
  snprintf(p, sizeof(p), "%s/%s", base, rel);
  if (mkdir(p, 0755)) { perror(p); abort(); }
}

static void test_listing(void) {
  char base[512], r1[600], r2[600], p[1200], err[128];
  snprintf(base, sizeof(base), "%s/fetch-ls-%d", tmpdir(), (int)getpid());
  if (mkdir(base, 0700)) { perror(base); abort(); }
  mkd(base, "internal");
  mkd(base, "internal/.developer");
  mkd(base, "internal/sub");
  mkd(base, "internal/sub/deep");
  mkd(base, "apps");
  mkd(base, "apps/org.example.app");
  mkd(base, "secret");
  mkd(base, "internalX");
  mkd(base, "internal/a b");
  mkd(base, "internal/$(id)");
  snprintf(p, sizeof(p), "%s/secret/key", base); write_file(p, "k");
  snprintf(p, sizeof(p), "%s/internal/sub/deep/pkg.ipk", base); write_file(p, "x");
  snprintf(p, sizeof(p), "%s/internal/file", base); write_file(p, "x");
  char target[600];
  snprintf(target, sizeof(target), "%s/secret", base);
  snprintf(p, sizeof(p), "%s/internal/link", base);
  if (symlink(target, p)) abort();
  snprintf(p, sizeof(p), "%s/internal/sub/up", base);
  if (symlink("../..", p)) abort();
  snprintf(r1, sizeof(r1), "%s/internal", base);
  snprintf(r2, sizeof(r2), "%s/apps", base);
  const char *roots[] = { r1, r2 };
  int before = open_fds();

#define OPEN(rel) (snprintf(p, sizeof(p), "%s%s", base, rel), fetch_open_listing_dir(p, roots, 2, err, sizeof(err)))
  check("root listed", lists(OPEN("/internal"), "sub"));
  check("root with trailing slash listed", lists(OPEN("/internal/"), ".developer"));
  check(".developer listed", lists(OPEN("/internal/.developer"), ".."));
  check("nested directory listed", lists(OPEN("/internal/sub/deep/"), "pkg.ipk"));
  check("second root listed", lists(OPEN("/apps"), "org.example.app"));
  check("app directory listed", lists(OPEN("/apps/org.example.app"), "."));
  check("parent of the roots refused", OPEN("") < 0 && OPEN("/") < 0);
  check("sibling directory refused", OPEN("/secret") < 0);
  check("look-alike root refused", OPEN("/internalX") < 0);
  check("\"..\" out of a root refused", OPEN("/internal/../secret") < 0);
  check("\"..\" inside a root refused", OPEN("/internal/sub/../sub") < 0);
  check("\".\" refused", OPEN("/internal/./sub") < 0);
  check("\"//\" refused", OPEN("/internal//sub") < 0);
  check("symlink out of a root refused", OPEN("/internal/link") < 0 && strstr(err, "symbolic"));
  check("symlink to an ancestor refused", OPEN("/internal/sub/up") < 0);
  check("path through a symlink refused", OPEN("/internal/sub/up/secret") < 0);
  check("file refused", OPEN("/internal/file") < 0);
  check("missing directory refused", OPEN("/internal/none") < 0);
  check("relative path refused", fetch_open_listing_dir("internal", roots, 2, err, sizeof(err)) < 0);
  // these exist, so only the name check refuses them
  check("shell characters refused even if the directory exists", OPEN("/internal/$(id)") < 0);
  check("space refused even if the directory exists", OPEN("/internal/a b") < 0);
  check("newline refused", OPEN("/internal/sub\n") < 0);
  check("NULL refused", fetch_open_listing_dir(NULL, roots, 2, err, sizeof(err)) < 0);
  check("no roots, nothing listed", fetch_open_listing_dir(r1, roots, 0, err, sizeof(err)) < 0);
  check("real root /etc refused", fetch_open_listing_dir("/etc", roots, 2, err, sizeof(err)) < 0);
#undef OPEN
  check("no descriptors leaked", open_fds() == before);

  static const char *const files[] = { "secret/key", "internal/sub/deep/pkg.ipk", "internal/file",
				       "internal/link", "internal/sub/up" };
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    snprintf(p, sizeof(p), "%s/%s", base, files[i]);
    unlink(p);
  }
  static const char *const dirs[] = { "internal/a b", "internal/$(id)", "internal/sub/deep", "internal/sub", "internal/.developer", "internal",
				      "apps/org.example.app", "apps", "secret", "internalX" };
  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
    snprintf(p, sizeof(p), "%s/%s", base, dirs[i]);
    rmdir(p);
  }
  rmdir(base);
}

// --------------------------------------------------------------- headers

static void test_header_values(void) {
  check("device id", fetch_header_value_ok("0123456789abcdef:1"));
  check("CRLF header injection refused", !fetch_header_value_ok("x\r\nX-Evil: 1"));
  check("LF refused", !fetch_header_value_ok("x\nX-Evil: 1"));
  check("empty refused", !fetch_header_value_ok(""));
  check("NULL refused", !fetch_header_value_ok(NULL));
}

// ------------------------------------------------------- credentials

static void test_send_auth(void) {
  check("official feed over https gets credentials",
	fetch_send_auth("https://feeds.webos-ports.org/webos-ports/all/Packages.gz"));
  check("host compared case-insensitively", fetch_send_auth("https://FEEDS.webos-ports.ORG/x"));
  check("port does not matter", fetch_send_auth("https://feeds.webos-ports.org:443/x"));
  check("official feed over http gets none", !fetch_send_auth("http://feeds.webos-ports.org/x"));
  check("third-party https feed gets none", !fetch_send_auth("https://feed.pivotce.com/Packages"));
  check("suffix look-alike gets none", !fetch_send_auth("https://feeds.webos-ports.org.evil.example/x"));
  check("prefix look-alike gets none", !fetch_send_auth("https://evilfeeds.webos-ports.org/x"));
  check("subdomain gets none", !fetch_send_auth("https://a.feeds.webos-ports.org/x"));
  check("parent domain gets none", !fetch_send_auth("https://webos-ports.org/x"));
  check("host in the path gets none", !fetch_send_auth("https://evil.example/feeds.webos-ports.org/x"));
  check("host in the query gets none", !fetch_send_auth("https://evil.example/?h=feeds.webos-ports.org"));
  check("userinfo trick gets none", !fetch_send_auth("https://feeds.webos-ports.org@evil.example/x"));
  check("userinfo trick with port gets none", !fetch_send_auth("https://feeds.webos-ports.org:443@evil.example/x"));
  check("trailing dot gets none", !fetch_send_auth("https://feeds.webos-ports.org./x"));
  check("file URL gets none", !fetch_send_auth("file:///media/internal/.developer/x.ipk"));
  check("NULL gets none", !fetch_send_auth(NULL));
}

// ------------------------------------------------------------ curl argv

static size_t argc_of(const char **argv) {
  size_t n = 0;
  while (argv[n]) n++;
  return n;
}

static bool has_arg(const char **argv, const char *a) {
  for (size_t i = 0; argv[i]; i++)
    if (!strcmp(argv[i], a)) return true;
  return false;
}

static bool has_pair(const char **argv, const char *a, const char *b) {
  for (size_t i = 0; argv[i] && argv[i + 1]; i++)
    if (!strcmp(argv[i], a) && !strcmp(argv[i + 1], b)) return true;
  return false;
}

static void test_curl_argv(void) {
  const char *argv[40];
  fetch_headers hdr;
  const char *url = "https://feeds.example/feed/app_1.0_all.ipk?a=1&b=(2)";

  size_t n = fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, url, NULL, true, false, NULL, NULL);
  check("HEAD argv built", n > 0 && argv[n] == NULL && argc_of(argv) == n);
  check("curl by absolute path", n && !strcmp(argv[0], "/usr/bin/curl"));
  check("URL is one argument, verbatim and last", n && !strcmp(argv[n - 1], url));
  check("https only, also on redirect", has_pair(argv, "--proto", "=https") &&
	has_pair(argv, "--proto-redir", "=https"));
  check("no URL globbing", has_arg(argv, "--globoff"));
  check("HEAD mode", has_arg(argv, "-I") && !has_arg(argv, "--output"));
  check("redirects followed without credentials", has_arg(argv, "--location"));
  check("no headers without credentials", !has_arg(argv, "-H"));

  n = fetch_curl_argv(argv, 40, &hdr, FETCH_DOWNLOAD, url, "/media/internal/.developer/app.ipk", true, false, NULL, NULL);
  check("download argv has the output path", n && has_pair(argv, "--output", "/media/internal/.developer/app.ipk"));
  check("relative output refused",
	!fetch_curl_argv(argv, 40, &hdr, FETCH_DOWNLOAD, url, "app.ipk", true, false, NULL, NULL));
  check("option-like output refused",
	!fetch_curl_argv(argv, 40, &hdr, FETCH_DOWNLOAD, url, "-o/etc/passwd", true, false, NULL, NULL));
  check("missing output refused", !fetch_curl_argv(argv, 40, &hdr, FETCH_DOWNLOAD, url, NULL, true, false, NULL, NULL));

  n = fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, "http://plain.example/feed/Packages", NULL, false, false, NULL, NULL);
  check("http limited to http, redirect may go to https", n && has_pair(argv, "--proto", "=http") &&
	has_pair(argv, "--proto-redir", "=http,https"));
  check("https flag must match the URL",
	!fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, "http://plain.example/x", NULL, true, false, NULL, NULL));
  check("crafted URL refused in argv too",
	!fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, "https://h.example/`id`", NULL, true, false, NULL, NULL));

  n = fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, url, NULL, true, true, "dev:1", "tok");
  check("credentials as two -H arguments", n && has_pair(argv, "-H", "Device-Id: dev:1") &&
	has_pair(argv, "-H", "Auth-Token: tok"));
  check("no redirects with credentials", n && !has_arg(argv, "--location"));
  check("CRLF in the token refused",
	!fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, url, NULL, true, true, "dev", "t\r\nX-Evil: 1"));
  check("missing credentials refused", !fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, url, NULL, true, true, NULL, "t"));
  check("too small argv refused", !fetch_curl_argv(argv, 5, &hdr, FETCH_HEAD, url, NULL, true, false, NULL, NULL));

  char display[1024];
  n = fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, url, NULL, true, true, "dev:1", "secret-token");
  fetch_argv_display(argv, display, sizeof(display));
  check("display leaves the token out", n && !strstr(display, "secret-token") && !strstr(display, "dev:1") &&
	strstr(display, "Auth-Token: ...") && strstr(display, url));
  fetch_argv_display(argv, display, 8);
  check("display truncates safely", strlen(display) < 8);
}

// ---------------------------------------------------------------- gunzip

static void write_gz(const char *path, const char *content) {
  gzFile gz = gzopen(path, "wb");
  if (!gz) abort();
  gzputs(gz, content);
  gzclose(gz);
}

static void test_gunzip(void) {
  char src[512], dst[512], link[512], victim[512], err[256];
  snprintf(src, sizeof(src), "%s/fetch-%d.gz", tmpdir(), (int)getpid());
  snprintf(dst, sizeof(dst), "%s/fetch-%d.out", tmpdir(), (int)getpid());
  snprintf(link, sizeof(link), "%s/fetch-%d.link", tmpdir(), (int)getpid());
  snprintf(victim, sizeof(victim), "%s/fetch-%d.victim", tmpdir(), (int)getpid());

  write_gz(src, "Package: a\nVersion: 1\n\n");
  check("feed index unpacked", fetch_gunzip(src, dst, err, sizeof(err)));
  char got[64] = "";
  FILE *f = fopen(dst, "r");
  if (f) { size_t r = fread(got, 1, sizeof(got) - 1, f); got[r] = '\0'; fclose(f); }
  check("unpacked content", !strcmp(got, "Package: a\nVersion: 1\n\n"));

  // truncated gzip
  struct stat st;
  if (stat(src, &st)) abort();
  if (truncate(src, st.st_size - 6)) abort();
  check("truncated gzip refused", !fetch_gunzip(src, dst, err, sizeof(err)) && access(dst, F_OK) != 0);

  write_file(src, "not gzip at all, but plain text\n");
  // zlib reads a non-gzip file as is (transparent mode); a plain Packages
  // index is fine to take, so only the round trip matters here.
  (void)fetch_gunzip(src, dst, err, sizeof(err));
  unlink(dst);

  write_gz(src, "x");
  write_file(victim, "untouched");
  if (symlink(victim, link)) abort();
  check("symlinked destination refused", !fetch_gunzip(src, link, err, sizeof(err)));
  got[0] = '\0';
  f = fopen(victim, "r");
  if (f) { size_t r = fread(got, 1, sizeof(got) - 1, f); got[r] = '\0'; fclose(f); }
  check("symlink target untouched", !strcmp(got, "untouched"));

  check("missing source refused", !fetch_gunzip("/nonexistent.gz", dst, err, sizeof(err)));

  unlink(src); unlink(dst); unlink(link); unlink(victim);
}

// ------------------------------------------------------------- threads

// The service checks and builds feed downloads in several threads at once.
static const char *thread_conf;
static int thread_bad;

static void *thread_work(void *arg) {
  (void)arg;
  for (int i = 0; i < 200; i++) {
    const char *argv[40];
    fetch_headers hdr;
    char err[128], disp[512];
    if (!fetch_url_allowed("http://plain.example/feed/Packages", thread_conf, err, sizeof(err)) ||
	fetch_url_allowed("https://h.example/$(id)", thread_conf, err, sizeof(err)) ||
	!fetch_curl_argv(argv, 40, &hdr, FETCH_HEAD, "https://h.example/x", NULL, true, true, "d", "t"))
      __atomic_add_fetch(&thread_bad, 1, __ATOMIC_RELAXED);
    else
      fetch_argv_display(argv, disp, sizeof(disp));
  }
  return NULL;
}

static void test_threads(void) {
  char dir[512], path[600];
  snprintf(dir, sizeof(dir), "%s/fetch-thr-%d", tmpdir(), (int)getpid());
  if (mkdir(dir, 0700)) { perror(dir); abort(); }
  snprintf(path, sizeof(path), "%s/plain.conf", dir);
  write_file(path, "src/gz plain http://plain.example/feed\n");
  thread_conf = dir;
  pthread_t t[4];
  for (int i = 0; i < 4; i++)
    if (pthread_create(&t[i], NULL, thread_work, NULL)) abort();
  for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
  check("checks agree across threads", thread_bad == 0);
  unlink(path);
  rmdir(dir);
}

int main(void) {
  test_names();
  test_urls();
  test_http_configured();
  test_developer_path();
  test_header_values();
  test_send_auth();
  test_write_feed_conf();
  test_listing();
  test_curl_argv();
  test_gunzip();
  test_threads();
  printf("== %d cases, %d failed ==\n", cases, failures);
  return failures ? 1 : 0;
}
