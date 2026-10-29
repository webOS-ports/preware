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

#ifndef IPK_CHECK_H_
#define IPK_CHECK_H_

#include <stdbool.h>
#include <stddef.h>

//
// Checks an application package before it is installed, without unpacking it.
//
// The service installs packages as root, into /media/cryptofs/apps.  An
// application package may only carry:
//  - in control.tar.gz: control (and md5sums), so no preinst, postinst,
//    prerm, postrm or any other maintainer script;
//  - in data.tar.gz: the directories ./usr, ./usr/palm and
//    ./usr/palm/{applications,packages,services}, and below those only
//    applications/<package>, packages/<package> and services/<package> or
//    services/<package>.<name>, as directories, regular files and symbolic
//    links that cannot leave the package's own directory (relative, without
//    "..") or that point into /usr/palm/frameworks/; nothing may be unpacked
//    below a symbolic link the archive itself made.
// Hard links, device nodes, fifos, setuid or setgid files, absolute names,
// ".." and anything after the end of an archive are refused.  Every name an
// unpacker could use for an entry (the header's own, a GNU long name, a pax
// path) is checked.  Only gzip-compressed or uncompressed members are
// understood; any other compression is refused.
//

typedef enum {
  IPK_OK = 0,
  IPK_ERR_IO,           // cannot read the file
  IPK_ERR_FORMAT,       // not an ar archive with debian-binary, control and data
  IPK_ERR_COMPRESSION,  // a member compressed with something other than gzip
  IPK_ERR_SCRIPT,       // a maintainer script or other unexpected control file
  IPK_ERR_PATH,         // a data entry outside the package's directories
  IPK_ERR_TYPE,         // a hard link, device, fifo, setuid/setgid file or escaping symlink
  IPK_ERR_PACKAGE       // no or an invalid "Package:" in control
} ipk_result;

// Check the package at path.  On IPK_OK, package holds its "Package:" name.
// On any other result, err holds a reason fit to show to the user.
ipk_result ipk_check(const char *path, char *package, size_t package_len,
		     char *err, size_t err_len);

// A package name the service accepts: 1 to 127 of [A-Za-z0-9.+_-], not
// starting with '.', '-' or '+', and without "..".
bool ipk_valid_package_name(const char *name);

// The decision ipk_check() takes for one data.tar entry: name and linkname as
// in the archive, type the tar typeflag ('0', '5', '2', ...), mode its
// permission bits.  On false, err holds the reason.
bool ipk_data_entry_allowed(const char *package, const char *name, char type,
			    const char *linkname, unsigned long mode,
			    char *err, size_t err_len);

// The decision ipk_check() takes for one control.tar entry.
bool ipk_control_entry_allowed(const char *name, char type, char *err, size_t err_len);

// Copy the regular file src (not followed if it is a symbolic link) into a new
// file in dir, which is created 0700 if missing and must be a directory owned
// by us that nobody else can write or read.  The copy, readable by us only,
// is named <random>.ipk; its path goes to dst.  Check and install the copy,
// not src: src may sit where others can replace it in between.
bool ipk_stage(const char *src, const char *dir, char *dst, size_t dstlen, char *err, size_t errlen);

#endif // IPK_CHECK_H_
