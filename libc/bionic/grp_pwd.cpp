/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <mntent.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <packagelistparser/packagelistparser.h>
#include "private/android_filesystem_config.h"
#include "private/bionic_macros.h"
#include "private/ErrnoRestorer.h"
#include "private/libc_logging.h"
#include "private/ThreadLocalBuffer.h"

// POSIX seems to envisage an implementation where the <pwd.h> functions are
// implemented by brute-force searching with getpwent(3), and the <grp.h>
// functions are implemented similarly with getgrent(3). This means that it's
// okay for all the <grp.h> functions to share state, and all the <passwd.h>
// functions to share state, but <grp.h> functions can't clobber <passwd.h>
// functions' state and vice versa.

struct packagelistparser_state_t {
  packagelistparser_state_t *next;
  id_t id;
  size_t home_idx;
  char name[];
};

#define LOGIN_NAME_MAX 64

struct group_state_t {
  group group_;
  char* group_members_[2];
  char group_name_buffer_[LOGIN_NAME_MAX];
  // Must be last so init_group_state can run a simple memset for the above
  ssize_t getgrent_idx;
  packagelistparser_state_t *head;
};

struct passwd_state_t {
  passwd passwd_;
  // e.g. com.google.android.printservice.recommendation
  char name_buffer_[LOGIN_NAME_MAX];
  // e.g. /data/user/0/com.google.android.printservice.recommendation
  char dir_buffer_[LOGIN_NAME_MAX];
  char sh_buffer_[32];
  ssize_t getpwent_idx;
  packagelistparser_state_t *head;
};

static ThreadLocalBuffer<group_state_t> g_group_tls_buffer;
static ThreadLocalBuffer<passwd_state_t> g_passwd_tls_buffer;

static void init_group_state(group_state_t* state) {
  memset(state, 0, sizeof(group_state_t) - sizeof(state->getgrent_idx) - sizeof(state->head));
  state->group_.gr_mem = state->group_members_;
}

static group_state_t* __group_state() {
  group_state_t* result = g_group_tls_buffer.get();
  if (result != nullptr) {
    init_group_state(result);
  }
  return result;
}

static int do_getpw_r(int by_name, const char* name, uid_t uid,
                      passwd* dst, char* buf, size_t byte_count,
                      passwd** result) {
  // getpwnam_r and getpwuid_r don't modify errno, but library calls we
  // make might.
  ErrnoRestorer errno_restorer;
  *result = NULL;

  // Our implementation of getpwnam(3) and getpwuid(3) use thread-local
  // storage, so we can call them as long as we copy everything out
  // before returning.
  const passwd* src = by_name ? getpwnam(name) : getpwuid(uid); // NOLINT: see above.

  // POSIX allows failure to find a match to be considered a non-error.
  // Reporting success (0) but with *result NULL is glibc's behavior.
  if (src == NULL) {
    return (errno == ENOENT) ? 0 : errno;
  }

  // Work out where our strings will go in 'buf', and whether we've got
  // enough space.
  size_t required_byte_count = 0;
  dst->pw_name = buf;
  required_byte_count += strlen(src->pw_name) + 1;
  dst->pw_dir = buf + required_byte_count;
  required_byte_count += strlen(src->pw_dir) + 1;
  dst->pw_shell = buf + required_byte_count;
  required_byte_count += strlen(src->pw_shell) + 1;
  if (byte_count < required_byte_count) {
    return ERANGE;
  }

  // Copy the strings.
  snprintf(buf, byte_count, "%s%c%s%c%s", src->pw_name, 0, src->pw_dir, 0, src->pw_shell);

  // pw_passwd and pw_gecos are non-POSIX and unused (always NULL) in bionic.
  // Note: On LP32, we define pw_gecos to pw_passwd since they're both NULL.
  dst->pw_passwd = NULL;
#if defined(__LP64__)
  dst->pw_gecos = NULL;
#endif

  // Copy the integral fields.
  dst->pw_gid = src->pw_gid;
  dst->pw_uid = src->pw_uid;

  *result = dst;
  return 0;
}

int getpwnam_r(const char* name, passwd* pwd,
               char* buf, size_t byte_count, passwd** result) {
  return do_getpw_r(1, name, -1, pwd, buf, byte_count, result);
}

