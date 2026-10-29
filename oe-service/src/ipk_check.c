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

#define _DEFAULT_SOURCE  // mkstemps

#include "ipk_check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zlib.h>

#define TAR_BLOCK 512
// Longest name, link name or pax header we are prepared to look at, and the
// largest control file.  Real packages stay far below these.
#define META_MAX 65536
#define IO_CHUNK 65536

static void set_err(char *err, size_t err_len, const char *fmt, const char *arg) {
  if (err && err_len) snprintf(err, err_len, fmt, arg ? arg : "");
}

// ----------------------------------------------------------------------------
// Names
// ----------------------------------------------------------------------------

bool ipk_valid_package_name(const char *name) {
  static const char allowed[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.+_-";
  if (!name) return false;
  size_t len = strlen(name);
  if (len == 0 || len > 127) return false;
  if (strspn(name, allowed) != len) return false;
  if (name[0] == '.' || name[0] == '-' || name[0] == '+') return false;
  if (strstr(name, "..")) return false;
  return true;
}

//
// Split an archive name into its components.  A leading "./" (any number of
// them), a lone "." and trailing slashes are dropped; an absolute name, an
// empty, "." or ".." component inside the name is refused.  The first four
// components are copied into first[], the count goes to *count.
//
#define FIRST_COMPONENTS 4
#define COMPONENT_MAX 256

static bool split_name(const char *name, char first[FIRST_COMPONENTS][COMPONENT_MAX], size_t *count) {
  *count = 0;
  if (!name) return false;
  if (name[0] == '/') return false;

  const char *p = name;
  while (p[0] == '.' && p[1] == '/') {
    p += 2;
    while (*p == '/') p++;
  }
  if (!strcmp(p, ".")) p += 1;

  size_t len = strlen(p);
  while (len && p[len - 1] == '/') len--;

  size_t i = 0;
  while (i < len) {
    size_t j = i;
    while (j < len && p[j] != '/') j++;
    size_t clen = j - i;
    if (clen == 0) return false;                                  // "a//b"
    if (clen == 1 && p[i] == '.') return false;                    // "a/./b"
    if (clen == 2 && p[i] == '.' && p[i + 1] == '.') return false; // ".."
    if (*count < FIRST_COMPONENTS) {
      if (clen >= COMPONENT_MAX) return false;
      memcpy(first[*count], p + i, clen);
      first[*count][clen] = '\0';
    }
    (*count)++;
    i = j + 1;
  }
  return true;
}

// Absolute link targets are only allowed into the shared, read-only web
// frameworks, which apps commonly link to (cordova.js, enyo).
#define FRAMEWORKS_DIR "/usr/palm/frameworks/"

// A symbolic link target that cannot lead out of the package's directory: a
// relative one without any ".." component, or one into FRAMEWORKS_DIR
// without ".", ".." or empty components.
static bool link_target_contained(const char *target) {
  if (!target || !*target) return false;
  bool absolute = target[0] == '/';
  const char *p = target;
  if (absolute) {
    if (strncmp(target, FRAMEWORKS_DIR, strlen(FRAMEWORKS_DIR))) return false;
    p = target + strlen(FRAMEWORKS_DIR);
    if (!*p) return false;
  }
  while (*p) {
    const char *q = strchr(p, '/');
    size_t clen = q ? (size_t)(q - p) : strlen(p);
    if (clen == 2 && p[0] == '.' && p[1] == '.') return false;
    if (absolute && (clen == 0 || (clen == 1 && p[0] == '.'))) return false;
    if (!q) break;
    p = q + 1;
  }
  return true;
}

// name without leading "./", a lone "." or trailing slashes, as a new string;
// NULL if split_name() refuses it.
static char *canonical_name(const char *name) {
  char c[FIRST_COMPONENTS][COMPONENT_MAX];
  size_t n;
  if (!split_name(name, c, &n)) return NULL;
  const char *p = name;
  while (p[0] == '.' && p[1] == '/') {
    p += 2;
    while (*p == '/') p++;
  }
  if (!strcmp(p, ".")) p += 1;
  size_t len = strlen(p);
  while (len && p[len - 1] == '/') len--;
  char *s = malloc(len + 1);
  if (!s) return NULL;
  memcpy(s, p, len);
  s[len] = '\0';
  return s;
}

static bool is_regular(char type) {
  return type == '0' || type == '\0' || type == '7';
}

static ipk_result data_entry_check(const char *package, const char *name, char type,
				   const char *linkname, unsigned long mode,
				   char *err, size_t err_len) {
  char c[FIRST_COMPONENTS][COMPONENT_MAX];
  size_t n = 0;

  if (!ipk_valid_package_name(package)) {
    set_err(err, err_len, "invalid package name %s", package);
    return IPK_ERR_PACKAGE;
  }
  if (!split_name(name, c, &n)) {
    set_err(err, err_len, "refusing package file with an invalid name: %s", name);
    return IPK_ERR_PATH;
  }

  if (!is_regular(type) && type != '5' && type != '2') {
    set_err(err, err_len, "refusing package entry that is not a file, directory or symbolic link: %s", name);
    return IPK_ERR_TYPE;
  }
  if (mode & 06000) {
    set_err(err, err_len, "refusing setuid/setgid package file: %s", name);
    return IPK_ERR_TYPE;
  }

  // ".", "usr", "usr/palm" and "usr/palm/{applications,packages,services}"
  // may only be directories; everything else must sit below one of those, in
  // the package's own directory.
  if (n >= 1 && strcmp(c[0], "usr")) goto outside;
  if (n >= 2 && strcmp(c[1], "palm")) goto outside;
  if (n >= 3 && strcmp(c[2], "applications") && strcmp(c[2], "packages") && strcmp(c[2], "services"))
    goto outside;
  if (n <= 3) {
    if (type != '5') goto outside;
    return IPK_OK;
  }

  size_t plen = strlen(package);
  if (!strcmp(c[2], "services")) {
    if (strncmp(c[3], package, plen) || (c[3][plen] != '\0' && c[3][plen] != '.') ||
	(c[3][plen] == '.' && c[3][plen + 1] == '\0'))
      goto outside;
  }
  else if (strcmp(c[3], package)) goto outside;

  if (type == '2') {
    // A link at the package's own directory, or one that can climb out of
    // it, would let the next entry write into another package's directory.
    if (n < 5 || !link_target_contained(linkname)) {
      set_err(err, err_len, "refusing symbolic link that leaves the package directory: %s", name);
      return IPK_ERR_TYPE;
    }
  }
  return IPK_OK;

 outside:
  set_err(err, err_len, "refusing package file outside the application's directories: %s", name);
  return IPK_ERR_PATH;
}

bool ipk_data_entry_allowed(const char *package, const char *name, char type,
			    const char *linkname, unsigned long mode,
			    char *err, size_t err_len) {
  return data_entry_check(package, name, type, linkname, mode, err, err_len) == IPK_OK;
}

bool ipk_control_entry_allowed(const char *name, char type, char *err, size_t err_len) {
  char c[FIRST_COMPONENTS][COMPONENT_MAX];
  size_t n = 0;

  if (!split_name(name, c, &n)) {
    set_err(err, err_len, "refusing control file with an invalid name: %s", name);
    return false;
  }
  if (n == 0 && type == '5') return true;
  if (n == 1 && is_regular(type) && (!strcmp(c[0], "control") || !strcmp(c[0], "md5sums")))
    return true;

  if (n == 1 && (!strcmp(c[0], "preinst") || !strcmp(c[0], "postinst") ||
		 !strcmp(c[0], "prerm") || !strcmp(c[0], "postrm")))
    set_err(err, err_len, "refusing package with a maintainer script: %s", c[0]);
  else
    set_err(err, err_len, "refusing package with an unexpected control file: %s", name);
  return false;
}

// ----------------------------------------------------------------------------
// tar
// ----------------------------------------------------------------------------

typedef enum { COLLECT_NONE, COLLECT_LONGNAME, COLLECT_LONGLINK, COLLECT_PAX,
	       COLLECT_PAX_GLOBAL, COLLECT_CONTROL } collect_kind;

typedef struct {
  bool control;              // checking control.tar rather than data.tar
  const char *package;       // data.tar: the package name from control
  unsigned char hdr[TAR_BLOCK];
  size_t hdr_fill;
  bool in_data;
  uint64_t data_left;        // bytes of the entry's data still to come
  uint64_t pad_left;         // padding after it
  collect_kind collect;
  char *buf;                 // collected metadata or control file
  size_t buf_len;
  char *long_name, *long_link, *pax_path, *pax_link;
  bool ended;                // saw the end-of-archive block
  ipk_result result;
  char *err;
  size_t err_len;
  char *control_file;        // control: contents of ./control
  size_t control_len;
  char **links;              // data: the symbolic links seen so far
  size_t links_len;
} tar_state;

static void tar_fail(tar_state *t, ipk_result r, const char *fmt, const char *arg) {
  if (t->result != IPK_OK) return;
  t->result = r;
  set_err(t->err, t->err_len, fmt, arg);
}

static void tar_free(tar_state *t) {
  free(t->buf);
  free(t->long_name);
  free(t->long_link);
  free(t->pax_path);
  free(t->pax_link);
  free(t->control_file);
  t->buf = t->long_name = t->long_link = t->pax_path = t->pax_link = t->control_file = NULL;
  for (size_t i = 0; i < t->links_len; i++) free(t->links[i]);
  free(t->links);
  t->links = NULL;
  t->links_len = 0;
}

// Refuse an entry below a symbolic link the archive made earlier: unpacking
// it would write wherever that link points.  Remember the entry if it is a
// link itself.
static void check_through_links(tar_state *t, const char *name, char type) {
  char *canon = canonical_name(name);
  if (!canon) return;  // already refused by data_entry_check()
  size_t clen = strlen(canon);
  for (size_t i = 0; i < t->links_len && t->result == IPK_OK; i++) {
    size_t llen = strlen(t->links[i]);
    if (clen > llen && canon[llen] == '/' && !strncmp(canon, t->links[i], llen))
      tar_fail(t, IPK_ERR_TYPE, "refusing package entry below a symbolic link: %s", name);
  }
  if (t->result == IPK_OK && type == '2') {
    char **grown = realloc(t->links, (t->links_len + 1) * sizeof(*grown));
    if (!grown) {
      tar_fail(t, IPK_ERR_IO, "out of memory%s", "");
    }
    else {
      t->links = grown;
      t->links[t->links_len++] = canon;
      canon = NULL;
    }
  }
  free(canon);
}

static bool parse_number(const unsigned char *f, size_t len, uint64_t *out) {
  uint64_t v = 0;
  if (f[0] & 0x80) {
    // GNU base-256: the remaining bits, big endian.  Negative values and
    // anything above 2^62 are refused.
    if (f[0] & 0x40) return false;
    v = f[0] & 0x3f;
    for (size_t i = 1; i < len; i++) {
      if (v >> 54) return false;
      v = (v << 8) | f[i];
    }
    *out = v;
    return true;
  }
  size_t i = 0;
  while (i < len && f[i] == ' ') i++;
  for (; i < len && f[i] >= '0' && f[i] <= '7'; i++) {
    if (v >> 60) return false;
    v = (v << 3) | (uint64_t)(f[i] - '0');
  }
  for (; i < len; i++)
    if (f[i] != ' ' && f[i] != '\0') return false;
  *out = v; // an empty field is 0
  return true;
}

static bool checksum_ok(const unsigned char *h) {
  uint64_t stored;
  if (!parse_number(h + 148, 8, &stored)) return false;
  unsigned long usum = 0;
  long ssum = 0;
  for (size_t i = 0; i < TAR_BLOCK; i++) {
    unsigned char b = (i >= 148 && i < 156) ? ' ' : h[i];
    usum += b;
    ssum += (signed char)b;
  }
  return stored == usum || (long)stored == ssum;
}

static char *field_dup(const unsigned char *f, size_t len) {
  size_t n = strnlen((const char *)f, len);
  char *s = malloc(n + 1);
  if (!s) return NULL;
  memcpy(s, f, n);
  s[n] = '\0';
  return s;
}

// Pull "path" and "linkpath" out of pax records ("<len> <key>=<value>\n").
static bool parse_pax(const char *buf, size_t len, char **path, char **link) {
  size_t pos = 0;
  while (pos < len) {
    size_t i = pos, rlen = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
      if (rlen > META_MAX) return false;
      rlen = rlen * 10 + (size_t)(buf[i] - '0');
      i++;
    }
    if (i == pos || i >= len || buf[i] != ' ' || rlen <= i - pos + 1 || pos + rlen > len) return false;
    if (buf[pos + rlen - 1] != '\n') return false;
    const char *kv = buf + i + 1;
    size_t kvlen = pos + rlen - 1 - (i + 1);
    const char *eq = memchr(kv, '=', kvlen);
    if (!eq) return false;
    size_t klen = (size_t)(eq - kv);
    size_t vlen = kvlen - klen - 1;
    char **target = NULL;
    if (klen == 4 && !memcmp(kv, "path", 4)) target = path;
    else if (klen == 8 && !memcmp(kv, "linkpath", 8)) target = link;
    if (target) {
      if (memchr(eq + 1, '\0', vlen)) return false;
      free(*target);
      *target = malloc(vlen + 1);
      if (!*target) return false;
      memcpy(*target, eq + 1, vlen);
      (*target)[vlen] = '\0';
    }
    pos += rlen;
  }
  return true;
}

