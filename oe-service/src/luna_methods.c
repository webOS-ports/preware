/*=============================================================================
 Copyright (C) 2010 WebOS Internals <support@webos-internals.org>

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

// pipe2(), for the pipes spawn_reader() opens while other threads spawn too.
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <errno.h>
#include <spawn.h>

#include <json.h>

#include "luna_service.h"
#include "luna_methods.h"
#include "ipk_check.h"
#include "fetch.h"

#define ALLOWED_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-+_"

// 18 is the first version where downloadFeed is safe to call for several feeds
// at once.  Preware checks for it before downloading feeds in parallel.
#define API_VERSION "18"

//
// We use static buffers instead of continually allocating and deallocating stuff,
// since we're a long-running service, and do not want to leak anything.
//
static char buffer[MAXBUFLEN];
static char esc_buffer[MAXBUFLEN];
static char run_command_buffer[MAXBUFLEN];
// The file readers ship much larger pieces than a command line, so they get
// their own buffers rather than sharing the MAXBUFLEN ones above.
static char read_file_buffer[ESCCHUNKSIZE+256];
static char read_esc_buffer[ESCCHUNKSIZE];

// These are used for CDN downloads
static char device[MAXNAMLEN];
static char token[MAXNAMLEN];

//
// Feed downloads run in their own threads, and Preware may start several of
// them at once, so they cannot use the static buffers above without scribbling
// over each other.  Each download allocates one of these instead, and uses the
// reentrant _r variants of the helpers below.  Everything else in this service
// is called one operation at a time by the app, and still uses the statics.
//
typedef struct {
  char out[MAXBUFLEN];		// accumulates the command output for the reply
  char esc[MAXBUFLEN];		// scratch used when escaping strings into JSON
  char msg[MAXBUFLEN];		// scratch used when building a reply message
} command_buffers;

//
// liblunaservice makes no promise about being called from several threads at
// once, so the parallel downloads hold this while they reply.  Only the reply
// is serialised, which is brief; the downloads themselves still run alongside
// each other, which is where all of the time actually goes.
//
static pthread_mutex_t respond_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool locked_respond(LSMessage *message, const char *reply, LSError *lserror) {
  pthread_mutex_lock(&respond_mutex);
  bool returnValue = LSMessageRespond(message, reply, lserror);
  pthread_mutex_unlock(&respond_mutex);
  return returnValue;
}

//
// The apps that may use this service: Preware 2 on LuneOS, and the build of
// Preware 2 that also runs on legacy webOS. That one has to use an id in the
// com.palm namespace there, to be allowed to show the icons of other apps.
//
static const char *allowed_app_ids[] = {
  "org.webosports.app.preware",
  "com.palm.app.preware2",
  NULL
};

static bool app_allowed(const char *appId) {
  if (!appId) return false;

  for (int i = 0; allowed_app_ids[i]; i++) {
    size_t len = strlen(allowed_app_ids[i]);
    // The application id may be followed by a space and more
    if (!strncmp(appId, allowed_app_ids[i], len) && (appId[len] == '\0' || appId[len] == ' '))
      return true;
  }

  return false;
}

static bool access_denied(LSMessage *message) {
  LSError lserror;
  LSErrorInit(&lserror);

  if (!app_allowed(LSMessageGetApplicationID(message))) {
    if (!LSMessageRespond(message, "{\"returnValue\": false, \"errorText\": \"Unauthorised access\"}", &lserror)) {
      LSErrorPrint(&lserror, stderr);
      LSErrorFree(&lserror);
    }
    return true;
  }

  return false;
}

//
// Escape a string so that it can be used directly in a JSON response.
// In general, this means escaping quotes, backslashes and control chars.
// It writes into the supplied escape buffer, which must be large enough
// for the worst case expansion of the input string.
//
static char *json_escape_str_r(char *escbuf, size_t esccap, char *str)
{
  const char *json_hex_chars = "0123456789abcdef";

  // Initialise the output buffer
  strcpy(escbuf, "");

  // Check the constraints on the input string.  A control character becomes
  // six bytes (\u00xx), so that is the expansion we have to have room for.
  if ((strlen(str) * 6 + 1) > esccap) return escbuf;

  // Initialise the pointers used to step through the input and output.
  char *resultsPt = escbuf;
  int pos = 0, start_offset = 0;

  // Traverse the input, copying to the output in the largest chunks
  // possible, escaping characters as we go.
  unsigned char c;
  do {
    c = str[pos];
    switch (c) {
    case '\0':
      // Terminate the copying
      break;
    case '\b':
    case '\n':
    case '\r':
    case '\t':
    case '"':
    case '\\': {
      // Copy the chunk before the character which must be escaped
      if (pos - start_offset > 0) {
	memcpy(resultsPt, str + start_offset, pos - start_offset);
	resultsPt += pos - start_offset;
      };

      // Escape the character
      if      (c == '\b') {memcpy(resultsPt, "\\b",  2); resultsPt += 2;}
      else if (c == '\n') {memcpy(resultsPt, "\\n",  2); resultsPt += 2;}
      else if (c == '\r') {memcpy(resultsPt, "\\r",  2); resultsPt += 2;}
      else if (c == '\t') {memcpy(resultsPt, "\\t",  2); resultsPt += 2;}
      else if (c == '"')  {memcpy(resultsPt, "\\\"", 2); resultsPt += 2;}
      else if (c == '\\') {memcpy(resultsPt, "\\\\", 2); resultsPt += 2;}

      // Reset the start of the next chunk
      start_offset = ++pos;
      break;
    }

    default:

      // Check for "special" characters
      if ((c < ' ') || (c > 127)) {

	// Copy the chunk before the character which must be escaped
	if (pos - start_offset > 0) {
	  memcpy(resultsPt, str + start_offset, pos - start_offset);
	  resultsPt += pos - start_offset;
	}

	// Insert a normalised representation
	sprintf(resultsPt, "\\u00%c%c",
		json_hex_chars[c >> 4],
		json_hex_chars[c & 0xf]);

	// Reset the start of the next chunk
	start_offset = ++pos;
      }
      else {
	// Just move along the source string, without copying
	pos++;
      }
    }
  } while (c);

  // Copy the final chunk, if required
  if (pos - start_offset > 0) {
    memcpy(resultsPt, str + start_offset, pos - start_offset);
    resultsPt += pos - start_offset;
  }

  // Terminate the output buffer ...
  memcpy(resultsPt, "\0", 1);

  // and return a pointer to it.
  return escbuf;
}

//
// Escape a string using the process-wide static buffer.  Safe for everything
// that is called one operation at a time, which is everything except the
// feed downloads.
//
static char *json_escape_str(char *str)
{
  return json_escape_str_r(esc_buffer, MAXBUFLEN, str);
}

//
// A dummy method, useful for unimplemented functions or as a status function.
// Called directly from webOS, and returns directly to webOS.
//
bool dummy_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  if (!LSMessageRespond(message, "{\"returnValue\": true}", &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

//
// Return the current API version of the service.
// Called directly from webOS, and returns directly to webOS.
//
bool version_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  if (!LSMessageRespond(message, "{\"returnValue\": true, \"version\": \"" VERSION "\", \"apiVersion\": \"" API_VERSION "\"}", &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

//
// Restart ipkgservice.
//
bool restart_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);
  (void)LSMessageRespond(message, "{\"returnValue\": true}", &lserror);
  if (system("/usr/bin/killall org.webosports.service.ipkg") != 0) {
    fprintf(stderr, "Unable to restart the service\n");
  }
  // It's likely that this point will never be reached.
  return true;
}

//
// A function pointer, used to filter output messages from commands.
// The input string is assumed to be a buffer large enough to hold
// the filtered output string, and is forcibly overwritten.
// The return value says whether this message should be considered
// to be an immediate terminating error condition from the command.
//
typedef bool (*subscribefun)(char *);

//
// Pass through all messages unchanged.
//
static bool passthrough(char *message) {
  return true;
}

//
// Parse progress messages from curl --verbose and format for display in webOS.
// curl provides a correct command return status value, so we always return true.
// Messages are of the form:
// 100  164k  100  164k    0     0  10055      0  0:00:16  0:00:16 --:--:-- 37284
//
static bool downloadstats(char *message) {

  // Buffers to store the extracted data
  char total[MAXNUMLEN];
  char current[MAXNUMLEN];
  char togo[MAXNUMLEN];
  char speed[MAXNUMLEN];

  // Check for curl progress messages, and extract the relevant data.
  // The widths keep each field inside its MAXNUMLEN sized buffer.
  if ((sscanf(message, "%*s %31s %*s %31s %*s %*s %*s %*s %*s %*s %31s %31s",
	      total, current, togo, speed) == 4) &&
      // Ignore the first line and initial fetch latency
      strcmp(speed, "0") && strcmp(speed, "Current")) {
    // Format in a short human-readable format.
    sprintf(message, "Transferred: %s / %s<br>Time Left: %s<br>Transfer Speed: %s",
	    current, total, togo, speed);
    return true;
  }

  // Nullify any other messages
  strcpy(message, "");
  return true;
}

//
// Parse progress messages from curl --verbose and format for display in webOS.
// curl provides a correct command return status value, so we always return true.
//
static bool verifyheader(char *message) {

  // Check for curl progress messages, and extract the relevant data
  if (!strncmp(message, "error:", 6)) {
    return false;
  }

  // Nullify any other messages except the HTTP status line
  if (strncmp(message, "HTTP/", 5)) {
    strcpy(message, "");
  }
  return true;
}

//
// Parse progress messages from the com.palm.appinstaller service and format for display in webOS.
// We also need to force a termination on install failures (since luna-send will hang early).
// Messages are of the form:
// ** Message: serviceResponse Handling: 2, { \"ticket\":28 , \"status\":\"VERIFYING\" }
//
static bool appinstaller(char *message) {

  // A buffer to store the extracted data
  char status[MAXLINLEN];

  // Check for appinstaller progress messages, and extract the relevant data
  if ((sscanf(message, "%*s %*s %*s %*s %*s { %*s , \"status\":\"%s }",
	      status) == 1)) {

    // The last string field will still have the ending ", so remove that.
    status[strlen(status)-1] = '\0';

    // %%% We may want to do a mapping to more end-user friendly messages here. %%%

    // Format in a short human-readable format.
    sprintf(message, "%s", status);

    // Terminate the command early if there is an install failure.
    if (!strcmp(status, "FAILED_OPKG_INSTALL")) {
      return false;
    }

    return true;
  }

  // Nullify any other messages
  strcpy(message, "");
  return true;
}

//
// Run a shell command, and return the output in-line in a buffer for returning to webOS.
// If message and subscriber are defined, then also send back status messages.
// The outbuf must be initialised before calling this function.  The escbuf and
// msgbuf are scratch space, and must be distinct from outbuf and each other.
// If locked is set, replies are serialised against the other threads.
// The return value says whether the command executed successfully or not.
//
static bool run_process_r(char *outbuf, char *escbuf, char *msgbuf, bool locked,
			  const char *command, char *const argv[],
			  LSMessage *message, subscribefun subscriber);

static bool run_command_r(char *outbuf, char *escbuf, char *msgbuf, bool locked,
			  const char *command, LSMessage *message, subscribefun subscriber) {
  return run_process_r(outbuf, escbuf, msgbuf, locked, command, NULL, message, subscriber);
}

//
// Start argv[0] with argv, without a shell, its stdout and stderr on the
// returned stream and stdin on /dev/null.  The pid goes to *pid.
//
static FILE *spawn_reader(char *const argv[], pid_t *pid) {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC)) return NULL;

  posix_spawn_file_actions_t fa;
  if (posix_spawn_file_actions_init(&fa)) {
    close(fds[0]); close(fds[1]);
    return NULL;
  }
  int rc = posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
  if (!rc) rc = posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
  if (!rc) rc = posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
  extern char **environ;
  if (!rc) rc = posix_spawn(pid, argv[0], &fa, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(fds[1]);
  if (rc) {
    close(fds[0]);
    return NULL;
  }
  FILE *fp = fdopen(fds[0], "r");
  if (!fp) {
    close(fds[0]);
    (void)waitpid(*pid, NULL, 0);
  }
  return fp;
}

//
// End a process from spawn_reader(): true if it exited with status 0.
//
static bool spawn_close(FILE *fp, pid_t pid) {
  fclose(fp);
  int status;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

//
// Run argv without a shell, as run_command_r() runs a shell command.
// display is what logs and error replies show for it.
//
static bool run_argv_r(char *outbuf, char *escbuf, char *msgbuf, bool locked,
		       const char *display, char *const argv[],
		       LSMessage *message, subscribefun subscriber) {
  return run_process_r(outbuf, escbuf, msgbuf, locked, display, argv, message, subscriber);
}

static bool run_process_r(char *outbuf, char *escbuf, char *msgbuf, bool locked,
			  const char *command, char *const argv[],
			  LSMessage *message, subscribefun subscriber) {
  LSError lserror;
  LSErrorInit(&lserror);

  // Local buffers to store the current and previous lines.
  char line[MAXLINLEN];
  char lastline[MAXLINLEN];

  // outbuf is assumed to be initialised, ready for strcat to append.
  char *lastpos = outbuf+strlen(outbuf);

  // Is this the first line of output?
  bool first = true;
  bool lastfirst = true;

  // Has an early termination error been detected?
  bool error = false;

  bool array = false;

  fprintf(stderr, "Running command %s\n", command);

  // Start execution of the command, and read the output.
  pid_t pid = 0;
  FILE *fp = argv ? spawn_reader(argv, &pid) : popen(command, "r");

  // Return immediately if we cannot even start the command.
  if (!fp) {
    return false;
  }

  // Initialise the previous line contents.
  strcpy(lastline, "");

  // Loop through the output lines (a read error ends them too)
  while (!feof(fp) && !ferror(fp)) {

    // Initialise the current line contents.
    strcpy(line, "");
    int len = 0;

    // Read and store characters up to the next LF or NL, leaving room for
    // the terminating NUL: a longer line (curl prints the server's headers)
    // is split.
    int c;
    while ((len < MAXLINLEN - 1) && ((c = fgetc(fp)) != EOF)) {
      // Rewind the buffer for carriage returns without a linefeed.
      if (c == '\r') {
	*lastpos = '\0';
	first = lastfirst;
      }
      if ((c == '\r') || (c == '\n')) {
	line[len] = '\0';
	// Skip empty lines
	if (!len) continue;
	break;
      }
      line[len++] = c; line[len] = '\0';
    }

    // If we read something, then process it
    if (len) {

      lastpos = outbuf+strlen(outbuf);

      // Add formatting breaks between lines
      if (first) {
	if (outbuf[strlen(outbuf)-1] == '[') {
	  array = true;
	}
	first = false;
	lastfirst = true;
      }
      else {
	if (array) {
	  strcat(outbuf, ", ");
	}
	else {
	  strcat(outbuf, "<br>");
	}
	lastfirst = false;
      }

      // Have status updates been requested?
      if (message && subscriber) {

	// Is the current line different from the last line?
	if (strcmp(line, lastline)) {

	  // Copy into a new local buffer for possible modification
	  // cause we want the original to be return in outbuf.
	  char newline[MAXLINLEN];
	  strcpy(newline, line);

	  // Perform the filtering modificatons, and check for termination errors.
	  if (!subscriber(newline)) error = true;

	  // Is there anything left after filtering?
	  if (strlen(newline)) {

	    // Send it as a status message.
	    strcpy(msgbuf, "{\"returnValue\": true, \"stage\": \"status\", \"status\": \"");
	    strcat(msgbuf, json_escape_str_r(escbuf, MAXBUFLEN, newline));
	    strcat(msgbuf, "\"}");

	    // %%% Should we break out of the loop here, or just ignore the error? %%%
	    if (locked) {
	      if (!locked_respond(message, msgbuf, &lserror)) goto error;
	    }
	    else {
	      if (!LSMessageRespond(message, msgbuf, &lserror)) goto error;
	    }

	  }

	  // If a termination failure has been detected, then break out of the loop.
	  if (error) break;

	  // Store the current line as the previous line to check next time around.
	  strcpy(lastline, line);
	}
      }

      // Append the unfiltered output to the outbuf.
      if (array) {
	strcat(outbuf, "\"");
      }
      strcat(outbuf, json_escape_str_r(escbuf, MAXBUFLEN, line));
      if (array) {
	strcat(outbuf, "\"");
      }
    }
  }

  // Check the close status of the process, and return the combined error status
  if (argv ? !spawn_close(fp, pid) : pclose(fp) != 0) {
    return false;
  }
  if (error) {
    return false;
  }

  return true;
 error:
  if (argv) (void)spawn_close(fp, pid); else (void)pclose(fp);
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  // %%% We need a way to distinguish command failures from LSMessage failures %%%
  // %%% This may need to be true if we just want to ignore LSMessage failures %%%
  return false;
}

//
// Run a shell command using the process-wide static buffers.  Safe for
// everything that is called one operation at a time, which is everything
// except the feed downloads.  The global run_command_buffer must be
// initialised first.
//
static bool run_command(char *command, LSMessage *message, subscribefun subscriber) {
  return run_command_r(run_command_buffer, esc_buffer, buffer, false,
		       command, message, subscriber);
}

//
// Send a standard format command failure message back to webOS.
// The command will be escaped.  The output argument should be a JSON array and is not escaped.
// The additional text  will not be escaped.
// The return value is from the LSMessageRespond call, not related to the command execution.
//
static bool report_command_failure_r(char *msgbuf, char *escbuf, bool locked,
				     LSMessage *message, char *command, char *stdErrText, char *additional) {
  LSError lserror;
  LSErrorInit(&lserror);

  // Include the command that was executed, in escaped form.
  snprintf(msgbuf, MAXBUFLEN,
	   "{\"errorText\": \"Unable to run command: %s\"",
	   json_escape_str_r(escbuf, MAXBUFLEN, command));

  // Include any stderr fields from the command.
  if (stdErrText) {
    strcat(msgbuf, ", \"stdErr\": ");
    strcat(msgbuf, stdErrText);
  }

  // Report that an error occurred.
  strcat(msgbuf, ", \"returnValue\": false, \"errorCode\": -1");

  // Add any additional JSON fields.
  if (additional) {
    strcat(msgbuf, ", ");
    strcat(msgbuf, additional);
  }

  // Terminate the JSON reply message ...
  strcat(msgbuf, "}");

  // and send it.
  if (locked) {
    if (!locked_respond(message, msgbuf, &lserror)) goto error;
  }
  else {
    if (!LSMessageRespond(message, msgbuf, &lserror)) goto error;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

//
// Report a command failure using the process-wide static buffers.
//
static bool report_command_failure(LSMessage *message, char *command, char *stdErrText, char *additional) {
  return report_command_failure_r(buffer, esc_buffer, false,
				  message, command, stdErrText, additional);
}

//
// Refuse a download: reply with why and stage "failed".  Returns the
// LSMessageRespond result.
//
static bool refuse_fetch(LSMessage *message, bool locked, const char *why) {
  LSError lserror;
  LSErrorInit(&lserror);
  char reply[512];
  // why is one of our own messages, without characters JSON would escape.
  snprintf(reply, sizeof(reply),
	   "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"%s\", \"stage\": \"failed\"}",
	   why);
  fprintf(stderr, "%s\n", why);
  bool ok = locked ? locked_respond(message, reply, &lserror) : LSMessageRespond(message, reply, &lserror);
  if (!ok) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return ok;
}

typedef enum {
  CURL_OK,        // done, and the stage reply sent
  CURL_FAILED,    // curl failed, and the failure reported
  CURL_LS_ERROR   // a reply could not be sent
} curl_result;

//
// Run curl, without a shell, to check (FETCH_HEAD) or download
// (FETCH_DOWNLOAD, into output) url, which fetch_url_allowed() passed.
// Replies with stage "verify" or "download", or reports the failure, using
// the given buffers; locked as for run_command_r().
//
static curl_result run_curl(char *outbuf, char *escbuf, char *msgbuf, bool locked, LSMessage *message,
			    fetch_mode mode, const char *url, const char *output) {
  LSError lserror;
  LSErrorInit(&lserror);

  bool https = false;
  (void)fetch_url_parse(url, &https, NULL, 0);
  // The device id and token only go to the webOS-ports feeds, over https.
  bool auth = fetch_send_auth(url);

  const char *argv[40];
  fetch_headers hdr;
  char display[MAXLINLEN];
  if (!fetch_curl_argv(argv, sizeof(argv) / sizeof(argv[0]), &hdr, mode, url, output, https,
		       auth, device, token)) {
    return refuse_fetch(message, locked, "Refusing the download: invalid URL, file or credentials")
      ? CURL_FAILED : CURL_LS_ERROR;
  }
  fetch_argv_display(argv, display, sizeof(display));

  const char *stage = mode == FETCH_HEAD ? "verify" : "download";
  strcpy(outbuf, "{\"stdOut\": [");
  // posix_spawn() takes char *const[]; the strings are not changed.
  if (run_argv_r(outbuf, escbuf, msgbuf, locked, display, (char *const *)argv, message,
		 mode == FETCH_HEAD ? verifyheader : downloadstats)) {
    size_t used = strlen(outbuf);
    snprintf(outbuf + used, MAXBUFLEN - used, "], \"returnValue\": true, \"stage\": \"%s\"}", stage);
    bool ok = locked ? locked_respond(message, outbuf, &lserror) : LSMessageRespond(message, outbuf, &lserror);
    if (!ok) {
      LSErrorPrint(&lserror, stderr);
      LSErrorFree(&lserror);
      return CURL_LS_ERROR;
    }
    return CURL_OK;
  }
  strcat(outbuf, "]");
  return report_command_failure_r(msgbuf, escbuf, locked, message, display, outbuf + 11,
				  "\"stage\": \"failed\"") ? CURL_FAILED : CURL_LS_ERROR;
}

//
// Run a simple shell command, and return the output to webOS.
//
static bool simple_command(LSMessage *message, char *command) {
  LSError lserror;
  LSErrorInit(&lserror);

  // Initialise the output buffer
  strcpy(run_command_buffer, "{\"stdOut\": [");

  // Run the command
  if (run_command(command, NULL, NULL)) {

    // Finalise the message ...
    strcat(run_command_buffer, "], \"returnValue\": true}");

    // and send it to webOS.
    if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
  }
  else {

    // Finalise the command output ...
    strcat(run_command_buffer, "]");

    // and use it in a failure report message.
    if (!report_command_failure(message, command, run_command_buffer+11, NULL)) goto end;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Call the rescan service, and return the output to webOS.
//
bool rescan_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(message, "/usr/bin/luna-send -n 1 palm://com.palm.applicationManager/rescan {} 2>&1");
}

//
// Restart Luna, and return the output to webOS.
//
bool restart_luna_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(message, "/bin/systemctl restart surface-manager 2>&1");
}

//
// Restart Java, and return the output to webOS.
//
bool restart_java_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(message, "/usr/bin/killall java 2>&1");
}

//
// Restart the device, and return the output to webOS.
//
bool restart_device_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(message, "/sbin/reboot 2>&1");
}

//
// Get the machine name, and return the output to webOS.
//
bool get_machine_name_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(message, "/bin/cat /etc/prefs/properties/machineName 2>&1");
}

//
// Set the authentication parameters.
//
bool set_auth_params_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  // Extract the deviceId argument from the message
  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id = json_object_object_get(object, "deviceId");
  if (!id || !json_object_is_type(id, json_type_string) || (strspn(json_object_get_string(id), ALLOWED_CHARS":") != strlen(json_object_get_string(id)))) {
    if (!LSMessageRespond(message,
			  "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing deviceId\"}",
			  &lserror)) goto error;
    return true;
  }

  // g_strlcpy guarantees a terminated (if necessary, truncated) copy.
  g_strlcpy(device, json_object_get_string(id), MAXNAMLEN);

  // Extract the token argument from the message
  id = json_object_object_get(object, "token");
  if (!id || !json_object_is_type(id, json_type_string) || (strspn(json_object_get_string(id), ALLOWED_CHARS) != strlen(json_object_get_string(id)))) {
    if (!LSMessageRespond(message,
			  "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing token\"}",
			  &lserror)) goto error;
    return true;
  }

  // g_strlcpy guarantees a terminated (if necessary, truncated) copy.
  g_strlcpy(token, json_object_get_string(id), MAXNAMLEN);

  if (!LSMessageRespond(message, "{\"returnValue\": true}", &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

//
// Get the list of opkg configuration files, and return them and their contents.
//
//
// The package manager's directories under /media/cryptofs/apps, and an empty
// package status file (it has none until the first package is installed).
// The package creates them at image build time, but where /media/cryptofs is
// mounted from the data partition at boot (Halium devices, e.g. the BlackBerry
// KEY2) those are hidden: feed configs could not be listed or added, feeds not
// downloaded, and the package status not read. Creates only what is missing,
// so it is called by every method that needs them.
//
static void prepare_directories(void) {
  static const char *dirs[] = {
    "/media/cryptofs/apps/etc/opkg",
    "/media/cryptofs/apps/var/lib/opkg/cache",
    "/media/cryptofs/apps/var/lib/opkg/lists",
    NULL
  };
  for (int i = 0; dirs[i]; i++) {
    g_mkdir_with_parents(dirs[i], 0755);
  }
  int fd = open("/media/cryptofs/apps/var/lib/opkg/status", O_WRONLY | O_CREAT, 0644);
  if (fd >= 0) close(fd);
}

bool get_configs_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  prepare_directories();

  // Local buffer to store the config filename
  char filename[MAXNAMLEN];

  // Local buffer to build a command to read the config file contents
  char command[MAXLINLEN];

  // Is this the first line of output?
  bool first = true;

  // Was there an error in accessing any of the files?
  bool error = false;

  // Initialise the command to read the list of config files.
  strcpy(command, "/bin/ls -1 /media/cryptofs/apps/etc/opkg/ 2>&1");

  // Start execution of the command to list the config files.
  FILE *fp = popen(command, "r");

  // If the command cannot be started
  if (!fp) {

    // then report the error to webOS.
    if (!report_command_failure(message, command, NULL, NULL)) goto end;

    // The error report has been sent, so return to webOS.
    return true;
  }

  // Initialise the output message: always a configs array, empty when there
  // are no config files.
  strcpy(buffer, "{\"configs\": [");

  // Loop through the list of files in the config directory.
  while ( fgets( filename, sizeof filename, fp)) {

    // Chomp the newline
    char *nl = strchr(filename,'\n'); if (nl) *nl = 0;

    // Ignore the arch.conf file
    if (!strcmp(filename, "arch.conf")) continue;

    // Continue the JSON array
    if (!first) {
      strcat(buffer, ", ");
    }
    first = false;

    // Start the entry for each config file
    strcat(buffer, "{");
	
    // Determine if the config file is enabled or not
    char *p = strstr(filename,".disabled");

    // Store the config name, excluding any .disabled suffix
    if (p) *p = '\0';
    strcat(buffer, "\"config\": \"");
    strcat(buffer, filename);
    strcat(buffer, "\", ");
    if (p) *p = '.';

    // Store the enabled flag
    strcat(buffer, "\"enabled\": ");
    strcat(buffer, p ? "false" : "true");
    strcat(buffer, ", ");

    // Initialise the command to read the contents of the file.
    strcpy(command, "/bin/cat /media/cryptofs/apps/etc/opkg/");
    strcat(command, filename);
    strcat(command, " 2>&1");

    // Initialise the output buffer.
    strcpy(run_command_buffer, "");

    // Retrieve the file contents, and check for an error
    if (run_command(command, NULL, NULL)) {
      strcat(buffer, "\"contents\": \"");
    }
    else {
      strcat(buffer, "\"errorText\": \"");
      error = true;
    }

    // Store the command output (the contents of the file)
    strcat(buffer, run_command_buffer);

    // Terminate the JSON entry
    strcat(buffer, "\"}");
  }

  // Terminate the JSON array
  strcat(buffer, "], ");

  // Check the close status of the process, and return the combined error status
  if (pclose(fp) || error) {
    strcat(buffer, "\"returnValue\": false}");
  }
  else {
    strcat(buffer, "\"returnValue\": true}");
  }

  // Return the results to webOS.
  if (!LSMessageRespond(message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

void *update_thread(void *arg) {
  LSError lserror;
  LSErrorInit(&lserror);

  // Local buffer to store the update command
  char command[MAXLINLEN];

  // Capture any errors
  bool error = false;

  LSMessage *message = (LSMessage *)arg;

  // Store the command, so it can be used in the error report if necessary
  strcpy(command, "/usr/bin/opkg -o /media/cryptofs/apps update 2>&1");

  // Initialise the output buffer
  strcpy(run_command_buffer, "{\"stdOut\": [");

  // Run the update command
  if (!run_command(command, message, passthrough)) {

    // Terminate the output buffer
    strcat(run_command_buffer, "]");

    // Report the command failure.
    if (!report_command_failure(message, command, run_command_buffer+11, NULL)) goto end;

    // Remember that an error occurred, to be reported at the end
    error = true;

    // Even if there is an error, continue to move the files below
  }

  // Create the cache directory
  strcpy(command, "/bin/mkdir -p /media/cryptofs/apps/var/lib/opkg/cache 2>&1");
  strcpy(run_command_buffer, "[");
  if (!run_command(command, message, passthrough)) {
    strcat(run_command_buffer, "]");
    if (!report_command_failure(message, command, run_command_buffer, NULL)) goto end;
    error = true;
    // Even if there is an error, continue to move the files below
  }

  // Remove any existing cache files
  strcpy(command, "/bin/rm -f /media/cryptofs/apps/var/lib/opkg/cache/* 2>&1");
  strcpy(run_command_buffer, "[");
  if (!run_command(command, message, passthrough)) {
    strcat(run_command_buffer, "]");
    if (!report_command_failure(message, command, run_command_buffer, NULL)) goto end;
    error = true;
    // Even if there is an error, continue to move the files below
  }

  // Determine if any feeds were updated
  bool anyfeeds = false;
  DIR *dp = opendir ("/media/cryptofs/apps/var/lib/opkg/lists/");
  if (dp) {
    struct dirent *ep;
    while ((ep = readdir (dp)) != NULL) {
      if (strcmp(ep->d_name, ".") && strcmp(ep->d_name, "..")) {
	anyfeeds = true;
      }
    }
    (void)closedir(dp);
  }

  if (anyfeeds) {
    // Move package feed lists files over to the cache
    strcpy(command, "/bin/mv /media/cryptofs/apps/var/lib/opkg/lists/* /media/cryptofs/apps/var/lib/opkg/cache/ 2>&1");
    strcpy(run_command_buffer, "[");
    if (!run_command(command, message, passthrough)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(message, command, run_command_buffer, NULL)) goto end;
      error = true;
      // At this point, we're done anyway
    }
  }

  // Report the error status of the initial update command
  if (error) {
    if (!LSMessageRespond(message, "{\"returnValue\": false, \"stage\": \"failed\"}", &lserror)) goto error;
  }
  else {
    if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"completed\"}", &lserror)) goto error;
  }

 end:
  LSMessageUnref(message);
  return NULL;

 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  goto end;
}

//
// Run opkg update to download all enabled feeds.
// Note that the package lists are retrieved separately, this just does the download.
// The package lists are moved to a cache sibling directory to avoid any possible
// interaction with installations via the App Catalog and ApplicationInstallerUtility.
//
bool update_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  prepare_directories();

  pthread_t tid;

  LSMessageRef(message);

  // Report that the update operaton has begun
  if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"update\"}", &lserror)) goto error;

  pthread_create(&tid, NULL, update_thread, (void *)message);

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

static bool read_file(LSMessage *message, char *filename) {
  LSError lserror;
  LSErrorInit(&lserror);

  FILE * file = fopen(filename, "r");
  if (!file) {
    sprintf(read_file_buffer,
	    "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Cannot open %s\"}",
	    filename);

    if (!LSMessageRespond(message, read_file_buffer, &lserror)) goto error;
    return true;
  }

  char chunk[CHUNKSIZE+1];	// +1 so a full read can still be terminated
  int chunksize = CHUNKSIZE;

  fprintf(stderr, "Reading file %s\n", filename);

  fseek(file, 0, SEEK_END);
  int filesize = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (sprintf(read_file_buffer,
	      "{\"returnValue\": true, \"filesize\": %d, \"chunksize\": %d, \"stage\": \"start\"}",
	      filesize, chunksize)) {

    if (!LSMessageRespond(message, read_file_buffer, &lserror)) goto error;

  }

  int size;
  int datasize = 0;
  while ((size = fread(chunk, 1, chunksize, file)) > 0) {
    datasize += size;
    chunk[size] = '\0';
    sprintf(read_file_buffer, "{\"returnValue\": true, \"size\": %d, \"contents\": \"", size);
    strcat(read_file_buffer, json_escape_str_r(read_esc_buffer, ESCCHUNKSIZE, chunk));
    strcat(read_file_buffer, "\", \"stage\": \"middle\"}");

    if (!LSMessageRespond(message, read_file_buffer, &lserror)) goto error;

  }

  if (!fclose(file)) {
    sprintf(read_file_buffer, "{\"returnValue\": true, \"datasize\": %d, \"stage\": \"end\"}", datasize);

    if (!LSMessageRespond(message, read_file_buffer, &lserror)) goto error;

  }
  else {
    sprintf(read_file_buffer, "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Cannot close file\"}");

    if (!LSMessageRespond(message, read_file_buffer, &lserror)) goto error;

  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

bool get_list_file_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  char filename[MAXLINLEN];

  // Extract the feed argument from the message
  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id = json_object_object_get(object, "feed");
  if (!id || !json_object_is_type(id, json_type_string) || !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing feed\"}",
			&lserror)) goto error;
    return true;
  }

  strcpy(filename, "/media/cryptofs/apps/var/lib/opkg/cache/");
  strcat(filename, json_object_get_string(id));

  return read_file(message, filename);

 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

bool get_package_info_method(LSHandle *lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);
  GDir *dir;
  char *filename = NULL;
  const gchar *name = NULL;
  gchar **packages = NULL;
  gchar *package = NULL;
  gchar *feedname = NULL;
  gchar *contents = NULL;
  gsize length;
  gboolean ret;
  char chunk[CHUNKSIZE+1];	// +1 so a full read can still be terminated
  int chunksize = CHUNKSIZE;
  int size;
  int datasize = 0;

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id = json_object_object_get(object, "package");

  if (!id || !json_object_is_type(id, json_type_string) || !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing package\"}",
			&lserror)) goto error;
  }

  dir = g_dir_open("/media/cryptofs/apps/var/lib/opkg/cache", 0, NULL);

  if (!dir) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Error opening cache dir\"}",
			&lserror)) goto error;
  }

  while ((name = g_dir_read_name(dir)) != NULL) {
    int i = 0;
    if (asprintf(&filename, "/media/cryptofs/apps/var/lib/opkg/cache/%s", name) == -1) {
      filename = NULL;
      continue;
    }
    ret = g_file_get_contents(filename, &contents, &length, NULL);
    g_free(filename);
    filename = NULL;

    // Skip any feed list that cannot be read.
    if (!ret) continue;

    packages = g_strsplit(contents, "\nPackage: ", -1);
    while (packages[i]) {
      int len = strlen(json_object_get_string(id));
      int offset = (i == 0) ? strlen("Package: ") : 0;
      if (!bcmp(json_object_get_string(id), &packages[i][offset], len) &&
          (packages[i][offset + len] == '\n')) {
        package = packages[i];
	g_free(feedname);
	if (asprintf(&feedname, "%s", name) == -1) feedname = NULL;
      }
      i++;
    }

    g_free(contents);
  }

  g_dir_close(dir);

  if (!package) {
    if (!LSMessageRespond(message,
          "{\"returnValue\": true, \"size\": 0, \"contents\": \"\"}", &lserror)) {
      goto error;
    }
    g_free(feedname);
    g_strfreev(packages);
    return true;
  }

  if (sprintf(read_file_buffer,
	      "{\"returnValue\": true, \"feed\": \"%s\", \"filesize\": %zu, \"chunksize\": %d, \"stage\": \"start\"}",
	      feedname ? feedname : "", strlen(package)+10, chunksize)) {
    if (!LSMessageRespond(message, read_file_buffer, &lserror)) goto error;
  }

  while (package && datasize < strlen(package)) {
    size = MIN(strlen(&package[datasize]) + strlen("\nPackage: "), chunksize);
    bcopy(&package[datasize], chunk, size);
    chunk[size] = '\0';
    sprintf(read_file_buffer, "{\"returnValue\": true, \"size\": %d, \"contents\": \"", size);
    if (!datasize)
      strcat(read_file_buffer, "Package: ");
    strcat(read_file_buffer, json_escape_str_r(read_esc_buffer, ESCCHUNKSIZE, chunk));
    strcat(read_file_buffer, "\"");
    strcat(read_file_buffer, ", \"stage\": \"middle\"");
    strcat(read_file_buffer, "}");

    datasize += size;
    if (!LSMessageRespond(message, read_file_buffer, &lserror)) goto error;
  }

  sprintf(read_file_buffer, "{\"returnValue\": true, \"datasize\": %d, \"stage\": \"end\"}", datasize);
  if (!LSMessageRespond(message, read_file_buffer, &lserror)) goto error;

  g_free(feedname);

  g_strfreev(packages);
  return true;

 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  g_free(feedname);
  g_strfreev(packages);
  return false;
}

bool get_control_file_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  char filename[MAXLINLEN];

  // Extract the feed argument from the message
  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id = json_object_object_get(object, "package");
  if (!id || !json_object_is_type(id, json_type_string) || !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing package\"}",
			&lserror)) goto error;
  }

  strcpy(filename, "/media/cryptofs/apps/var/lib/opkg/info/");
  strcat(filename, json_object_get_string(id));
  strcat(filename, ".control");

  return read_file(message, filename);

 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

bool get_status_file_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  prepare_directories();

  return read_file(message, "/media/cryptofs/apps/var/lib/opkg/status");

  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

bool get_appinfo_file_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  char filename[MAXLINLEN];

  // Extract the feed argument from the message
  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id = json_object_object_get(object, "package");
  if (!id || !json_object_is_type(id, json_type_string) || !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing package\"}",
			&lserror)) goto error;
    return true;
  }

  strcpy(filename, "/media/cryptofs/apps/usr/palm/applications/");
  strcat(filename, json_object_get_string(id));
  strcat(filename, "/appinfo.json");

  return read_file(message, filename);

 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

//
// Get the listing of a directory, and return it's contents.
//
bool get_dir_listing_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  struct dirent *ep;

  // Is this the first line of output?
  bool first = true;

  // Was there an error in accessing any of the files?
  bool error = false;

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id = json_object_object_get(object, "directory");

  if (!id || !json_object_is_type(id, json_type_string)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing directory\"}",
			&lserror)) goto error;
    return true;
  }

  // This lists as root: only the user's storage and the installed
  // applications, and no symbolic link below them (fetch.h).
  static const char *const roots[] = FETCH_LISTING_ROOTS;
  char why[128];
  int dfd = fetch_open_listing_dir(json_object_get_string(id), roots, sizeof(roots) / sizeof(roots[0]),
				   why, sizeof(why));
  DIR *dp = dfd >= 0 ? fdopendir(dfd) : NULL;
  if (dfd >= 0 && !dp) close(dfd);

  // If the directory cannot be opened
  if (!dp) {
    snprintf(buffer, MAXBUFLEN,
	     "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"%s\"}",
	     dfd >= 0 ? "Unable to open directory" : why);
    if (!LSMessageRespond(message, buffer, &lserror)) goto error;

    // The error report has been sent, so return to webOS.
    return true;
  }

  // Initialise the output message.
  strcpy(buffer, "{");

  // Loop through the list of directory entries.
  while ((ep = readdir(dp)) != NULL) {

    // Stop before an entry could overrun the reply buffer: a name escapes
    // to at most six times its length, plus the entry's own text.
    if (strlen(buffer) + 6 * strlen(ep->d_name) + 64 > MAXBUFLEN) {
      error = true;
      break;
    }

    // Start or continue the JSON array
    if (first) {
      strcat(buffer, "\"contents\": [");
      first = false;
    }
    else {
      strcat(buffer, ", ");
    }

    strcat(buffer, "{\"name\":\"");
    strcat(buffer, json_escape_str(ep->d_name));
    strcat(buffer, "\", ");

    strcat(buffer, "\"type\":\"");
    if (ep->d_type == DT_DIR) {
      strcat(buffer, "directory");
    }
    else if (ep->d_type == DT_REG) {
      strcat(buffer, "file");
    }
    else if (ep->d_type == DT_LNK) {
      strcat(buffer, "symlink");
    }
    else {
      strcat(buffer, "other");
    }
    strcat(buffer, "\"}");
  }

  // Terminate the JSON array
  if (!first) {
    strcat(buffer, "], ");
  }

  // Check the close status of the process, and return the combined error status
  if (closedir(dp) || error) {
    strcat(buffer, "\"returnValue\": false}");
  }
  else {
    strcat(buffer, "\"returnValue\": true}");
  }

  // Return the results to webOS.
  if (!LSMessageRespond(message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

bool set_config_state_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id;

  // Extract the config argument from the message
  id = json_object_object_get(object, "config");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing config parameter\"}",
			&lserror)) goto error;
    return true;
  }
  char config[MAXNAMLEN];
  strcpy(config, json_object_get_string(id));

  // Extract the enabled argument from the message
  id = json_object_object_get(object, "enabled");
  if (!id || !json_object_is_type(id, json_type_boolean)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing enabled parameter\"}",
			&lserror)) goto error;
    return true;
  }
  bool enabled = json_object_get_boolean(id);

  char command[MAXLINLEN];
  if (enabled) {
    snprintf(command, MAXLINLEN,
	     "mv /media/cryptofs/apps/etc/opkg/%s.disabled /media/cryptofs/apps/etc/opkg/%s 2>&1",
	     config, config);
  }
  else {
    snprintf(command, MAXLINLEN,
	     "mv /media/cryptofs/apps/etc/opkg/%s /media/cryptofs/apps/etc/opkg/%s.disabled 2>&1",
	     config, config);
  }

  strcpy(run_command_buffer, "{\"stdOut\": [");
  if (run_command(command, NULL, NULL)) {
    strcat(run_command_buffer, "], \"returnValue\": true}");
    if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
  }
  else {
    strcat(run_command_buffer, "]");
    if (!report_command_failure(message, command, run_command_buffer+11, NULL)) goto end;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

bool add_config_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  prepare_directories();

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id;

  // Extract the config argument from the message
  id = json_object_object_get(object, "config");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing config parameter\"}",
			&lserror)) goto error;
    return true;
  }
  char config[MAXNAMLEN];
  strcpy(config, json_object_get_string(id));

  // Extract the name argument from the message
  id = json_object_object_get(object, "name");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing name parameter\"}",
			&lserror)) goto error;
    return true;
  }
  char name[MAXNAMLEN];
  strcpy(name, json_object_get_string(id));

  // Extract the url argument from the message
  id = json_object_object_get(object, "url");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing url parameter\"}",
			&lserror)) goto error;
    return true;
  }
  char url[MAXLINLEN];
  strcpy(url, json_object_get_string(id));

  // Extract the gzip argument from the message
  id = json_object_object_get(object, "gzip");
  if (!id || !json_object_is_type(id, json_type_boolean)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing gzip parameter\"}",
			&lserror)) goto error;
    return true;
  }
  bool gzip = json_object_get_boolean(id);

  // Written here, not through a shell; fetch_write_feed_conf() checks the
  // names and that the URL is a plain http(s) feed URL.
  char command[MAXLINLEN];
  snprintf(command, MAXLINLEN, "write %s/%s: %s %s %s", FETCH_FEED_CONF_DIR, config,
	   gzip ? "src/gz" : "src", name, url);
  char why[128];
  bool written = fetch_write_feed_conf(FETCH_FEED_CONF_DIR, config, gzip, name, url, why, sizeof(why));
  if (!written) fprintf(stderr, "addConfig: %s\n", why);

  strcpy(run_command_buffer, "{\"stdOut\": [");
  if (written) {
    strcat(run_command_buffer, "], \"returnValue\": true, \"stage\": \"completed\"}");
    if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
  }
  else {
    strcat(run_command_buffer, "]");
    if (!report_command_failure(message, command, run_command_buffer+11, NULL)) goto end;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

bool delete_config_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id;

  // Extract the config argument from the message
  id = json_object_object_get(object, "config");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing config parameter\"}",
			&lserror)) goto error;
    return true;
  }
  char config[MAXNAMLEN];
  strcpy(config, json_object_get_string(id));

  // Extract the name argument from the message
  id = json_object_object_get(object, "name");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing name parameter\"}",
			&lserror)) goto error;
    return true;
  }
  char name[MAXNAMLEN];
  strcpy(name, json_object_get_string(id));

  char command[MAXLINLEN];
  snprintf(command, MAXLINLEN,
	   "/bin/rm -f /media/cryptofs/apps/etc/opkg/%s /media/cryptofs/apps/etc/opkg/%s.disabled 2>&1",
	   config, config);

  strcpy(run_command_buffer, "{\"stdOut\": [");
  if (run_command(command, NULL, NULL)) {
    strcat(run_command_buffer, "], \"returnValue\": true, \"stage\": \"completed\"}");
    if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
  }
  else {
    strcat(run_command_buffer, "]");
    if (!report_command_failure(message, command, run_command_buffer+11, NULL)) goto end;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Download a single feed.  Runs in its own thread, possibly alongside other
// feeds Preware has in flight, so everything it touches has to be either
// local to this call or serialised.  It uses its own command_buffers rather
// than the process-wide statics, and replies under the respond mutex.
//
bool do_download(LSMessage *message, bool gzipped, const char *feed, const char *url) {
  bool returnValue = false;

  // The feed name becomes a file name in the cache, the URL a curl argument.
  if (!fetch_safe_name(feed)) {
    (void)refuse_fetch(message, true, "Refusing an invalid feed name");
    return false;
  }
  char why[256];
  if (!fetch_url_allowed(url, FETCH_FEED_CONF_DIR, why, sizeof(why))) {
    (void)refuse_fetch(message, true, why);
    return false;
  }

  char pathname[MAXNAMLEN + 64];
  snprintf(pathname, sizeof(pathname), "/media/cryptofs/apps/var/lib/opkg/cache/%s", feed);
  char packed[sizeof(pathname) + 16];
  snprintf(packed, sizeof(packed), "%s.gz-download", pathname);
  char index_url[MAXLINLEN + 16];
  if (snprintf(index_url, sizeof(index_url), "%s/%s", url, gzipped ? "Packages.gz" : "Packages")
      >= (int)sizeof(index_url)) {
    (void)refuse_fetch(message, true, "Refusing an over-long feed URL");
    return false;
  }

  prepare_directories();

  // Too big to sit on the thread stack, and needed for the whole call.
  command_buffers *bufs = malloc(sizeof(command_buffers));
  if (!bufs) {
    (void)refuse_fetch(message, true, "Out of memory");
    return false;
  }

  /* Verify access to the file */

  if (run_curl(bufs->out, bufs->esc, bufs->msg, true, message, FETCH_HEAD, index_url, NULL) != CURL_OK)
    goto end;

  /* Download the file */

  if (run_curl(bufs->out, bufs->esc, bufs->msg, true, message, FETCH_DOWNLOAD, index_url,
	       gzipped ? packed : pathname) != CURL_OK) {
    if (gzipped) unlink(packed);
    goto end;
  }

  if (gzipped) {
    char gzerr[128];
    bool unpacked = fetch_gunzip(packed, pathname, gzerr, sizeof(gzerr));
    unlink(packed);
    if (!unpacked) {
      (void)refuse_fetch(message, true, gzerr);
      goto end;
    }
  }

  returnValue = true;

 end:
  free(bufs);
  return returnValue;
}