int getpwuid_r(uid_t uid, passwd* pwd,
               char* buf, size_t byte_count, passwd** result) {
  return do_getpw_r(0, NULL, uid, pwd, buf, byte_count, result);
}

static passwd* android_iinfo_to_passwd(passwd_state_t* state,
                                       const android_id_info* iinfo) {
  snprintf(state->name_buffer_, sizeof(state->name_buffer_), "%s", iinfo->name);
  snprintf(state->dir_buffer_, sizeof(state->dir_buffer_), "/");
  snprintf(state->sh_buffer_, sizeof(state->sh_buffer_), "/system/bin/sh");

  passwd* pw = &state->passwd_;
  pw->pw_name  = state->name_buffer_;
  pw->pw_uid   = iinfo->aid;
  pw->pw_gid   = iinfo->aid;
  pw->pw_dir   = state->dir_buffer_;
  pw->pw_shell = state->sh_buffer_;
  return pw;
}

static group* android_iinfo_to_group(group_state_t* state,
                                     const android_id_info* iinfo) {
  snprintf(state->group_name_buffer_, sizeof(state->group_name_buffer_), "%s", iinfo->name);

  group* gr = &state->group_;
  gr->gr_name   = state->group_name_buffer_;
  gr->gr_gid    = iinfo->aid;
  gr->gr_mem[0] = gr->gr_name;
  return gr;
}

static passwd* android_id_to_passwd(passwd_state_t* state, unsigned id) {
  for (size_t n = 0; n < android_id_count; ++n) {
    if (android_ids[n].aid == id) {
      return android_iinfo_to_passwd(state, android_ids + n);
    }
  }
  return NULL;
}

static passwd* android_name_to_passwd(passwd_state_t* state, const char* name) {
  for (size_t n = 0; n < android_id_count; ++n) {
    if (!strcmp(android_ids[n].name, name)) {
      return android_iinfo_to_passwd(state, android_ids + n);
    }
  }
  return NULL;
}

static group* android_id_to_group(group_state_t* state, unsigned id) {
  for (size_t n = 0; n < android_id_count; ++n) {
    if (android_ids[n].aid == id) {
      return android_iinfo_to_group(state, android_ids + n);
    }
  }
  return NULL;
}

static group* android_name_to_group(group_state_t* state, const char* name) {
  for (size_t n = 0; n < android_id_count; ++n) {
    if (!strcmp(android_ids[n].name, name)) {
      return android_iinfo_to_group(state, android_ids + n);
    }
  }
  return NULL;
}

// Translate a user/group name to the corresponding user/group id.
// all_a1234 -> 0 * AID_USER + AID_SHARED_GID_START + 1234 (group name only)
// u0_a1234 -> 0 * AID_USER + AID_APP + 1234
// u2_i1000 -> 2 * AID_USER + AID_ISOLATED_START + 1000
// u1_system -> 1 * AID_USER + android_ids['system']
// returns 0 and sets errno to ENOENT in case of error.
static id_t app_id_from_name(const char* name, bool is_group) {
  char* end;
  unsigned long userid;
  bool is_shared_gid = false;

  if (is_group && name[0] == 'a' && name[1] == 'l' && name[2] == 'l') {
    end = const_cast<char*>(name+3);
    userid = 0;
    is_shared_gid = true;
  } else if (name[0] == 'u' && isdigit(name[1])) {
    userid = strtoul(name+1, &end, 10);
  } else {
    errno = ENOENT;
    return 0;
  }

  if (end[0] != '_' || end[1] == 0) {
    errno = ENOENT;
    return 0;
  }

  unsigned long appid = 0;
  if (end[1] == 'a' && isdigit(end[2])) {
    if (is_shared_gid) {
      // end will point to \0 if the strtoul below succeeds.
      appid = strtoul(end+2, &end, 10) + AID_SHARED_GID_START;
      if (appid > AID_SHARED_GID_END) {
        errno = ENOENT;
        return 0;
      }
    } else {
      // end will point to \0 if the strtoul below succeeds.
      appid = strtoul(end+2, &end, 10) + AID_APP;
    }
  } else if (end[1] == 'i' && isdigit(end[2])) {
    // end will point to \0 if the strtoul below succeeds.
    appid = strtoul(end+2, &end, 10) + AID_ISOLATED_START;
  } else {
    for (size_t n = 0; n < android_id_count; n++) {
      if (!strcmp(android_ids[n].name, end + 1)) {
        appid = android_ids[n].aid;
        // Move the end pointer to the null terminator.
        end += strlen(android_ids[n].name) + 1;
        break;
      }
    }
  }

  // Check that the entire string was consumed by one of the 3 cases above.
  if (end[0] != 0) {
    errno = ENOENT;
    return 0;
  }

  // Check that user id won't overflow.
  if (userid > 1000) {
    errno = ENOENT;
    return 0;
  }

  // Check that app id is within range.
  if (appid >= AID_USER) {
    errno = ENOENT;
    return 0;
  }

  return (appid + userid*AID_USER);
}