static void check_entry(tar_state *t, char type, const char *raw_name, const char *raw_link,
			unsigned long mode) {
  const char *names[3] = { raw_name, t->long_name, t->pax_path };
  const char *links[3] = { raw_link, t->long_link, t->pax_link };

  for (int i = 0; i < 3 && t->result == IPK_OK; i++) {
    if (!names[i]) continue;
    if (t->control) {
      if (!ipk_control_entry_allowed(names[i], type, t->err, t->err_len))
	t->result = IPK_ERR_SCRIPT;
      continue;
    }
    for (int j = 0; j < 3 && t->result == IPK_OK; j++) {
      if (j > 0 && !links[j]) continue;
      t->result = data_entry_check(t->package, names[i], type, links[j], mode, t->err, t->err_len);
    }
    if (t->result == IPK_OK) check_through_links(t, names[i], type);
  }
}

static void process_header(tar_state *t) {
  const unsigned char *h = t->hdr;
  bool zero = true;
  for (size_t i = 0; i < TAR_BLOCK; i++)
    if (h[i]) { zero = false; break; }
  if (zero) {
    t->ended = true;
    return;
  }
  if (!checksum_ok(h)) {
    tar_fail(t, IPK_ERR_FORMAT, "corrupt tar header in package%s", "");
    return;
  }

  uint64_t size, mode;
  if (!parse_number(h + 124, 12, &size) || !parse_number(h + 100, 8, &mode)) {
    tar_fail(t, IPK_ERR_FORMAT, "corrupt tar header in package%s", "");
    return;
  }
  char type = (char)h[156];

  t->in_data = true;
  t->data_left = size;
  t->pad_left = (TAR_BLOCK - size % TAR_BLOCK) % TAR_BLOCK;
  t->collect = COLLECT_NONE;

  switch (type) {
  case 'L': t->collect = COLLECT_LONGNAME; break;
  case 'K': t->collect = COLLECT_LONGLINK; break;
  case 'x': t->collect = COLLECT_PAX; break;
  case 'g': t->collect = COLLECT_PAX_GLOBAL; break;
  default: break;
  }
  if (t->collect != COLLECT_NONE) {
    if (size > META_MAX) {
      tar_fail(t, IPK_ERR_FORMAT, "oversized tar metadata in package%s", "");
      return;
    }
    free(t->buf);
    t->buf = malloc((size_t)size + 1);
    t->buf_len = 0;
    if (!t->buf) tar_fail(t, IPK_ERR_IO, "out of memory%s", "");
    return;
  }

  // A real entry.  The POSIX ustar prefix field only means a prefix there;
  // GNU tar keeps other data in it.
  char *raw_name;
  if (!memcmp(h + 257, "ustar\0", 6) && h[345]) {
    char *prefix = field_dup(h + 345, 155);
    char *name = field_dup(h, 100);
    raw_name = (prefix && name) ? malloc(strlen(prefix) + strlen(name) + 2) : NULL;
    if (raw_name) sprintf(raw_name, "%s/%s", prefix, name);
    free(prefix);
    free(name);
  }
  else {
    raw_name = field_dup(h, 100);
  }
  char *raw_link = field_dup(h + 157, 100);
  if (!raw_name || !raw_link) {
    tar_fail(t, IPK_ERR_IO, "out of memory%s", "");
  }
  else {
    check_entry(t, type, raw_name, raw_link, (unsigned long)mode);
  }

  // Keep ./control from control.tar, to read the package name from it.
  if (t->result == IPK_OK && t->control && is_regular(type)) {
    char c[FIRST_COMPONENTS][COMPONENT_MAX];
    size_t n;
    const char *nm = t->pax_path ? t->pax_path : t->long_name ? t->long_name : raw_name;
    if (split_name(nm, c, &n) && n == 1 && !strcmp(c[0], "control")) {
      if (t->control_file) {
	tar_fail(t, IPK_ERR_FORMAT, "package has two control files%s", "");
      }
      else if (size > META_MAX) {
	tar_fail(t, IPK_ERR_FORMAT, "oversized control file in package%s", "");
      }
      else {
	free(t->buf);
	t->buf = malloc((size_t)size + 1);
	t->buf_len = 0;
	t->collect = COLLECT_CONTROL;
	if (!t->buf) tar_fail(t, IPK_ERR_IO, "out of memory%s", "");
      }
    }
  }

  free(raw_name);
  free(raw_link);
  free(t->long_name);
  free(t->long_link);
  free(t->pax_path);
  free(t->pax_link);
  t->long_name = t->long_link = t->pax_path = t->pax_link = NULL;
}