void *feed_download_thread(void *arg) {
  LSError lserror;
  LSErrorInit(&lserror);

  LSMessage *message = (LSMessage *)arg;

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));

  // Extract the gzipped argument from the message
  json_object *gzipped = json_object_object_get(object, "gzipped");
  if (!gzipped || !json_object_is_type(gzipped, json_type_boolean)) {
    if (!locked_respond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing gzipped parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the feed argument from the message
  json_object *feed = json_object_object_get(object, "feed");
  if (!feed || !json_object_is_type(feed, json_type_string) ||
      (strlen(json_object_get_string(feed)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(feed))) {
    if (!locked_respond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing feed parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the url argument from the message
  json_object *url = json_object_object_get(object, "url");
  if (!url || !json_object_is_type(url, json_type_string) ||
      (strlen(json_object_get_string(url)) >= MAXLINLEN)) {
    if (!locked_respond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing url parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  if (do_download(message,
		  json_object_get_boolean(gzipped) ? true : false,
		  json_object_get_string(feed),
		  json_object_get_string(url))) {
    if (!locked_respond(message, "{\"returnValue\": true, \"stage\": \"completed\"}", &lserror)) goto error;
  }

 end:
  LSMessageUnref(message);
  return NULL;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  goto end;
}

bool feed_download_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  pthread_t tid;

  LSMessageRef(message);

  // Report that the update operaton has begun
  if (!locked_respond(message, "{\"returnValue\": true, \"stage\": \"begin\"}", &lserror)) goto error;

  pthread_create(&tid, NULL, feed_download_thread, (void *)message);

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

//
// Call appinstalld (install or remove) and follow the operation to its end.
//
// appinstalld answers a subscribed install or remove with a status message for
// every step (ipk parsing, app closing, installing / removing, ...) and ends with
// a final state: "installed" or "install failed" (about 17 messages for an
// install), "removed" or "remove failed" (about 15 for a remove). It keeps the
// subscription open afterwards, so luna-send never exits by itself; it is
// started without a shell, so that it can be ended once the operation is over.
// A package appinstalld does not know (e.g. installed by opkg, a PDK app) is
// refused at once with a single reply ("No such id"). The states are passed on
// as status messages, as before. run_command_buffer receives the output, like
// run_command().
//
#define APPINSTALLER_IDLE_TIMEOUT 300 // seconds without a message from appinstalld

typedef enum {
  APPINSTALLER_DONE,      // reached doneState
  APPINSTALLER_FAILED,    // reached failState, went quiet, or ended early
  APPINSTALLER_REFUSED    // refused outright (returnValue false before any state)
} appinstaller_result;

static appinstaller_result appinstaller_follow(LSMessage *message, const char *method,
					       const char *payload, const char *doneState,
					       const char *failState) {
  LSError lserror;
  LSErrorInit(&lserror);

  appinstaller_result result = APPINSTALLER_FAILED;
  bool finished = false, first = true, anyState = false;
  int fds[2];
  char uri[128];

  snprintf(uri, sizeof(uri), "luna://com.webos.appInstallService/%s", method);

  if (pipe(fds)) return APPINSTALLER_FAILED;

  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]); close(fds[1]);
    return APPINSTALLER_FAILED;
  }
  if (!pid) {
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]); close(fds[1]);
    execl("/usr/bin/luna-send", "luna-send", "-i", uri, payload, (char *)NULL);
    _exit(127);
  }
  close(fds[1]);

  FILE *fp = fdopen(fds[0], "r");
  // Unbuffered, so that select() below sees every line not yet handled: a
  // buffered fgets() can read several of appinstalld's messages at once,
  // leaving the rest (the final state among them) in the FILE buffer while
  // select() waits on the pipe for more, until the idle timeout.
  if (fp) setvbuf(fp, NULL, _IONBF, 0);
  char line[MAXLINLEN];

  while (fp && !finished) {
    // Give up if appinstalld goes quiet, rather than hang this thread.
    fd_set readable;
    struct timeval timeout = {APPINSTALLER_IDLE_TIMEOUT, 0};
    FD_ZERO(&readable);
    FD_SET(fds[0], &readable);
    if (select(fds[0] + 1, &readable, NULL, NULL, &timeout) <= 0) break;
    if (!fgets(line, sizeof(line), fp)) break;
    line[strcspn(line, "\r\n")] = '\0';
    if (!strlen(line)) continue;

    // Keep the raw output for the reply, as run_command() does.
    json_escape_str_r(esc_buffer, MAXBUFLEN, line);
    if (strlen(run_command_buffer) + strlen(esc_buffer) + 8 < MAXBUFLEN) {
      if (!first) strcat(run_command_buffer, ", ");
      first = false;
      strcat(run_command_buffer, "\"");
      strcat(run_command_buffer, esc_buffer);
      strcat(run_command_buffer, "\"");
    }

    json_object *reply = json_tokener_parse(line);
    json_object *details = reply ? json_object_object_get(reply, "details") : NULL;
    json_object *state = details ? json_object_object_get(details, "state") : NULL;
    json_object *ret = reply ? json_object_object_get(reply, "returnValue") : NULL;

    if (state && json_object_is_type(state, json_type_string)) {
      const char *st = json_object_get_string(state);
      anyState = true;
      snprintf(buffer, MAXBUFLEN, "{\"returnValue\": true, \"stage\": \"status\", \"status\": \"%s\"}",
	       json_escape_str_r(esc_buffer, MAXBUFLEN, (char *)st));
      if (!LSMessageRespond(message, buffer, &lserror)) {
	LSErrorPrint(&lserror, stderr);
	LSErrorFree(&lserror);
      }
      if (!strcmp(st, doneState)) {
	result = APPINSTALLER_DONE;
	finished = true;
      }
      else if (!strcmp(st, failState)) {
	finished = true;
      }
    }
    else if (ret && !json_object_get_boolean(ret)) {
      if (!anyState) result = APPINSTALLER_REFUSED;
      finished = true;
    }
    if (reply) json_object_put(reply);
  }

  kill(pid, SIGTERM);
  if (fp) fclose(fp); else close(fds[0]);
  waitpid(pid, NULL, 0);

  return result;
}

static bool appinstaller_install(LSMessage *message, const char *pkg, const char *pathname) {
  // The id and path come from the caller: let json-c do the quoting.
  json_object *params = json_object_new_object();
  json_object_object_add(params, "subscribe", json_object_new_boolean(1));
  json_object_object_add(params, "id", json_object_new_string(pkg));
  json_object_object_add(params, "ipkUrl", json_object_new_string(pathname));
  char payload[MAXLINLEN];
  g_strlcpy(payload, json_object_to_json_string(params), sizeof(payload));
  json_object_put(params);

  return appinstaller_follow(message, "install", payload, "installed", "install failed") == APPINSTALLER_DONE;
}

static appinstaller_result appinstaller_remove(LSMessage *message, const char *pkg) {
  json_object *params = json_object_new_object();
  json_object_object_add(params, "subscribe", json_object_new_boolean(1));
  json_object_object_add(params, "id", json_object_new_string(pkg));
  char payload[MAXLINLEN];
  g_strlcpy(payload, json_object_to_json_string(params), sizeof(payload));
  json_object_put(params);

  return appinstaller_follow(message, "remove", payload, "removed", "remove failed");
}

static bool do_install_package(LSMessage *message, const char *filename, const char *pkg, const char *url, bool useSvc);
static bool install_staged(LSMessage *message, const char *pathname, const char *pkg, bool useSvc);

//
// Install a package, then remove the file if it was downloaded for this: once
// installed (or failed) it is of no further use, and nothing else ever removes
// it. A package the caller already had in /media/internal/.developer, given
// as a file:// URL there, is installed in place and left alone.
//
bool do_install(LSMessage *message, const char *filename, const char *pkg, const char *url, bool useSvc) {
  bool result = do_install_package(message, filename, pkg, url, useSvc);

  // filename is only a file name of ours when it passed fetch_safe_name().
  if (fetch_safe_name(filename) && strncmp(url, "file:///media/internal/.developer/", 34)) {
    char pathname[MAXNAMLEN];
    snprintf(pathname, sizeof(pathname), "/media/internal/.developer/%s", filename);
    unlink(pathname);
  }

  return result;
}

//
// Where packages are copied before they are checked and installed: on the
// apps partition, so an app-sized package fits, and readable by root only.
//
#define STAGING_DIR "/media/cryptofs/apps/.ipkg-staging"

static bool do_install_package(LSMessage *message, const char *filename, const char *pkg, const char *url, bool useSvc) {
  LSError lserror;
  LSErrorInit(&lserror);

  char pathname[MAXNAMLEN + 64];

  if (!strncmp(url, "file://", 7)) {
    // Only a file directly in /media/internal/.developer.
    if (!fetch_developer_path(url, pathname, sizeof(pathname))) {
      if (!refuse_fetch(message, false, "Refusing a file URL outside /media/internal/.developer")) goto end;
      return true;
    }
  }
  else {
    char why[256];
    if (!fetch_safe_name(filename)) {
      if (!refuse_fetch(message, false, "Refusing an invalid package file name")) goto end;
      return true;
    }
    if (!fetch_url_allowed(url, FETCH_FEED_CONF_DIR, why, sizeof(why))) {
      if (!refuse_fetch(message, false, why)) goto end;
      return true;
    }
    snprintf(pathname, sizeof(pathname), "%s%s", FETCH_DEVELOPER_DIR, filename);

    /* Verify access to the file */

    curl_result r = run_curl(run_command_buffer, esc_buffer, buffer, false, message, FETCH_HEAD, url, NULL);
    if (r == CURL_LS_ERROR) goto end;
    if (r == CURL_FAILED) return false;

    /* Download the package */

    r = run_curl(run_command_buffer, esc_buffer, buffer, false, message, FETCH_DOWNLOAD, url, pathname);
    if (r == CURL_LS_ERROR) goto end;
    if (r == CURL_FAILED) return true;
  }

  /* Copy the package to where only root can change it */

  // /media/internal is writable by everyone: checking the file there and
  // then installing it would let it be swapped in between.
  char staged[MAXNAMLEN];
  char stage_error[256];
  if (!ipk_stage(pathname, STAGING_DIR, staged, sizeof(staged), stage_error, sizeof(stage_error))) {
    fprintf(stderr, "Cannot stage %s: %s\n", pathname, stage_error);
    snprintf(buffer, MAXBUFLEN,
	     "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"%s\", \"stage\": \"failed\"}",
	     json_escape_str(stage_error));
    if (!LSMessageRespond(message, buffer, &lserror)) goto error;
    return true;
  }

  bool result = install_staged(message, staged, pkg, useSvc);
  unlink(staged);
  return result;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Check the staged package at pathname, then install it.  Returns as
// do_install_package() does.
//
static bool install_staged(LSMessage *message, const char *pathname, const char *pkg, bool useSvc) {
  LSError lserror;
  LSErrorInit(&lserror);

  char command[MAXLINLEN];

  bool installed = false;

  /* Check the package and extract its id */

  // This service installs as root.  Refuse, before anything is unpacked, a
  // package with maintainer scripts or with files outside its own
  // application, package and service directories (ipk_check.h).
  char package[MAXNAMLEN];
  char check_error[512];
  ipk_result checked = ipk_check(pathname, package, sizeof(package), check_error, sizeof(check_error));
  if (checked == IPK_OK) {
    snprintf(buffer, MAXBUFLEN, "{\"stdOut\": [\"%s\"], \"returnValue\": true, \"stage\": \"identify\"}",
	     json_escape_str(package));
    if (!LSMessageRespond(message, buffer, &lserror)) goto error;
  }
  else {
    fprintf(stderr, "Refusing %s: %s\n", pathname, check_error);
    snprintf(buffer, MAXBUFLEN,
	     "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"%s\", \"stage\": \"failed\"}",
	     json_escape_str(check_error));
    if (!LSMessageRespond(message, buffer, &lserror)) goto error;
    return true;
  }

  /* Install the package */
  bool installOk;

  strcpy(run_command_buffer, "{\"stdOut\": [");
  if (useSvc) {
    snprintf(command, MAXLINLEN, "appInstallService/install %s", pathname);
    installOk = appinstaller_install(message, pkg, pathname);
  }
  else {
    snprintf(command, MAXLINLEN, "/usr/bin/opkg -o /media/cryptofs/apps -force-overwrite install %s 2>&1", pathname);
    installOk = run_command(command, message, passthrough);
  }
  if (installOk) {
    strcat(run_command_buffer, "], \"returnValue\": true, \"stage\": \"install\"}");
    installed = true;
    if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
  }
  else {
    strcat(run_command_buffer, "]");
    installed = false;
    if (!report_command_failure(message, command, run_command_buffer+11, "\"stage\": \"failed\"")) goto end;
    return true;
  }

  // ipk_check() refused any package with maintainer scripts, so there is no
  // postinst to run and no prerm to keep: nothing from the package runs as
  // root, and / is not remounted read-write.

  /* Revert any failed installation */

  if (!installed) {
    snprintf(command, MAXLINLEN,
	     "/usr/bin/opkg -o /media/cryptofs/apps remove %s 2>&1", package);

    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (run_command(command, message, appinstaller)) {
      strcat(run_command_buffer, "], \"returnValue\": true, \"stage\": \"remove\"}");
      if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
    }
    else {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(message, command, run_command_buffer+11, "\"stage\": \"remove\"")) goto end;
      // Ignore any error here.
    }

    snprintf(command, MAXLINLEN,
	     "/bin/rm -rf /media/cryptofs/apps/usr/palm/applications/%s /media/cryptofs/apps/.scripts/%s 2>&1", package, package);

    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (run_command(command, message, appinstaller)) {
      strcat(run_command_buffer, "], \"returnValue\": true, \"stage\": \"delete\"}");
      if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
    }
    else {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(message, command, run_command_buffer+11, "\"stage\": \"delete\"")) goto end;
      // Ignore any error here.
    }
  }

  /* Report the success or failure of the operation */

  if (installed) {
    if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"completed\"}", &lserror)) goto error;
  }
  else {
    if (!LSMessageRespond(message, "{\"returnValue\": false, \"stage\": \"failed\"}", &lserror)) goto error;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

bool do_remove(LSMessage *message, const char *package, bool replace, bool *removed) {
  LSError lserror;
  LSErrorInit(&lserror);

  *removed = false;

  char command[MAXLINLEN];

  struct stat info;

  // A package installed before maintainer scripts were refused may still
  // have a prerm.  It is not run: package-supplied shell does not run as root
  // here, and / is not remounted read-write for it.
  // Room for the longest script path around a MAXNAMLEN sized package name.
  char prerm[MAXNAMLEN+128];
  snprintf(prerm, sizeof(prerm), "/media/cryptofs/apps/var/lib/opkg/info/%s.prerm", package);

  if (!stat(prerm, &info)) {
    fprintf(stderr, "Not running the prerm script of %s\n", package);
  }

  char appinfo[MAXLINLEN];
  sprintf(appinfo, "/media/cryptofs/apps/usr/palm/applications/%s/appinfo.json", package);

  // An app goes through appinstalld, followed to its end (it sends about 15
  // messages; "luna-send -n 10" stopped listening before the remove was over,
  // and waited forever on a package appinstalld refuses with a single reply).
  // A package appinstalld does not know - installed by opkg, or an app type it
  // does not handle, such as PDK apps - is removed with opkg instead.
  bool useOpkg = true;
  if (!stat(appinfo, &info)) {
    strcpy(run_command_buffer, "{\"stdOut\": [");
    appinstaller_result result = appinstaller_remove(message, package);
    if (result == APPINSTALLER_DONE) {
      strcat(run_command_buffer, "], \"returnValue\": true, \"stage\": \"remove\"}");
      *removed = true;
      if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
      useOpkg = false;
    }
    else if (result == APPINSTALLER_FAILED) {
      strcat(run_command_buffer, "]");
      *removed = false;
      snprintf(command, MAXLINLEN, "appinstalld remove %s", package);
      if (!report_command_failure(message, command, run_command_buffer+11, "\"stage\": \"failed\"")) goto end;
      return true;
    }
  }

  if (useOpkg) {
    snprintf(command, MAXLINLEN,
	     "/usr/bin/opkg -o /media/cryptofs/apps remove %s 2>&1", package);

    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (run_command(command, message, appinstaller)) {
      strcat(run_command_buffer, "], \"returnValue\": true, \"stage\": \"remove\"}");
      *removed = true;
      if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
    }
    else {
      strcat(run_command_buffer, "]");
      *removed = false;
      if (!report_command_failure(message, command, run_command_buffer+11, "\"stage\": \"failed\"")) goto end;
      // %%% Should check for whether the application directory has been removed, or run opkg list_installed %%%
      // Assume that it hasn't been removed properly (don't change *removed)
      return true;
    }
  }

  snprintf(command, MAXLINLEN,
	   "/bin/rm -rf /media/cryptofs/apps/usr/palm/applications/%s /media/cryptofs/apps/.scripts/%s 2>&1", package, package);

  strcpy(run_command_buffer, "{\"stdOut\": [");
  if (run_command(command, message, appinstaller)) {
    strcat(run_command_buffer, "], \"returnValue\": true, \"stage\": \"delete\"}");
    if (!LSMessageRespond(message, run_command_buffer, &lserror)) goto error;
  }
  else {
    strcat(run_command_buffer, "]");
    if (!report_command_failure(message, command, run_command_buffer+11, "\"stage\": \"delete\"")) goto end;
  }

  if (replace) {
    if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"removed\"}", &lserror)) goto error;
  }
  else {
    if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"completed\"}", &lserror)) goto error;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