static void print_app_name_from_uid(const uid_t uid, char* buffer, const int bufferlen) {
  const uid_t appid = uid % AID_USER;
  const uid_t userid = uid / AID_USER;
  if (appid >= AID_ISOLATED_START) {
    snprintf(buffer, bufferlen, "u%u_i%u", userid, appid - AID_ISOLATED_START);
  } else if (appid < AID_APP) {
    for (size_t n = 0; n < android_id_count; n++) {
      if (android_ids[n].aid == appid) {
        snprintf(buffer, bufferlen, "u%u_%s", userid, android_ids[n].name);
        return;
      }
    }
  } else {
    snprintf(buffer, bufferlen, "u%u_a%u", userid, appid - AID_APP);
  }
}

static void print_app_name_from_gid(const gid_t gid, char* buffer, const int bufferlen) {
  const uid_t appid = gid % AID_USER;
  const uid_t userid = gid / AID_USER;
  if (appid >= AID_ISOLATED_START) {
    snprintf(buffer, bufferlen, "u%u_i%u", userid, appid - AID_ISOLATED_START);
  } else if (userid == 0 && appid >= AID_SHARED_GID_START && appid <= AID_SHARED_GID_END) {
    snprintf(buffer, bufferlen, "all_a%u", appid - AID_SHARED_GID_START);
  } else if (appid < AID_APP) {
    for (size_t n = 0; n < android_id_count; n++) {
      if (android_ids[n].aid == appid) {
        snprintf(buffer, bufferlen, "u%u_%s", userid, android_ids[n].name);
        return;
      }
    }
  } else {
    snprintf(buffer, bufferlen, "u%u_a%u", userid, appid - AID_APP);
  }
}

// oem_XXXX -> uid
//  Supported ranges:
//   AID_OEM_RESERVED_START to AID_OEM_RESERVED_END (2900-2999)
//   AID_OEM_RESERVED_2_START to AID_OEM_RESERVED_2_END (5000-5999)
// Check OEM id is within range.
static bool is_oem_id(id_t id) {
  return (((id >= AID_OEM_RESERVED_START) && (id <= AID_OEM_RESERVED_END)) ||
      ((id >= AID_OEM_RESERVED_2_START) && (id <= AID_OEM_RESERVED_2_END)));
}

// Translate an OEM name to the corresponding user/group id.
static id_t oem_id_from_name(const char* name) {
  unsigned int id;
  if (sscanf(name, "oem_%u", &id) != 1) {
    return 0;
  }
  if (!is_oem_id(id)) {
    return 0;
  }
  return static_cast<id_t>(id);
}

static passwd* oem_id_to_passwd(uid_t uid, passwd_state_t* state) {
  if (!is_oem_id(uid)) {
    return NULL;
  }

  snprintf(state->name_buffer_, sizeof(state->name_buffer_), "oem_%u", uid);
  snprintf(state->dir_buffer_, sizeof(state->dir_buffer_), "/");
  snprintf(state->sh_buffer_, sizeof(state->sh_buffer_), "/system/bin/sh");

  passwd* pw = &state->passwd_;
  pw->pw_name  = state->name_buffer_;
  pw->pw_dir   = state->dir_buffer_;
  pw->pw_shell = state->sh_buffer_;
  pw->pw_uid   = uid;
  pw->pw_gid   = uid;
  return pw;
}

static group* oem_id_to_group(gid_t gid, group_state_t* state) {
  if (!is_oem_id(gid)) {
    return NULL;
  }

  snprintf(state->group_name_buffer_, sizeof(state->group_name_buffer_),
           "oem_%u", gid);

  group* gr = &state->group_;
  gr->gr_name   = state->group_name_buffer_;
  gr->gr_gid    = gid;
  gr->gr_mem[0] = gr->gr_name;
  return gr;
}