static void finish_collect(tar_state *t) {
  if (t->collect == COLLECT_NONE || !t->buf) return;
  t->buf[t->buf_len] = '\0';
  switch (t->collect) {
  case COLLECT_LONGNAME:
    free(t->long_name);
    t->long_name = strdup(t->buf);
    if (!t->long_name) tar_fail(t, IPK_ERR_IO, "out of memory%s", "");
    break;
  case COLLECT_LONGLINK:
    free(t->long_link);
    t->long_link = strdup(t->buf);
    if (!t->long_link) tar_fail(t, IPK_ERR_IO, "out of memory%s", "");
    break;
  case COLLECT_PAX:
    if (!parse_pax(t->buf, t->buf_len, &t->pax_path, &t->pax_link))
      tar_fail(t, IPK_ERR_FORMAT, "corrupt pax header in package%s", "");
    break;
  case COLLECT_PAX_GLOBAL: {
    // A global header that renames entries is not something we try to follow.
    char *path = NULL, *link = NULL;
    if (!parse_pax(t->buf, t->buf_len, &path, &link) || path || link)
      tar_fail(t, IPK_ERR_FORMAT, "unsupported global pax header in package%s", "");
    free(path);
    free(link);
    break;
  }
  case COLLECT_CONTROL:
    t->control_file = t->buf;
    t->control_len = t->buf_len;
    t->buf = NULL;
    break;
  default:
    break;
  }
  free(t->buf);
  t->buf = NULL;
  t->buf_len = 0;
  t->collect = COLLECT_NONE;
}