void *appinstaller_install_thread(void *arg) {
  LSError lserror;
  LSErrorInit(&lserror);

  LSMessage *message = (LSMessage *)arg;

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));

  // Extract the filename argument from the message
  json_object *filename = json_object_object_get(object, "filename");
  if (!filename || !json_object_is_type(filename, json_type_string) ||
      (strlen(json_object_get_string(filename)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(filename))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing filename parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the url argument from the message
  json_object *url = json_object_object_get(object, "url");
  if (!url || !json_object_is_type(url, json_type_string) ||
      (strlen(json_object_get_string(url)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing url parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the pkg argument from the message
  json_object *pkg = json_object_object_get(object, "pkg");
  if (!pkg || !json_object_is_type(pkg, json_type_string) ||
      (strlen(json_object_get_string(pkg)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing pkg parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  do_install(message, json_object_get_string(filename), json_object_get_string(pkg), json_object_get_string(url), true);

 end:
  LSMessageUnref(message);
  return NULL;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  goto end;
}

bool appinstaller_install_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  pthread_t tid;

  LSMessageRef(message);

  // Report that the update operaton has begun
  if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"begin\"}", &lserror)) goto error;

  pthread_create(&tid, NULL, appinstaller_install_thread, (void *)message);

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

void *opkg_install_thread(void *arg) {
  LSError lserror;
  LSErrorInit(&lserror);

  LSMessage *message = (LSMessage *)arg;

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));

  // Extract the filename argument from the message
  json_object *filename = json_object_object_get(object, "filename");
  if (!filename || !json_object_is_type(filename, json_type_string) ||
      (strlen(json_object_get_string(filename)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(filename))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing filename parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the url argument from the message
  json_object *url = json_object_object_get(object, "url");
  if (!url || !json_object_is_type(url, json_type_string) ||
      (strlen(json_object_get_string(url)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing url parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }
  
  // Extract the pkg argument from the message
  json_object *pkg = json_object_object_get(object, "pkg");
  if (!pkg || !json_object_is_type(pkg, json_type_string) ||
      (strlen(json_object_get_string(pkg)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing pkg parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }
  

  do_install(message, json_object_get_string(filename), json_object_get_string(pkg), json_object_get_string(url), false);

 end:
  LSMessageUnref(message);
  return NULL;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  goto end;
}

bool opkg_install_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  pthread_t tid;

  LSMessageRef(message);

  // Report that the update operaton has begun
  if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"begin\"}", &lserror)) goto error;

  pthread_create(&tid, NULL, opkg_install_thread, (void *)message);

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

void *remove_thread(void *arg) {
  LSError lserror;
  LSErrorInit(&lserror);

  LSMessage *message = (LSMessage *)arg;

  // Extract the pkg argument from the message
  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id = json_object_object_get(object, "pkg");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing pkg parameter\"}",
			&lserror)) goto error;
    goto end;
  }

  bool removed = false;
  do_remove(message, json_object_get_string(id), false, &removed);

 end:
  LSMessageUnref(message);
  return NULL;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  goto end;
}

bool remove_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  pthread_t tid;

  LSMessageRef(message);

  // Report that the remove operaton has begun
  if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"begin\"}", &lserror)) goto error;

  pthread_create(&tid, NULL, remove_thread, (void *)message);

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

void *opkg_replace_thread(void *arg) {
  LSError lserror;
  LSErrorInit(&lserror);

  LSMessage *message = (LSMessage *)arg;

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));

  // Extract the pkg argument from the message
  json_object *pkg = json_object_object_get(object, "pkg");
  if (!pkg || !json_object_is_type(pkg, json_type_string) ||
      (strlen(json_object_get_string(pkg)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(pkg))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing pkg parameter\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the filename argument from the message
  json_object *filename = json_object_object_get(object, "filename");
  if (!filename || !json_object_is_type(filename, json_type_string) ||
      (strlen(json_object_get_string(filename)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(filename))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing filename parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the url argument from the message
  json_object *url = json_object_object_get(object, "url");
  if (!url || !json_object_is_type(url, json_type_string) ||
      (strlen(json_object_get_string(url)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing url parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  bool removed = false;
  if (!do_remove(message, json_object_get_string(pkg), true, &removed)) goto end;
  if (removed) {
    if (!do_install(message, json_object_get_string(filename), json_object_get_string(pkg), json_object_get_string(url), false)) goto end;
  }

 end:
  LSMessageUnref(message);
  return NULL;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  goto end;
}

bool opkg_replace_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  pthread_t tid;

  LSMessageRef(message);

  // Report that the update operaton has begun
  if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"begin\"}", &lserror)) goto error;

  pthread_create(&tid, NULL, opkg_replace_thread, (void *)message);

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

void *appinstaller_replace_thread(void *arg) {
  LSError lserror;
  LSErrorInit(&lserror);

  LSMessage *message = (LSMessage *)arg;

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));

  // Extract the filename argument from the message
  json_object *filename = json_object_object_get(object, "filename");
  if (!filename || !json_object_is_type(filename, json_type_string) ||
      (strlen(json_object_get_string(filename)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(filename))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing filename parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the url argument from the message
  json_object *url = json_object_object_get(object, "url");
  if (!url || !json_object_is_type(url, json_type_string) ||
      (strlen(json_object_get_string(url)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing url parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  // Extract the pkg argument from the message
  json_object *pkg = json_object_object_get(object, "pkg");
  if (!pkg || !json_object_is_type(pkg, json_type_string) ||
      (strlen(json_object_get_string(pkg)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing pkg parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    goto end;
  }

  bool removed = false;
  if (!do_remove(message, json_object_get_string(pkg), true, &removed)) goto end;
  if (removed) {
    if (!do_install(message, json_object_get_string(filename), json_object_get_string(pkg), json_object_get_string(url), true)) goto end;
  }

 end:
  LSMessageUnref(message);
  return NULL;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  goto end;
}

bool appinstaller_replace_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  pthread_t tid;

  LSMessageRef(message);

  // Report that the update operaton has begun
  if (!LSMessageRespond(message, "{\"returnValue\": true, \"stage\": \"begin\"}", &lserror)) goto error;

  pthread_create(&tid, NULL, appinstaller_replace_thread, (void *)message);

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

bool extract_control_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  char command[MAXLINLEN];

  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id;

  // Extract the filename argument from the message
  id = json_object_object_get(object, "filename");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXNAMLEN) ||
      !fetch_safe_name(json_object_get_string(id))) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing filename parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    return true;
  }
  char filename[MAXNAMLEN + 64];
  const char *name = json_object_get_string(id);

  // Extract the url argument from the message
  id = json_object_object_get(object, "url");
  if (!id || !json_object_is_type(id, json_type_string) ||
      (strlen(json_object_get_string(id)) >= MAXLINLEN)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, "
			"\"errorText\": \"Invalid or missing url parameter\", "
			"\"stage\": \"failed\"}",
			&lserror)) goto error;
    return true;
  }
  const char *url = json_object_get_string(id);

  if (!strncmp(url, "file://", 7)) {
    // Only a file directly in /media/internal/.developer.
    if (!fetch_developer_path(url, filename, sizeof(filename))) {
      if (!refuse_fetch(message, false, "Refusing a file URL outside /media/internal/.developer")) goto end;
      return true;
    }
  }
  else {
    char why[256];
    if (!fetch_safe_name(name)) {
      if (!refuse_fetch(message, false, "Refusing an invalid package file name")) goto end;
      return true;
    }
    if (!fetch_url_allowed(url, FETCH_FEED_CONF_DIR, why, sizeof(why))) {
      if (!refuse_fetch(message, false, why)) goto end;
      return true;
    }
    snprintf(filename, sizeof(filename), "%s%s", FETCH_DEVELOPER_DIR, name);

    /* Verify access to the file */

    curl_result r = run_curl(run_command_buffer, esc_buffer, buffer, false, message, FETCH_HEAD, url, NULL);
    if (r == CURL_LS_ERROR) goto end;
    if (r == CURL_FAILED) return false;

    /* Download the package */

    r = run_curl(run_command_buffer, esc_buffer, buffer, false, message, FETCH_DOWNLOAD, url, filename);
    if (r == CURL_LS_ERROR) goto end;
    if (r == CURL_FAILED) return true;
  }

  /* Extract the control file */

  // The file name goes to the shell as an argument, never into the script.
  char *const argv[] = {
    "/bin/sh", "-c",
    "/usr/bin/ar p \"$1\" control.tar.gz | /bin/tar -O -z -x --no-anchored -f - control 2>&1",
    "sh", filename, NULL
  };
  snprintf(command, MAXLINLEN, "/usr/bin/ar p %s control.tar.gz | /bin/tar -O -z -x --no-anchored -f - control", filename);
  // Initialise the output buffer
  strcpy(run_command_buffer, "{\"stdOut\": [");
  if (run_argv_r(run_command_buffer, esc_buffer, buffer, false, command, argv, NULL, NULL)) {
    sprintf(buffer, "{\"filename\":\"%s\", \"info\": ", filename);
    strcat(buffer, run_command_buffer+11);
    strcat(buffer, "], \"returnValue\": true, \"stage\": \"completed\"}");
    if (!LSMessageRespond(message, buffer, &lserror)) goto error;
  }
  else {
    // Finalise the command output ...
    strcat(run_command_buffer, "]");

    // and use it in a failure report message.
    if (!report_command_failure(message, command, run_command_buffer+11, "\"stage\": \"failed\"")) goto end;
    return true;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Handler for the impersonate service.
//
bool impersonate_handler(LSHandle* lshandle, LSMessage *reply, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessage* message = (LSMessage*)ctx;
  retVal = LSMessageRespond(message, LSMessageGetPayload(reply), &lserror);
  if (!LSMessageIsSubscription(message)) {
    LSMessageUnref(message);
  }
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Impersonate a call to the requested service and return the output to webOS.
//
bool impersonate_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessageRef(message);

  if (access_denied(message)) return true;

  // Extract the method argument from the message
  json_object *object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *id = json_object_object_get(object, "id");
  if (!id || !json_object_is_type(id, json_type_string)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing id\"}",
			&lserror)) goto error;
    return true;
  }

  // Extract the service argument from the message
  object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *service = json_object_object_get(object, "service");
  if (!service || !json_object_is_type(service, json_type_string)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing service\"}",
			&lserror)) goto error;
    return true;
  }

  // Extract the method argument from the message
  object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *method = json_object_object_get(object, "method");
  if (!method || !json_object_is_type(method, json_type_string)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing method\"}",
			&lserror)) goto error;
    return true;
  }

  // Extract the params argument from the message
  object = json_tokener_parse(LSMessageGetPayload(message));
  json_object *params = json_object_object_get(object, "params");
  if (!params || !json_object_is_type(params, json_type_object)) {
    if (!LSMessageRespond(message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing params\"}",
			&lserror)) goto error;
    return true;
  }

  char uri[MAXLINLEN];
  sprintf(uri, "luna://%s/%s", json_object_get_string(service), json_object_get_string(method));

  const char *paramstring = NULL;
  paramstring = json_object_to_json_string(params);
  if (!LSCallFromApplication(serviceHandle, uri, paramstring, json_object_get_string(id),
			     impersonate_handler, message, NULL, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
  return false;
}

//
// Handler for the listApps service.
//
bool listApps_handler(LSHandle* lshandle, LSMessage *reply, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessage* message = (LSMessage*)ctx;
  retVal = LSMessageRespond(message, LSMessageGetPayload(reply), &lserror);
  LSMessageUnref(message);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Call the listApps service using liblunaservice and return the output to webOS.
//
bool listApps_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessageRef(message);
  retVal = LSCall(serviceHandle, "luna://com.palm.applicationManager/listApps", "{}",
		  listApps_handler, message, NULL, &lserror);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Handler for the installStatus service.
//
bool installStatus_handler(LSHandle* lshandle, LSMessage *reply, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessage* message = (LSMessage*)ctx;
  retVal = LSMessageRespond(message, LSMessageGetPayload(reply), &lserror);
  LSMessageUnref(message);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Call the installStatus service using liblunaservice and return the output to webOS.
//
bool installStatus_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessageRef(message);
  retVal = LSCall(serviceHandle, "luna://com.webos.appInstallService/status", "{}",
		  installStatus_handler, message, NULL, &lserror);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Handler for the addResourceHandler service.
//
bool addResource_handler(LSHandle* lshandle, LSMessage *reply, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessage* message = (LSMessage*)ctx;
  retVal = LSMessageRespond(message, LSMessageGetPayload(reply), &lserror);
  LSMessageUnref(message);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Call the addResourceHandler service using liblunaservice and return the output to webOS.
//
bool addResource_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessageRef(message);
  const char *payload;
  payload = LSMessageGetPayload(message);
  retVal = LSCall(serviceHandle, "luna://com.palm.applicationManager/addResourceHandler",
		  payload, addResource_handler, message, NULL, &lserror);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Handler for the swapResourceHandler service.
//
bool swapResource_handler(LSHandle* lshandle, LSMessage *reply, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessage* message = (LSMessage*)ctx;
  retVal = LSMessageRespond(message, LSMessageGetPayload(reply), &lserror);
  LSMessageUnref(message);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Call the swapResourceHandler service using liblunaservice and return the output to webOS.
//
bool swapResource_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessageRef(message);
  const char *payload;
  payload = LSMessageGetPayload(message);
  retVal = LSCall(serviceHandle, "luna://com.palm.applicationManager/swapResourceHandler",
		  payload, swapResource_handler, message, NULL, &lserror);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

LSMethod luna_methods[] = {
  { "status",		dummy_method },
  { "version",		version_method },
  { "restart",		restart_method },

  { "update",		update_method },

  { "rescan",		rescan_method },
  { "restartLuna",	restart_luna_method },
  { "restartJava",	restart_java_method },
  { "restartDevice",	restart_device_method },

  { "getMachineName",	get_machine_name_method },
  { "setAuthParams",	set_auth_params_method },

  { "getConfigs",	get_configs_method },

  { "downloadFeed",	feed_download_method },

  { "getListFile",	get_list_file_method },
  { "getControlFile",	get_control_file_method },
  { "getStatusFile",	get_status_file_method },
  { "getAppinfoFile",	get_appinfo_file_method },
  { "getPackageInfo",	get_package_info_method },

  { "getDirListing",	get_dir_listing_method },

  { "setConfigState",	set_config_state_method },
  { "addConfig",	add_config_method },
  { "deleteConfig",	delete_config_method },

  { "extractControl",	extract_control_method },

  { "install",		opkg_install_method },
  { "installSvc",	appinstaller_install_method },
  { "installCli",	opkg_install_method },

  { "replace",		opkg_replace_method },
  { "replaceSvc",	appinstaller_replace_method },
  { "replaceCli",	opkg_replace_method },

  { "remove",		remove_method },

  { "impersonate",	impersonate_method },

  { "listApps",		listApps_method },
  { "installStatus",	installStatus_method },

  { "addResource",	addResource_method },
  { "swapResource",	swapResource_method },

  { 0, 0 }
};

bool register_methods(LSHandle *serviceHandle, LSError lserror) {
  strcpy(device, "webOS"); strcpy(token, "konami");
  return LSRegisterCategory(serviceHandle, "/", luna_methods,
				       NULL, NULL, &lserror);
}