// Translate a uid into the corresponding name.
// 0 to AID_APP-1                   -> "system", "radio", etc.
// AID_APP to AID_ISOLATED_START-1  -> u0_a1234
// AID_ISOLATED_START to AID_USER-1 -> u0_i1234
// AID_USER+                        -> u1_radio, u1_a1234, u2_i1234, etc.
// returns a passwd structure (sets errno to ENOENT on failure).
static passwd* app_id_to_passwd(uid_t uid, passwd_state_t* state) {
  if (uid < AID_APP) {
    errno = ENOENT;
    return NULL;
  }

  print_app_name_from_uid(uid, state->name_buffer_, sizeof(state->name_buffer_));

  const uid_t appid = uid % AID_USER;
  if (appid < AID_APP) {
      snprintf(state->dir_buffer_, sizeof(state->dir_buffer_), "/");
  } else {
      snprintf(state->dir_buffer_, sizeof(state->dir_buffer_), "/data");
  }

  snprintf(state->sh_buffer_, sizeof(state->sh_buffer_), "/system/bin/sh");

  passwd* pw = &state->passwd_;
  pw->pw_name  = state->name_buffer_;
  pw->pw_dir   = state->dir_buffer_;
  pw->pw_shell = state->sh_buffer_;
  pw->pw_uid   = uid;
  pw->pw_gid   = uid;
  return pw;
}

// Translate a gid into the corresponding app_<gid>
// group structure (sets errno to ENOENT on failure).
static group* app_id_to_group(gid_t gid, group_state_t* state) {
  if (gid < AID_APP) {
    errno = ENOENT;
    return NULL;
  }

  print_app_name_from_gid(gid, state->group_name_buffer_, sizeof(state->group_name_buffer_));

  group* gr = &state->group_;
  gr->gr_name   = state->group_name_buffer_;
  gr->gr_gid    = gid;
  gr->gr_mem[0] = gr->gr_name;
  return gr;
}

// pointers for libpackageparser
#define WEAK __attribute__((__weak__))
WEAK bool packagelist_parse(pfn_on_package, void*);
WEAK void packagelist_free(pkg_info*);

static bool packagelistparser_avail() {
  // check if libpackageparserlist.so is present and we have perms to access.
  return (packagelist_parse != NULL) &&
         (packagelist_free != NULL) &&
         (access("/data/system/packages.list", R_OK) == 0);
}

static void free_packagelistparser_state(packagelistparser_state_t **head) {
  for (packagelistparser_state_t* next = *head; next; next = *head) {
    *head = next->next;
    free(next);
  }
}

static packagelistparser_state_t* packagelistparser_from_info(pkg_info *info) {

  size_t home_idx = info->name ? strlen(info->name) + 1 : 1;
  size_t home_len = info->data_dir ? strlen(info->data_dir) + 1 : 1;

  packagelistparser_state_t* next;

  next = reinterpret_cast<packagelistparser_state_t *>(calloc(1, sizeof(*next) +
                                                              home_idx +
                                                              home_len));
  if (next) {
    next->id = info->uid;
    strcpy(next->name, info->name ?: "");
    next->home_idx = home_idx;
    strcpy(next->name + home_idx, info->data_dir ?: "");
  }
  packagelist_free(info);
  return next;
}

static bool aid_callback_name(pkg_info *info, void *userdata) {
  if (info == NULL) {
    return true;
  }
  if (info->uid < AID_APP) {
    packagelist_free(info);
    return true;
  }

  packagelistparser_state_t **head = reinterpret_cast<packagelistparser_state_t **>(userdata);
  packagelistparser_state_t* next = *head;
  if ((next == NULL) || (info->name == NULL) || (strcmp(next->name, info->name) != 0)) {
    packagelist_free(info);
    return true;
  }

  next = packagelistparser_from_info(info);
  if (next) {
    free(*head);
    *head = next;
  }
  return false;
}