static void tar_feed(tar_state *t, const unsigned char *p, size_t len) {
  while (len && t->result == IPK_OK) {
    if (t->ended) {
      // Whatever follows the end of the archive must be zero padding: another
      // unpacker might carry on reading after it.
      for (size_t i = 0; i < len; i++)
	if (p[i]) {
	  tar_fail(t, IPK_ERR_FORMAT, "data after the end of a tar archive in package%s", "");
	  return;
	}
      return;
    }
    if (!t->in_data) {
      size_t take = TAR_BLOCK - t->hdr_fill;
      if (take > len) take = len;
      memcpy(t->hdr + t->hdr_fill, p, take);
      t->hdr_fill += take;
      p += take;
      len -= take;
      if (t->hdr_fill == TAR_BLOCK) {
	t->hdr_fill = 0;
	process_header(t);
	if (t->in_data && t->data_left == 0 && t->pad_left == 0) {
	  t->in_data = false;
	  finish_collect(t);
	}
      }
      continue;
    }
    if (t->data_left) {
      size_t take = t->data_left < len ? (size_t)t->data_left : len;
      if (t->collect != COLLECT_NONE && t->buf) {
	memcpy(t->buf + t->buf_len, p, take);
	t->buf_len += take;
      }
      t->data_left -= take;
      p += take;
      len -= take;
    }
    if (!t->data_left && t->pad_left && len) {
      size_t take = t->pad_left < len ? (size_t)t->pad_left : len;
      t->pad_left -= take;
      p += take;
      len -= take;
    }
    if (!t->data_left && !t->pad_left) {
      t->in_data = false;
      finish_collect(t);
    }
  }
}

