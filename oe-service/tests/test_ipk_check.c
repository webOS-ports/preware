/*
 * Tests for src/ipk_check.c.
 *
 *   test_ipk_check                  run the built-in cases
 *   test_ipk_check FILE EXPECT ...  check FILE, expect result EXPECT (a number,
 *                                   see ipk_result), for each pair
 *
 * The built-in cases assemble their packages in memory (ar around gzip'd or
 * plain tar), so that each defect can be produced exactly.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <zlib.h>

#include "ipk_check.h"

static int failures, cases;

static FILE *xfopen(const char *path, const char *mode) {
  FILE *f = fopen(path, mode);
  if (!f) { perror(path); abort(); }
  return f;
}

// ---------------------------------------------------------------- builders

typedef struct { unsigned char *p; size_t len, cap; } buf;

static void put(buf *b, const void *d, size_t n) {
  if (!n) return;
  if (b->len + n > b->cap) {
    size_t cap = (b->len + n) * 2 + 1024;
    unsigned char *np = realloc(b->p, cap);
    if (!np) abort();
    b->p = np;
    b->cap = cap;
  }
  memcpy(b->p + b->len, d, n);
  b->len += n;
}

static void zeros(buf *b, size_t n) {
  unsigned char z[512] = {0};
  while (n) { size_t k = n < 512 ? n : 512; put(b, z, k); n -= k; }
}

typedef enum { FMT_GNU, FMT_USTAR } tar_fmt;

// One tar header block (plus data and padding).
static void tar_entry_raw(buf *b, tar_fmt fmt, const char *name, char type, const char *link,
			  unsigned mode, const void *data, size_t size, const char *prefix,
			  int corrupt_checksum) {
  unsigned char h[512] = {0};
  strncpy((char *)h, name, 100);
  snprintf((char *)h + 100, 8, "%07o", mode);
  snprintf((char *)h + 108, 8, "%07o", 0);
  snprintf((char *)h + 116, 8, "%07o", 0);
  snprintf((char *)h + 124, 12, "%011lo", (unsigned long)(size & 077777777777UL));
  snprintf((char *)h + 136, 12, "%011lo", 0UL);
  h[156] = (unsigned char)type;
  if (link) strncpy((char *)h + 157, link, 100);
  if (fmt == FMT_GNU) memcpy(h + 257, "ustar  ", 8);
  else { memcpy(h + 257, "ustar", 6); memcpy(h + 263, "00", 2); }
  if (prefix) strncpy((char *)h + 345, prefix, 155);
  memset(h + 148, ' ', 8);
  unsigned sum = 0;
  for (int i = 0; i < 512; i++) sum += h[i];
  if (corrupt_checksum) sum += 1;
  snprintf((char *)h + 148, 8, "%06o", sum);
  h[155] = ' ';
  put(b, h, 512);
  if (size) {
    put(b, data, size);
    zeros(b, (512 - size % 512) % 512);
  }
}

static void tar_file(buf *b, const char *name, const char *content) {
  tar_entry_raw(b, FMT_GNU, name, '0', NULL, 0644, content, strlen(content), NULL, 0);
}

static void tar_dir(buf *b, const char *name) {
  tar_entry_raw(b, FMT_GNU, name, '5', NULL, 0755, NULL, 0, NULL, 0);
}

static void tar_end(buf *b) { zeros(b, 1024); }

static void gzip_buf(const buf *in, buf *out) {
  z_stream z;
  memset(&z, 0, sizeof(z));
  if (deflateInit2(&z, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) abort();
  unsigned char o[65536];
  z.next_in = in->p;
  z.avail_in = (uInt)in->len;
  int rc;
  do {
    z.next_out = o;
    z.avail_out = sizeof(o);
    rc = deflate(&z, Z_FINISH);
    put(out, o, sizeof(o) - z.avail_out);
  } while (rc != Z_STREAM_END);
  deflateEnd(&z);
}

static void ar_member(buf *b, const char *name, const buf *data) {
  char h[61];
  snprintf(h, sizeof(h), "%-16s%-12s%-6s%-6s%-8s%-10lu`\n", name, "0", "0", "0", "100644",
	   (unsigned long)data->len);
  put(b, h, 60);
  put(b, data->p, data->len);
  if (data->len & 1) put(b, "\n", 1);
}

static buf control_tar(const char *package, const char *extra_name) {
  buf t = {0};
  char control[256];
  snprintf(control, sizeof(control),
	   "Package: %s\nVersion: 1.0.0\nArchitecture: all\nMaintainer: Test\nDescription: test\n", package);
  tar_dir(&t, "./");
  tar_file(&t, "./control", control);
  if (extra_name) tar_file(&t, extra_name, "#!/bin/sh\nrm -rf /\n");
  tar_end(&t);
  return t;
}

static buf data_tar_ok(const char *package) {
  buf t = {0};
  char p[512];
  tar_dir(&t, "./");
  tar_dir(&t, "./usr/");
  tar_dir(&t, "./usr/palm/");
  tar_dir(&t, "./usr/palm/applications/");
  snprintf(p, sizeof(p), "./usr/palm/applications/%s/", package);
  tar_dir(&t, p);
  snprintf(p, sizeof(p), "./usr/palm/applications/%s/appinfo.json", package);
  tar_file(&t, p, "{}");
  snprintf(p, sizeof(p), "./usr/palm/applications/%s/index.html", package);
  tar_file(&t, p, "<html></html>");
  snprintf(p, sizeof(p), "./usr/palm/packages/%s/packageinfo.json", package);
  tar_file(&t, p, "{}");
  snprintf(p, sizeof(p), "./usr/palm/services/%s.service/services.json", package);
  tar_file(&t, p, "{}");
  snprintf(p, sizeof(p), "./usr/palm/applications/%s/images/link", package);
  tar_entry_raw(&t, FMT_GNU, p, '2', "index.html", 0777, NULL, 0, NULL, 0);
  return t;  // caller adds entries and tar_end()
}

static char *write_ipk(const buf *control, const buf *data, int gz_control, int gz_data,
		       const char *control_name, const char *data_name, int with_debian) {
  static int seq;
  buf ar = {0}, deb = {0}, c = {0}, d = {0};
  put(&ar, "!<arch>\n", 8);
  put(&deb, "2.0\n", 4);
  if (with_debian) ar_member(&ar, "debian-binary", &deb);
  if (gz_control) gzip_buf(control, &c); else put(&c, control->p, control->len);
  if (data) {
    if (gz_data) gzip_buf(data, &d); else put(&d, data->p, data->len);
  }
  ar_member(&ar, control_name ? control_name : (gz_control ? "control.tar.gz" : "control.tar"), &c);
  if (data) ar_member(&ar, data_name ? data_name : (gz_data ? "data.tar.gz" : "data.tar"), &d);

  char *path = malloc(512);
  if (!path) abort();
  snprintf(path, 512, "%s/ipk-test-%d-%d.ipk", getenv("TEST_TMPDIR") ? getenv("TEST_TMPDIR") : ".",
	   (int)getpid(), seq++);
  FILE *f = xfopen(path, "wb");
  if (fwrite(ar.p, 1, ar.len, f) != ar.len) abort();
  fclose(f);
  free(ar.p); free(deb.p); free(c.p); free(d.p);
  return path;
}

static void expect(const char *what, const char *path, ipk_result want, const char *want_pkg) {
  char pkg[128], err[512];
  ipk_result got = ipk_check(path, pkg, sizeof(pkg), err, sizeof(err));
  cases++;
  int ok = got == want && (!want_pkg || !strcmp(pkg, want_pkg));
  if (!ok) failures++;
  printf("[ %s ] %s: got %d want %d%s%s\n", ok ? " OK " : "FAIL", what, got, want,
	 *err ? " - " : "", err);
}

// One package: standard control, data from data_tar_ok() plus extra entries.
typedef void (*add_fn)(buf *);

static void case_data(const char *what, add_fn add, ipk_result want) {
  buf c = control_tar("com.example.app", NULL);
  buf d = data_tar_ok("com.example.app");
  if (add) add(&d);
  tar_end(&d);
  char *path = write_ipk(&c, &d, 1, 1, NULL, NULL, 1);
  expect(what, path, want, want == IPK_OK ? "com.example.app" : NULL);
  unlink(path);
  free(path); free(c.p); free(d.p);
}

static void add_etc(buf *d) { tar_file(d, "./etc/passwd", "root::0:0::/:/bin/sh\n"); }
static void add_dotdot(buf *d) { tar_file(d, "./usr/palm/applications/com.example.app/../../../../etc/x", "x"); }
static void add_abs(buf *d) { tar_file(d, "/etc/x", "x"); }
static void add_other_app(buf *d) { tar_file(d, "./usr/palm/applications/com.palm.app.phone/index.html", "x"); }
static void add_other_service(buf *d) { tar_file(d, "./usr/palm/services/com.palm.service.x/a", "x"); }
static void add_prefix_service(buf *d) { tar_file(d, "./usr/palm/services/com.example.appx/a", "x"); }
static void add_file_at_top(buf *d) { tar_file(d, "./usr/palm/applications", "x"); }
static void add_usr_bin(buf *d) { tar_file(d, "./usr/bin/evil", "x"); }
static void add_symlink_abs(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/l", '2', "/etc", 0777, NULL, 0, NULL, 0);
}
static void add_symlink_up(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/l", '2', "a/../../x", 0777, NULL, 0, NULL, 0);
}
static void add_symlink_root(buf *d) {
  // the package directory itself as a link to a sibling
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app", '2', "com.palm.app.phone", 0777, NULL, 0, NULL, 0);
}
static void add_hardlink(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/h", '1', "etc/shadow", 0644, NULL, 0, NULL, 0);
}
static void add_device(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/mem", '3', NULL, 0666, NULL, 0, NULL, 0);
}
static void add_fifo(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/f", '6', NULL, 0666, NULL, 0, NULL, 0);
}
static void add_setuid(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/su", '0', NULL, 04755, "x", 1, NULL, 0);
}
static void add_longname_outside(buf *d) {
  const char *real = "./etc/cron.d/very/long/name/that/does/not/fit/in/one/hundred/characters/of/a/plain/tar/header/x";
  tar_entry_raw(d, FMT_GNU, "././@LongLink", 'L', NULL, 0644, real, strlen(real) + 1, NULL, 0);
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/innocent", '0', NULL, 0644, "x", 1, NULL, 0);
}
static void add_longname_ok(buf *d) {
  const char *real = "./usr/palm/applications/com.example.app/a/really/long/path/name/that/does/not/fit/in/one/hundred/chars.js";
  tar_entry_raw(d, FMT_GNU, "././@LongLink", 'L', NULL, 0644, real, strlen(real) + 1, NULL, 0);
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/a/really/long/path/name/that/does/not/f", '0', NULL, 0644, "x", 1, NULL, 0);
}
static void add_pax_outside(buf *d) {
  const char *rec = "20 path=etc/passwd2\n";
  tar_entry_raw(d, FMT_USTAR, "./PaxHeaders/x", 'x', NULL, 0644, rec, strlen(rec), NULL, 0);
  tar_entry_raw(d, FMT_USTAR, "usr/palm/applications/com.example.app/x", '0', NULL, 0644, "x", 1, NULL, 0);
}
static void add_pax_linkpath(buf *d) {
  const char *rec = "17 linkpath=/etc\n";
  tar_entry_raw(d, FMT_USTAR, "./PaxHeaders/l", 'x', NULL, 0644, rec, strlen(rec), NULL, 0);
  tar_entry_raw(d, FMT_USTAR, "usr/palm/applications/com.example.app/d/l", '2', "ok", 0777, NULL, 0, NULL, 0);
}
static void add_pax_global(buf *d) {
  const char *rec = "16 path=etc/xyz\n";
  tar_entry_raw(d, FMT_USTAR, "./PaxHeaders/g", 'g', NULL, 0644, rec, strlen(rec), NULL, 0);
}
static void add_pax_bad(buf *d) {
  const char *rec = "99 path=x\n";
  tar_entry_raw(d, FMT_USTAR, "./PaxHeaders/x", 'x', NULL, 0644, rec, strlen(rec), NULL, 0);
  tar_file(d, "./usr/palm/applications/com.example.app/y", "y");
}
static void add_ustar_prefix_outside(buf *d) {
  tar_entry_raw(d, FMT_USTAR, "x", '0', NULL, 0644, "x", 1, "etc", 0);
}
static void add_ustar_prefix_ok(buf *d) {
  tar_entry_raw(d, FMT_USTAR, "x.js", '0', NULL, 0644, "x", 1, "usr/palm/applications/com.example.app/lib", 0);
}
static void add_bad_checksum(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/c", '0', NULL, 0644, "x", 1, NULL, 1);
}
static void add_after_end(buf *d) {
  tar_end(d);
  tar_file(d, "./etc/passwd", "x");
}
static void add_framework_link(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/cordova.js", '2',
		"/usr/palm/frameworks/cordova/cordova.js", 0777, NULL, 0, NULL, 0);
}
static void add_framework_link_up(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/c.js", '2',
		"/usr/palm/frameworks/../../../etc/shadow", 0777, NULL, 0, NULL, 0);
}
static void add_framework_dir_link(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/fw", '2',
		"/usr/palm/frameworks/", 0777, NULL, 0, NULL, 0);
}
static void add_write_through_link(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/lib", '2',
		"/usr/palm/frameworks/enyo", 0777, NULL, 0, NULL, 0);
  tar_file(d, "./usr/palm/applications/com.example.app/lib/enyo.js", "overwritten");
}
static void add_write_through_relative(buf *d) {
  tar_entry_raw(d, FMT_GNU, "./usr/palm/applications/com.example.app/a/l", '2', "b", 0777, NULL, 0, NULL, 0);
  tar_file(d, "./usr/palm/applications/com.example.app/a/l/x", "x");
}
static void add_dot_component(buf *d) { tar_file(d, "./usr/palm/./applications/com.example.app/z", "z"); }
static void add_double_slash(buf *d) { tar_file(d, "./usr//palm/applications/com.example.app/z", "z"); }

static void test_data_cases(void) {
  case_data("valid application package", NULL, IPK_OK);
  case_data("long name inside the app", add_longname_ok, IPK_OK);
  case_data("ustar prefix inside the app", add_ustar_prefix_ok, IPK_OK);
  case_data("file in /etc", add_etc, IPK_ERR_PATH);
  case_data("name with ..", add_dotdot, IPK_ERR_PATH);
  case_data("absolute name", add_abs, IPK_ERR_PATH);
  case_data("another app's directory", add_other_app, IPK_ERR_PATH);
  case_data("another package's service", add_other_service, IPK_ERR_PATH);
  case_data("service named with the id as a mere prefix", add_prefix_service, IPK_ERR_PATH);
  case_data("file where a directory belongs", add_file_at_top, IPK_ERR_PATH);
  case_data("file in /usr/bin", add_usr_bin, IPK_ERR_PATH);
  case_data("absolute symlink", add_symlink_abs, IPK_ERR_TYPE);
  case_data("symlink climbing out", add_symlink_up, IPK_ERR_TYPE);
  case_data("package directory as a symlink", add_symlink_root, IPK_ERR_TYPE);
  case_data("hard link", add_hardlink, IPK_ERR_TYPE);
  case_data("character device", add_device, IPK_ERR_TYPE);
  case_data("fifo", add_fifo, IPK_ERR_TYPE);
  case_data("setuid file", add_setuid, IPK_ERR_TYPE);
  case_data("GNU long name outside", add_longname_outside, IPK_ERR_PATH);
  case_data("pax path outside", add_pax_outside, IPK_ERR_PATH);
  case_data("pax linkpath absolute", add_pax_linkpath, IPK_ERR_TYPE);
  case_data("global pax path", add_pax_global, IPK_ERR_FORMAT);
  case_data("malformed pax record", add_pax_bad, IPK_ERR_FORMAT);
  case_data("ustar prefix outside", add_ustar_prefix_outside, IPK_ERR_PATH);
  case_data("bad header checksum", add_bad_checksum, IPK_ERR_FORMAT);
  case_data("entry after end of archive", add_after_end, IPK_ERR_FORMAT);
  case_data("link into the frameworks", add_framework_link, IPK_OK);
  case_data("link into the frameworks with ..", add_framework_link_up, IPK_ERR_TYPE);
  case_data("link to the frameworks directory itself", add_framework_dir_link, IPK_ERR_TYPE);
  case_data("file written through an absolute link", add_write_through_link, IPK_ERR_TYPE);
  case_data("file written through a relative link", add_write_through_relative, IPK_ERR_TYPE);
  case_data("'.' component", add_dot_component, IPK_ERR_PATH);
  case_data("empty component", add_double_slash, IPK_ERR_PATH);
}

static void case_control(const char *what, const char *extra, ipk_result want) {
  buf c = control_tar("com.example.app", extra);
  buf d = data_tar_ok("com.example.app");
  tar_end(&d);
  char *path = write_ipk(&c, &d, 1, 1, NULL, NULL, 1);
  expect(what, path, want, NULL);
  unlink(path);
  free(path); free(c.p); free(d.p);
}

static void test_control_cases(void) {
  case_control("postinst", "./postinst", IPK_ERR_SCRIPT);
  case_control("preinst", "./preinst", IPK_ERR_SCRIPT);
  case_control("prerm", "./prerm", IPK_ERR_SCRIPT);
  case_control("postrm", "./postrm", IPK_ERR_SCRIPT);
  case_control("postinst without ./", "postinst", IPK_ERR_SCRIPT);
  case_control("other control file", "./triggers", IPK_ERR_SCRIPT);
  case_control("md5sums is fine", "./md5sums", IPK_OK);
}

static void test_container_cases(void) {
  buf c, d;
  char *path;

  // package name in control must be valid
  c = control_tar("../../etc", NULL);
  d = data_tar_ok("com.example.app"); tar_end(&d);
  path = write_ipk(&c, &d, 1, 1, NULL, NULL, 1);
  expect("invalid Package: name", path, IPK_ERR_PACKAGE, NULL);
  unlink(path); free(path); free(c.p); free(d.p);

  // data paths are checked against the control's package, not a caller's
  c = control_tar("com.other.app", NULL);
  d = data_tar_ok("com.example.app"); tar_end(&d);
  path = write_ipk(&c, &d, 1, 1, NULL, NULL, 1);
  expect("data for a different package", path, IPK_ERR_PATH, NULL);
  unlink(path); free(path); free(c.p); free(d.p);

  // uncompressed members are fine
  c = control_tar("com.example.app", NULL);
  d = data_tar_ok("com.example.app"); tar_end(&d);
  path = write_ipk(&c, &d, 0, 0, NULL, NULL, 1);
  expect("uncompressed members", path, IPK_OK, "com.example.app");
  unlink(path); free(path);

  // other compressions are refused, not guessed at
  path = write_ipk(&c, &d, 1, 1, NULL, "data.tar.xz", 1);
  expect("xz data", path, IPK_ERR_COMPRESSION, NULL);
  unlink(path); free(path);

  path = write_ipk(&c, &d, 1, 1, NULL, "data.tar.zst", 1);
  expect("zstd data", path, IPK_ERR_COMPRESSION, NULL);
  unlink(path); free(path);

  path = write_ipk(&c, &d, 1, 1, NULL, "evil.sh", 1);
  expect("unknown ar member", path, IPK_ERR_FORMAT, NULL);
  unlink(path); free(path);

  path = write_ipk(&c, &d, 1, 1, NULL, NULL, 0);
  expect("no debian-binary", path, IPK_ERR_FORMAT, NULL);
  unlink(path); free(path);

  path = write_ipk(&c, NULL, 1, 1, NULL, NULL, 1);
  expect("no data member", path, IPK_ERR_FORMAT, NULL);
  unlink(path); free(path);

  // a gzip member with something appended
  {
    buf ar = {0}, deb = {0}, cz = {0}, dz = {0}, tail = {0};
    put(&ar, "!<arch>\n", 8);
    put(&deb, "2.0\n", 4);
    ar_member(&ar, "debian-binary", &deb);
    gzip_buf(&c, &cz);
    ar_member(&ar, "control.tar.gz", &cz);
    gzip_buf(&d, &dz);
    tar_file(&tail, "./etc/passwd", "x");
    tar_end(&tail);
    gzip_buf(&tail, &dz);  // second gzip member, concatenated
    ar_member(&ar, "data.tar.gz", &dz);
    char p2[512];
    snprintf(p2, sizeof(p2), "%s/ipk-test-%d-cat.ipk", getenv("TEST_TMPDIR") ? getenv("TEST_TMPDIR") : ".", (int)getpid());
    FILE *f = xfopen(p2, "wb");
    if (fwrite(ar.p, 1, ar.len, f) != ar.len) abort();
    fclose(f);
    expect("second gzip member after the archive end", p2, IPK_ERR_FORMAT, NULL);
    unlink(p2);
    free(ar.p); free(deb.p); free(cz.p); free(dz.p); free(tail.p);
  }

  // truncated file
  {
    path = write_ipk(&c, &d, 1, 1, NULL, NULL, 1);
    FILE *f = xfopen(path, "rb+");
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fclose(f);
    if (truncate(path, len - 40) != 0) abort();
    expect("truncated package", path, IPK_ERR_IO, NULL);
    unlink(path); free(path);
  }

  {
    char err[256];
    char pkg[128];
    FILE *f;
    char p3[512];
    snprintf(p3, sizeof(p3), "%s/ipk-test-%d-notar.ipk", getenv("TEST_TMPDIR") ? getenv("TEST_TMPDIR") : ".", (int)getpid());
    f = xfopen(p3, "wb");
    fputs("#!/bin/sh\necho not a package\n", f);
    fclose(f);
    expect("not an ar file", p3, IPK_ERR_FORMAT, NULL);
    unlink(p3);
    cases++;
    if (ipk_check("/nonexistent/x.ipk", pkg, sizeof(pkg), err, sizeof(err)) != IPK_ERR_IO) {
      failures++;
      printf("[ FAIL ] missing file\n");
    }
    else printf("[  OK  ] missing file\n");
  }

  free(c.p); free(d.p);
}

static void check(const char *what, int cond) {
  cases++;
  if (!cond) failures++;
  printf("[ %s ] %s\n", cond ? " OK " : "FAIL", what);
}

static void test_names(void) {
  check("plain id", ipk_valid_package_name("org.webosports.app.preware"));
  check("id with +_-", ipk_valid_package_name("com.example.my-app_2+x"));
  check("no empty", !ipk_valid_package_name(""));
  check("no slash", !ipk_valid_package_name("a/b"));
  check("no ..", !ipk_valid_package_name("a..b"));
  check("no leading dot", !ipk_valid_package_name(".hidden"));
  check("no leading dash", !ipk_valid_package_name("-rf"));
  check("no space", !ipk_valid_package_name("a b"));
  check("no shell", !ipk_valid_package_name("a;reboot"));
  char longname[200];
  memset(longname, 'a', sizeof(longname) - 1);
  longname[sizeof(longname) - 1] = '\0';
  check("no overlong", !ipk_valid_package_name(longname));

  char err[256];
  check("dir root ok", ipk_data_entry_allowed("p", "./", '5', NULL, 0755, err, sizeof(err)));
  check("usr as file refused", !ipk_data_entry_allowed("p", "./usr", '0', NULL, 0644, err, sizeof(err)));
  check("nested symlink ok", ipk_data_entry_allowed("p", "usr/palm/applications/p/a/b", '2', "c/d", 0777, err, sizeof(err)));
  check("symlink to . ok", ipk_data_entry_allowed("p", "usr/palm/applications/p/a/b", '2', ".", 0777, err, sizeof(err)));
  check("empty link refused", !ipk_data_entry_allowed("p", "usr/palm/applications/p/a/b", '2', "", 0777, err, sizeof(err)));
  check("service p. refused", !ipk_data_entry_allowed("p", "usr/palm/services/p./x", '0', NULL, 0644, err, sizeof(err)));
  check("service p.s ok", ipk_data_entry_allowed("p", "usr/palm/services/p.s/x", '0', NULL, 0644, err, sizeof(err)));
  check("setgid refused", !ipk_data_entry_allowed("p", "usr/palm/applications/p/x", '0', NULL, 02755, err, sizeof(err)));
  check("control dir ok", ipk_control_entry_allowed(".", '5', err, sizeof(err)));
  check("control subdir refused", !ipk_control_entry_allowed("./x/control", '0', err, sizeof(err)));
  check("control as symlink refused", !ipk_control_entry_allowed("./control", '2', err, sizeof(err)));
}

static void test_stage(void) {
  const char *base = getenv("TEST_TMPDIR") ? getenv("TEST_TMPDIR") : ".";
  char dir[512], src[512], link[512], dst[600], err[256];
  snprintf(dir, sizeof(dir), "%s/stage-%d", base, (int)getpid());
  snprintf(src, sizeof(src), "%s/src-%d.ipk", base, (int)getpid());
  snprintf(link, sizeof(link), "%s/link-%d.ipk", base, (int)getpid());
  FILE *f = xfopen(src, "wb");
  fputs("package bytes", f);
  fclose(f);

  check("stage copies the package", ipk_stage(src, dir, dst, sizeof(dst), err, sizeof(err)));
  struct stat st;
  check("staged copy is private", !stat(dst, &st) && (st.st_mode & 0777) == 0600 && st.st_size == 13);
  check("staged copy ends in .ipk", strlen(dst) > 4 && !strcmp(dst + strlen(dst) - 4, ".ipk"));
  check("staging dir is private", !stat(dir, &st) && (st.st_mode & 0777) == 0700);
  // the copy does not change when the original does
  f = xfopen(src, "wb");
  fputs("swapped!", f);
  fclose(f);
  check("copy unaffected by a later swap", !stat(dst, &st) && st.st_size == 13);
  unlink(dst);

  if (symlink(src, link)) abort();
  check("symlink source refused", !ipk_stage(link, dir, dst, sizeof(dst), err, sizeof(err)));
  check("directory source refused", !ipk_stage(dir, dir, dst, sizeof(dst), err, sizeof(err)));
  chmod(dir, 0755);
  check("shared staging dir refused", !ipk_stage(src, dir, dst, sizeof(dst), err, sizeof(err)));
  char small[8];
  chmod(dir, 0700);
  check("short dst refused", !ipk_stage(src, dir, small, sizeof(small), err, sizeof(err)));
  unlink(link);
  unlink(src);
  rmdir(dir);
}

int main(int argc, char **argv) {
  if (argc > 1) {
    for (int i = 1; i + 1 < argc; i += 2) {
      char pkg[128], err[512];
      ipk_result got = ipk_check(argv[i], pkg, sizeof(pkg), err, sizeof(err));
      int want = atoi(argv[i + 1]);
      cases++;
      if ((int)got != want) failures++;
      printf("[ %s ] %s: got %d want %d%s%s\n", (int)got == want ? " OK " : "FAIL", argv[i], got, want,
	     *err ? " - " : "", err);
    }
  }
  else {
    test_names();
    test_data_cases();
    test_control_cases();
    test_container_cases();
    test_stage();
  }
  printf("== %d cases, %d failed ==\n", cases, failures);
  return failures ? 1 : 0;
}