static bool aid_callback_aid(pkg_info *info, void *userdata) {
  packagelistparser_state_t **head = reinterpret_cast<packagelistparser_state_t **>(userdata);
  packagelistparser_state_t* next = *head;
  if ((next == NULL) || (info->uid != next->id)) {
    id_t id = next->id;
    id_t userId = id % AID_USER;
    if ((info->uid == userId) ||
        ((userId > (AID_SHARED_GID_START - AID_APP)) &&
          (next->name[0] == '\0') &&
          (info->uid == (userId - (AID_SHARED_GID_START - AID_APP))))) {
      next = packagelistparser_from_info(info);
      if (next) {
        next->id = id; // liar!
        free(*head);
        *head = next;
      }
      return true;
    }
    packagelist_free(info);
    return true;
  }

  next = packagelistparser_from_info(info);
  if (next) {
    free(*head);
    *head = next;
  }
  return false;
}

static packagelistparser_state_t* packagelistparser_prep(
    packagelistparser_state_t **head, id_t id, const char* name) {

  if (!packagelistparser_avail()) {
    return NULL;
  }

  if (id) { // mutually exclusive
    name = NULL;
  }

  size_t home_idx = name ? strlen(name) : 0;
  packagelistparser_state_t* next =
      reinterpret_cast<packagelistparser_state_t *>(calloc(1, sizeof(*next) +
                                                           home_idx + 1));
  if (next == NULL) {
    return NULL;
  }

  free_packagelistparser_state(head);
  next->id = id;
  strcpy(next->name, name ?: "");
  next->home_idx = home_idx;
  *head = next;

  if (!packagelist_parse(name ? aid_callback_name : aid_callback_aid, head)) {
    free_packagelistparser_state(head);
    return NULL;
  }

  next = *head;
  if (next == NULL) {
    return NULL;
  }

  if (name ? (next->id == 0) : (next->name[0] == '\0')) {
    return NULL;
  }

  *head = NULL;
  return next;
}

static passwd* packagelistparser_to_passwd(packagelistparser_state_t *next,
                                           passwd_state_t* state) {
  snprintf(state->name_buffer_, sizeof(state->name_buffer_), "%s",
           next->name);
  snprintf(state->dir_buffer_, sizeof(state->dir_buffer_), "%s",
           next->name + next->home_idx);
  const uid_t uid = next->id;

  free(next);

  snprintf(state->sh_buffer_, sizeof(state->sh_buffer_), "/system/bin/sh");

  passwd* pw = &state->passwd_;
  pw->pw_name  = state->name_buffer_;
  pw->pw_dir   = state->dir_buffer_;
  pw->pw_shell = state->sh_buffer_;
  pw->pw_uid   = uid;
  pw->pw_gid   = uid;

  return pw;
}

passwd* getpwuid(uid_t uid) { // NOLINT: implementing bad function.
  passwd_state_t* state = g_passwd_tls_buffer.get();
  if (state == NULL) {
    return NULL;
  }

  passwd* pw = android_id_to_passwd(state, uid);
  if (pw != NULL) {
    return pw;
  }
  // Handle OEM range.
  pw = oem_id_to_passwd(uid, state);
  if (pw != NULL) {
    return pw;
  }

  // Handle package parser
  packagelistparser_state_t *next = packagelistparser_prep(&state->head, uid, NULL);
  if (next) {
    return packagelistparser_to_passwd(next, state);
  }

  return app_id_to_passwd(uid, state);
}

passwd* getpwnam(const char* login) { // NOLINT: implementing bad function.
  passwd_state_t* state = g_passwd_tls_buffer.get();
  if (state == NULL) {
    return NULL;
  }

  passwd* pw = android_name_to_passwd(state, login);
  if (pw != NULL) {
    return pw;
  }
  // Handle OEM range.
  pw = oem_id_to_passwd(oem_id_from_name(login), state);
  if (pw != NULL) {
    return pw;
  }

  id_t id = app_id_from_name(login, false);
  packagelistparser_state_t *next = packagelistparser_prep(&state->head, id, login);
  if (next) {
    return packagelistparser_to_passwd(next, state);
  }

  return app_id_to_passwd(id, state);
}

// All users are in just one group, the one passed in.
int getgrouplist(const char* /*user*/, gid_t group, gid_t* groups, int* ngroups) {
  if (*ngroups < 1) {
    *ngroups = 1;
    return -1;
  }
  groups[0] = group;
  return (*ngroups = 1);
}