static void tar_finish(tar_state *t) {
  if (t->result != IPK_OK) return;
  if (t->in_data || t->hdr_fill)
    tar_fail(t, IPK_ERR_FORMAT, "truncated tar archive in package%s", "");
}

// ----------------------------------------------------------------------------
// ar members
// ----------------------------------------------------------------------------

// Feed size bytes of f, gzip-compressed or not, to the tar checker.
static void check_member(FILE *f, uint64_t size, bool gzip, tar_state *t) {
  unsigned char *in = malloc(IO_CHUNK), *out = malloc(IO_CHUNK);
  z_stream z;
  bool zinit = false, zdone = false;

  if (!in || !out) {
    tar_fail(t, IPK_ERR_IO, "out of memory%s", "");
    goto done;
  }
  memset(&z, 0, sizeof(z));
  if (gzip) {
    if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) {
      tar_fail(t, IPK_ERR_IO, "cannot initialise zlib%s", "");
      goto done;
    }
    zinit = true;
  }

  while (size && t->result == IPK_OK) {
    size_t want = size < IO_CHUNK ? (size_t)size : IO_CHUNK;
    size_t got = fread(in, 1, want, f);
    if (got != want) {
      tar_fail(t, IPK_ERR_IO, "truncated package%s", "");
      break;
    }
    size -= got;

    if (!gzip) {
      tar_feed(t, in, got);
      continue;
    }

    z.next_in = in;
    z.avail_in = (uInt)got;
    while (z.avail_in && t->result == IPK_OK) {
      if (zdone) {
	// Only zero padding may follow; anything else would be another gzip
	// member, which we read on rather than ignore.
	size_t i = 0;
	while (i < z.avail_in && !z.next_in[i]) i++;
	if (i == z.avail_in) { z.avail_in = 0; break; }
	z.next_in += i;
	z.avail_in -= (uInt)i;
	if (inflateReset(&z) != Z_OK) {
	  tar_fail(t, IPK_ERR_FORMAT, "corrupt gzip data in package%s", "");
	  break;
	}
	zdone = false;
      }
      z.next_out = out;
      z.avail_out = IO_CHUNK;
      int rc = inflate(&z, Z_NO_FLUSH);
      if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
	tar_fail(t, IPK_ERR_FORMAT, "corrupt gzip data in package%s", "");
	break;
      }
      tar_feed(t, out, IO_CHUNK - z.avail_out);
      if (rc == Z_STREAM_END) zdone = true;
      else if (rc == Z_BUF_ERROR && z.avail_out == IO_CHUNK) break;
    }
  }

  if (t->result == IPK_OK && gzip) {
    // Drain what zlib still holds.
    while (!zdone && t->result == IPK_OK) {
      z.next_out = out;
      z.avail_out = IO_CHUNK;
      int rc = inflate(&z, Z_FINISH);
      tar_feed(t, out, IO_CHUNK - z.avail_out);
      if (rc == Z_STREAM_END) zdone = true;
      else if (z.avail_out == IO_CHUNK) break;
    }
    if (!zdone) tar_fail(t, IPK_ERR_FORMAT, "truncated gzip data in package%s", "");
  }
  tar_finish(t);

 done:
  if (zinit) inflateEnd(&z);
  free(in);
  free(out);
}

