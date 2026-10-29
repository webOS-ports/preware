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

#ifndef FETCH_H_
#define FETCH_H_

#include <stdbool.h>
#include <stddef.h>

//
// What the service downloads, from where, and how curl is started for it.
//
// The URLs and file names come from the caller (Preware, which reads them
// from the feeds).  They used to be pasted into shell command lines run as
// root; now curl is started with an argument vector, without a shell, and
// URLs and names are checked first:
//  - a URL is https://, or http:// only below a feed configured with an
//    http:// "src" line in an enabled *.conf of the feed directory; it has a
//    host, no user name or password, and only printable, unquoted characters;
//  - a file name is one path component of [A-Za-z0-9.+_-] that does not start
//    with '.' or '-', so it cannot be "..", hidden or an option;
//  - a file:// URL is only taken for a file directly in
//    /media/internal/.developer.
// curl is limited to the URL's protocol, also on redirects, and does not
// expand {} or [] in URLs.
//

// The opkg feed configuration on the device.
#define FETCH_FEED_CONF_DIR "/media/cryptofs/apps/etc/opkg"

// Where file:// packages and downloads live.
#define FETCH_DEVELOPER_DIR "/media/internal/.developer/"

// A file name the service may create or read in one of its directories.
bool fetch_safe_name(const char *name);

// Is url an http:// or https:// URL we can hand to curl?  On true, host (if
// given) holds its lower-case host name, without port.
bool fetch_url_parse(const char *url, bool *https, char *host, size_t host_len);

// Is the http:// url below a feed configured with http:// in an enabled
// (*.conf) file of confdir?
bool fetch_http_configured(const char *url, const char *confdir);

// May the service download url?  https:// with a valid host, or http://
// configured as above.  On false, err holds the reason.
bool fetch_url_allowed(const char *url, const char *confdir, char *err, size_t err_len);

// A file:// URL for a file directly in FETCH_DEVELOPER_DIR: the path goes
// to path.
bool fetch_developer_path(const char *url, char *path, size_t path_len);

// The hosts of the official webOS-ports feeds: the only ones that get the
// Device-Id and Auth-Token headers.
#define FETCH_OFFICIAL_FEED_HOSTS { "feeds.webos-ports.org" }

// Should the Device-Id and Auth-Token headers go to url?  Only for an
// https:// URL whose host (after parsing, without port, case-insensitive)
// is exactly one of FETCH_OFFICIAL_FEED_HOSTS; never for http or any other
// feed.
bool fetch_send_auth(const char *url);

// Write the feed configuration file dir/config as one opkg "src" (or
// "src/gz") line for the feed name at url.  config and name must pass
// fetch_safe_name(), url fetch_url_parse(); the file is created 0644 or
// truncated, and not followed if it is a symbolic link.  On false, err holds
// the reason.
bool fetch_write_feed_conf(const char *dir, const char *config, bool gzip, const char *name,
			   const char *url, char *err, size_t err_len);

// A value fit for an HTTP header: 1..255 printable characters, no CR/LF.
bool fetch_header_value_ok(const char *value);

typedef enum {
  FETCH_HEAD,      // curl -s -I: does the file exist?
  FETCH_DOWNLOAD   // curl --output <output>
} fetch_mode;

// The headers' storage for fetch_curl_argv().
typedef struct {
  char device[300];
  char token[300];
} fetch_headers;

// Build curl's argument vector for url (checked with fetch_url_allowed()
// first) into argv, which has room for max entries; returns the number of
// entries before the terminating NULL, or 0 if they do not fit or an
// argument is invalid.  output is the file for FETCH_DOWNLOAD.  When
// send_auth is set, device and token go out as the Device-Id and Auth-Token
// headers (their storage is hdr), and redirects are not followed, as curl
// would send the headers on to whichever host it is redirected to.
size_t fetch_curl_argv(const char **argv, size_t max, fetch_headers *hdr, fetch_mode mode,
		       const char *url, const char *output, bool https,
		       bool send_auth, const char *device, const char *token);

// A printable form of argv for logs and error replies: header values are
// left out.
void fetch_argv_display(const char *const *argv, char *buf, size_t len);

// Decompress the gzip file src into dst (created 0644 or truncated, not
// followed if it is a symbolic link).  On false, err holds the reason.
bool fetch_gunzip(const char *src, const char *dst, char *err, size_t err_len);

#endif // FETCH_H_
