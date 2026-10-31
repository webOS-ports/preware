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

// Harness: drives luna_methods.c's run_argv_r() (no Luna bus needed with a
// NULL message) to show that arguments reach the program verbatim, without
// a shell, and that a line of MAXLINLEN+ characters is read safely.
#include "../src/luna_methods.c"
static int fails;
#define CHECK(w, c) do { int ok_ = (c); printf("[ %s ] %s\n", ok_ ? " OK " : "FAIL", w); if (!ok_) fails++; } while (0)
int main(void) {
  const char *dir = getenv("TEST_TMPDIR") ? getenv("TEST_TMPDIR") : ".";
  char marker[600], arg[2048];
  snprintf(marker, sizeof marker, "%s/PWNED", dir);
  unlink(marker);
  snprintf(arg, sizeof arg, "x;touch %s;$(touch %s)`touch %s`\n'\"", marker, marker, marker);
  char *const av[] = { "/bin/echo", arg, NULL };
  strcpy(run_command_buffer, "{\"stdOut\": [");
  bool ok = run_argv_r(run_command_buffer, esc_buffer, buffer, false, "echo", av, NULL, NULL);
  CHECK("echo ran", ok);
  CHECK("shell syntax in an argument is not run", access(marker, F_OK) != 0);
  CHECK("argument echoed verbatim", strstr(run_command_buffer, "touch") && strstr(run_command_buffer, "$(touch"));
  char *const fail[] = { "/bin/false", NULL };
  strcpy(run_command_buffer, "{\"stdOut\": [");
  CHECK("exit status reported", !run_argv_r(run_command_buffer, esc_buffer, buffer, false, "false", fail, NULL, NULL));
  char *const missing[] = { "/nonexistent/curl", NULL };
  strcpy(run_command_buffer, "{\"stdOut\": [");
  CHECK("missing program reported", !run_argv_r(run_command_buffer, esc_buffer, buffer, false, "x", missing, NULL, NULL));
  // a line longer than MAXLINLEN
  char script[200];
  snprintf(script, sizeof script, "yes a | head -n %d | tr -d '\\n'; echo", MAXLINLEN * 2 + 7);
  char *const longline[] = { "/bin/sh", "-c", script, NULL };
  strcpy(run_command_buffer, "{\"stdOut\": [");
  CHECK("over-long line read without overflow", run_argv_r(run_command_buffer, esc_buffer, buffer, false, "long", longline, NULL, NULL)
	&& strlen(run_command_buffer) > (size_t)MAXLINLEN * 2);
  // the extractControl pattern: file name as $1
  char *const sh[] = { "/bin/sh", "-c", "/bin/echo \"$1\"", "sh", arg, NULL };
  strcpy(run_command_buffer, "{\"stdOut\": [");
  CHECK("sh -c with a positional file name", run_argv_r(run_command_buffer, esc_buffer, buffer, false, "sh", sh, NULL, NULL)
	&& access(marker, F_OK) != 0);
  printf("== %d failed ==\n", fails);
  return fails != 0;
}