static bool skip_bytes(FILE *f, uint64_t n) {
  unsigned char tmp[4096];
  while (n) {
    size_t want = n < sizeof(tmp) ? (size_t)n : sizeof(tmp);
    if (fread(tmp, 1, want, f) != want) return false;
    n -= want;
  }
  return true;
}

static bool read_package_name(const char *control, size_t len, char *package, size_t package_len) {
  const char *p = control, *end = control + len;
  while (p < end) {
    const char *eol = memchr(p, '\n', (size_t)(end - p));
    if (!eol) eol = end;
    if ((size_t)(eol - p) > 8 && !memcmp(p, "Package:", 8)) {
      const char *v = p + 8, *ve = eol;
      while (v < ve && (*v == ' ' || *v == '\t')) v++;
      while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) ve--;
      size_t vlen = (size_t)(ve - v);
      if (vlen == 0 || vlen >= package_len) return false;
      memcpy(package, v, vlen);
      package[vlen] = '\0';
      return ipk_valid_package_name(package);
    }
    p = eol + 1;
  }
  return false;
}

ipk_result ipk_check(const char *path, char *package, size_t package_len,
		     char *err, size_t err_len) {
  ipk_result result = IPK_OK;
  bool have_debian = false, have_control = false, have_data = false;
  char pkg[128] = "";

  if (err && err_len) err[0] = '\0';
  if (package && package_len) package[0] = '\0';

  FILE *f = fopen(path, "rb");
  if (!f) {
    set_err(err, err_len, "cannot open package: %s", strerror(errno));
    return IPK_ERR_IO;
  }

  char magic[8];
  if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "!<arch>\n", 8)) {
    set_err(err, err_len, "not an ipk (ar) package%s", "");
    result = IPK_ERR_FORMAT;
    goto out;
  }

  for (;;) {
    unsigned char hdr[60];
    size_t got = fread(hdr, 1, 60, f);
    if (got == 0 && feof(f)) break;
    if (got != 60 || hdr[58] != '`' || hdr[59] != '\n') {
      set_err(err, err_len, "corrupt ar header in package%s", "");
      result = IPK_ERR_FORMAT;
      goto out;
    }

    char name[17];
    memcpy(name, hdr, 16);
    name[16] = '\0';
    size_t nlen = 16;
    while (nlen && name[nlen - 1] == ' ') name[--nlen] = '\0';
    if (nlen && name[nlen - 1] == '/') name[--nlen] = '\0';

    uint64_t size = 0;
    bool digits = false;
    for (size_t i = 48; i < 58; i++) {
      if (hdr[i] >= '0' && hdr[i] <= '9') {
	if (size >> 58) { digits = false; break; }
	size = size * 10 + (uint64_t)(hdr[i] - '0');
	digits = true;
      }
      else if (hdr[i] == ' ') {
	for (size_t k = i; k < 58; k++)
	  if (hdr[k] != ' ') digits = false;
	break;
      }
      else { digits = false; break; }
    }
    if (!digits) {
      set_err(err, err_len, "corrupt ar header in package%s", "");
      result = IPK_ERR_FORMAT;
      goto out;
    }

    bool is_control = !strncmp(name, "control.tar", 11);
    bool is_data = !strncmp(name, "data.tar", 8);
    const char *suffix = is_control ? name + 11 : is_data ? name + 8 : NULL;

    if (!strcmp(name, "debian-binary")) {
      char version[16];
      if (have_debian || size < 2 || size > sizeof(version) - 1 ||
	  fread(version, 1, (size_t)size, f) != size || memcmp(version, "2.", 2)) {
	set_err(err, err_len, "bad debian-binary in package%s", "");
	result = IPK_ERR_FORMAT;
	goto out;
      }
      have_debian = true;
    }
    else if (suffix) {
      bool gzip;
      if (!strcmp(suffix, ".gz")) gzip = true;
      else if (!strcmp(suffix, "")) gzip = false;
      else {
	set_err(err, err_len, "unsupported compression in package: %s", name);
	result = IPK_ERR_COMPRESSION;
	goto out;
      }
      if ((is_control && have_control) || (is_data && have_data) || (is_data && !have_control)) {
	set_err(err, err_len, "unexpected ar member order in package: %s", name);
	result = IPK_ERR_FORMAT;
	goto out;
      }

      tar_state t;
      memset(&t, 0, sizeof(t));
      t.control = is_control;
      t.package = pkg;
      t.err = err;
      t.err_len = err_len;
      check_member(f, size, gzip, &t);
      result = t.result;
      if (result == IPK_OK && is_control &&
	  !(t.control_file && read_package_name(t.control_file, t.control_len, pkg, sizeof(pkg)))) {
	set_err(err, err_len, "no valid Package: in the package's control file%s", "");
	result = IPK_ERR_PACKAGE;
      }
      tar_free(&t);
      if (result != IPK_OK) goto out;
      if (is_control) have_control = true;
      else have_data = true;
    }
    else {
      set_err(err, err_len, "unexpected member in package: %s", name);
      result = IPK_ERR_FORMAT;
      goto out;
    }

    if ((size & 1) && !skip_bytes(f, 1)) {
      // The final padding byte may be missing; nothing follows it anyway.
      if (!feof(f)) {
	set_err(err, err_len, "cannot read package%s", "");
	result = IPK_ERR_IO;
	goto out;
      }
      break;
    }
  }

  if (!have_debian || !have_control || !have_data) {
    set_err(err, err_len, "package lacks debian-binary, control or data%s", "");
    result = IPK_ERR_FORMAT;
    goto out;
  }

  if (package && package_len) {
    if (strlen(pkg) >= package_len) {
      set_err(err, err_len, "package name too long%s", "");
      result = IPK_ERR_PACKAGE;
      goto out;
    }
    strcpy(package, pkg);
  }

 out:
  fclose(f);
  return result;
}