char* getlogin() { // NOLINT: implementing bad function.
  passwd *pw = getpwuid(getuid()); // NOLINT: implementing bad function in terms of bad function.
  return (pw != NULL) ? pw->pw_name : NULL;
}

static bool aid_callback(pkg_info *info, void *userdata) {
  packagelistparser_state_t **head = reinterpret_cast<packagelistparser_state_t **>(userdata);
  packagelistparser_state_t* next;
  for (next = *head; next != NULL; next = next->next) {
    if ((info->uid < AID_APP) ||
        (next->id == info->uid) || // duplicate entry?
        (strcmp(next->name, info->name) == 0)) {
      break;
    }
  }
  if (next != NULL) {
    packagelist_free(info);
    return true;
  }

  next = packagelistparser_from_info(info);
  if (next) {
    next->next = *head;
    *head = next;
  }
  return true;
}

static packagelistparser_state_t* fill_packagelistparser_state(packagelistparser_state_t **head) {
  if ((*head == NULL) && packagelistparser_avail()) {
    packagelist_parse(aid_callback, head);
  }
  packagelistparser_state_t* next = *head;
  if (next) {
    *head = next->next;
  }
  return next;
}

void setpwent() {
  passwd_state_t* state = g_passwd_tls_buffer.get();
  if (state) {
    state->getpwent_idx = 0;
    free_packagelistparser_state(&state->head);
  }
}

void endpwent() {
  setpwent();
}

passwd* getpwent() {
  passwd_state_t* state = g_passwd_tls_buffer.get();
  if (state == NULL) {
    return NULL;
  }
  if (state->getpwent_idx < 0) {
    free_packagelistparser_state(&state->head);
    return NULL;
  }

  size_t start = 0;
  ssize_t end = android_id_count;
  if (state->getpwent_idx < end) {
    return android_iinfo_to_passwd(state, android_ids + state->getpwent_idx++);
  }

  start = end;
  end += AID_OEM_RESERVED_END - AID_OEM_RESERVED_START + 1;

  if (state->getpwent_idx < end) {
    return oem_id_to_passwd(
        state->getpwent_idx++ - start + AID_OEM_RESERVED_START, state);
  }

  start = end;
  end += AID_OEM_RESERVED_2_END - AID_OEM_RESERVED_2_START + 1;

  if (state->getpwent_idx < end) {
    return oem_id_to_passwd(
        state->getpwent_idx++ - start + AID_OEM_RESERVED_2_START, state);
  }

  packagelistparser_state_t *next;
  if (state->getpwent_idx == end) {
    next = fill_packagelistparser_state(&state->head);
    if (next) {
      state->getpwent_idx++;
      return packagelistparser_to_passwd(next, state);
    }
  } else {
    next = state->head;
    if (next) {
      state->head = next->next;
      if (state->head == NULL) {
        state->getpwent_idx = -1;
      }
      return packagelistparser_to_passwd(next, state);
    }
  }

  start = end;
  end += AID_USER - AID_APP; // Do not expose higher users

  if (state->getpwent_idx < end) {
    return app_id_to_passwd(state->getpwent_idx++ - start + AID_APP, state);
  }

  // We are not reporting u1_a* and higher or we will be here forever
  state->getpwent_idx = -1;
  return NULL;
}

static group* packagelistparser_to_group(packagelistparser_state_t* next,
                                         group_state_t* state) {
  init_group_state(state);

  snprintf(state->group_name_buffer_, sizeof(state->group_name_buffer_), "%s",
           next->name);
  const gid_t gid = next->id;

  free(next);

  group* gr = &state->group_;
  gr->gr_name   = state->group_name_buffer_;
  gr->gr_gid    = gid;
  gr->gr_mem[0] = gr->gr_name;

  return gr;
}

static group* getgrgid_internal(gid_t gid, group_state_t* state) {
  group* grp = android_id_to_group(state, gid);
  if (grp != NULL) {
    return grp;
  }
  // Handle OEM range.
  grp = oem_id_to_group(gid, state);
  if (grp != NULL) {
    return grp;
  }

  // Handle package parser
  packagelistparser_state_t *next = packagelistparser_prep(&state->head, gid, NULL);
  if (next) {
    return packagelistparser_to_group(next, state);
  }

  return app_id_to_group(gid, state);
}