// ----------------------------------------------------------------------------
// Staging
// ----------------------------------------------------------------------------

bool ipk_stage(const char *src, const char *dir, char *dst, size_t dstlen, char *err, size_t errlen) {
  if (mkdir(dir, 0700) && errno != EEXIST) {
    snprintf(err, errlen, "cannot create %s: %s", dir, strerror(errno));
    return false;
  }
  struct stat st;
  if (lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077)) {
    snprintf(err, errlen, "%s is not a private directory", dir);
    return false;
  }

  if (snprintf(dst, dstlen, "%s/XXXXXX.ipk", dir) >= (int)dstlen) {
    snprintf(err, errlen, "staging path too long");
    return false;
  }

  int in = open(src, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (in < 0) {
    snprintf(err, errlen, "cannot open package: %s", strerror(errno));
    return false;
  }
  if (fstat(in, &st) || !S_ISREG(st.st_mode)) {
    snprintf(err, errlen, "package is not a regular file");
    close(in);
    return false;
  }

  int out = mkstemps(dst, 4);
  if (out < 0) {
    snprintf(err, errlen, "cannot create staging file: %s", strerror(errno));
    close(in);
    return false;
  }

  bool ok = true;
  char chunk[65536];
  for (;;) {
    ssize_t got = read(in, chunk, sizeof(chunk));
    if (got < 0 && errno == EINTR) continue;
    if (got < 0) { ok = false; break; }
    if (got == 0) break;
    for (ssize_t off = 0; off < got; ) {
      ssize_t put = write(out, chunk + off, (size_t)(got - off));
      if (put < 0 && errno == EINTR) continue;
      if (put <= 0) { ok = false; break; }
      off += put;
    }
    if (!ok) break;
  }
  if (close(out)) ok = false;
  close(in);

  if (!ok) {
    snprintf(err, errlen, "cannot copy package: %s", strerror(errno));
    unlink(dst);
    return false;
  }
  return true;
}