group* getgrgid(gid_t gid) { // NOLINT: implementing bad function.
  group_state_t* state = __group_state();
  if (state == NULL) {
    return NULL;
  }
  return getgrgid_internal(gid, state);
}

static group* getgrnam_internal(const char* name, group_state_t* state) {
  group* grp = android_name_to_group(state, name);
  if (grp != NULL) {
    return grp;
  }
  // Handle OEM range.
  grp = oem_id_to_group(oem_id_from_name(name), state);
  if (grp != NULL) {
    return grp;
  }

  id_t id = app_id_from_name(name, true);
  packagelistparser_state_t *next = packagelistparser_prep(&state->head, id, name);
  if (next) {
    return packagelistparser_to_group(next, state);
  }

  return app_id_to_group(id, state);
}

group* getgrnam(const char* name) { // NOLINT: implementing bad function.
  group_state_t* state = __group_state();
  if (state == NULL) {
    return NULL;
  }
  return getgrnam_internal(name, state);
}

static int getgroup_r(bool by_name, const char* name, gid_t gid, struct group* grp, char* buf,
                      size_t buflen, struct group** result) {
  ErrnoRestorer errno_restorer;
  *result = NULL;
  char* p = reinterpret_cast<char*>(
      BIONIC_ALIGN(reinterpret_cast<uintptr_t>(buf), sizeof(uintptr_t)));
  if (p + sizeof(group_state_t) > buf + buflen) {
    return ERANGE;
  }
  group_state_t* state = reinterpret_cast<group_state_t*>(p);
  init_group_state(state);
  group* retval = (by_name ? getgrnam_internal(name, state) : getgrgid_internal(gid, state));
  if (retval != NULL) {
    *grp = *retval;
    *result = grp;
    return 0;
  }
  return errno;
}

int getgrgid_r(gid_t gid, struct group* grp, char* buf, size_t buflen, struct group** result) {
  return getgroup_r(false, NULL, gid, grp, buf, buflen, result);
}

int getgrnam_r(const char* name, struct group* grp, char* buf, size_t buflen,
               struct group **result) {
  return getgroup_r(true, name, 0, grp, buf, buflen, result);
}

void setgrent() {
  group_state_t* state = g_group_tls_buffer.get();
  if (state) {
    state->getgrent_idx = 0;
    free_packagelistparser_state(&state->head);
  }
}

void endgrent() {
  setgrent();
}

group* getgrent() {
  group_state_t* state = g_group_tls_buffer.get();
  if (state == NULL) {
    return NULL;
  }
  if (state->getgrent_idx < 0) {
    free_packagelistparser_state(&state->head);
    return NULL;
  }

  size_t start = 0;
  ssize_t end = android_id_count;
  if (state->getgrent_idx < end) {
    init_group_state(state);
    return android_iinfo_to_group(state, android_ids + state->getgrent_idx++);
  }

  start = end;
  end += AID_OEM_RESERVED_END - AID_OEM_RESERVED_START + 1;

  if (state->getgrent_idx < end) {
    init_group_state(state);
    return oem_id_to_group(
        state->getgrent_idx++ - start + AID_OEM_RESERVED_START, state);
  }

  start = end;
  end += AID_OEM_RESERVED_2_END - AID_OEM_RESERVED_2_START + 1;

  if (state->getgrent_idx < end) {
    init_group_state(state);
    return oem_id_to_group(
        state->getgrent_idx++ - start + AID_OEM_RESERVED_2_START, state);
  }

  packagelistparser_state_t *next;
  if (state->getgrent_idx == end) {
    next = fill_packagelistparser_state(&state->head);
    if (next) {
      state->getgrent_idx++;
      return packagelistparser_to_group(next, state);
    }
  } else {
    next = state->head;
    if (next) {
      state->head = next->next;
      if (state->head == NULL) {
        state->getgrent_idx = -1;
      }
      return packagelistparser_to_group(next, state);
    }
  }

  start = end;
  end += AID_USER - AID_APP; // Do not expose higher groups

  if (state->getgrent_idx < end) {
    init_group_state(state);
    return app_id_to_group(state->getgrent_idx++ - start + AID_APP, state);
  }

  // We are not reporting u1_a* and higher or we will be here forever
  state->getgrent_idx = -1;
  return NULL;
}
