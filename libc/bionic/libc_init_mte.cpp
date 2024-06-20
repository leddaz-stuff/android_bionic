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

#include <android/api-level.h>
#include <elf.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include <sys/mman.h>

#include "async_safe/log.h"
#include "heap_tagging.h"
#include "platform/bionic/macros.h"
#include "platform/bionic/mte.h"
#include "platform/bionic/page.h"
#include "private/bionic_asm.h"
#include "private/bionic_asm_note.h"
#include "private/bionic_elf_tls.h"
#include "private/bionic_globals.h"
#include "private/bionic_tls.h"
#include "private/elf_note.h"
#include "sys/system_properties.h"
#include "sysprop_helpers.h"

extern "C" const char* __gnu_basename(const char* path);

#ifdef __aarch64__
static HeapTaggingLevel __get_memtag_level_from_note(const ElfW(Phdr) * phdr_start, size_t phdr_ct,
                                                     const ElfW(Addr) load_bias, bool* stack) {
  const ElfW(Nhdr) * note;
  const char* desc;
  if (!__find_elf_note(NT_ANDROID_TYPE_MEMTAG, "Android", phdr_start, phdr_ct, &note, &desc,
                       load_bias)) {
    return M_HEAP_TAGGING_LEVEL_TBI;
  }

  // Previously (in Android 12), if the note was != 4 bytes, we check-failed
  // here. Let's be more permissive to allow future expansion.
  if (note->n_descsz < 4) {
    async_safe_fatal("unrecognized android.memtag note: n_descsz = %d, expected >= 4",
                     note->n_descsz);
  }

  // `desc` is always aligned due to ELF requirements, enforced in __find_elf_note().
  ElfW(Word) note_val = *reinterpret_cast<const ElfW(Word)*>(desc);
  *stack = (note_val & NT_MEMTAG_STACK) != 0;

  // Warning: In Android 12, any value outside of bits [0..3] resulted in a check-fail.
  if (!(note_val & (NT_MEMTAG_HEAP | NT_MEMTAG_STACK))) {
    async_safe_format_log(ANDROID_LOG_INFO, "libc",
                          "unrecognised memtag note_val did not specificy heap or stack: %u",
                          note_val);
    return M_HEAP_TAGGING_LEVEL_TBI;
  }

  unsigned mode = note_val & NT_MEMTAG_LEVEL_MASK;
  switch (mode) {
    case NT_MEMTAG_LEVEL_NONE:
      // Note, previously (in Android 12), NT_MEMTAG_LEVEL_NONE was
      // NT_MEMTAG_LEVEL_DEFAULT, which implied SYNC mode. This was never used
      // by anyone, but we note it (heh) here for posterity, in case the zero
      // level becomes meaningful, and binaries with this note can be executed
      // on Android 12 devices.
      return M_HEAP_TAGGING_LEVEL_TBI;
    case NT_MEMTAG_LEVEL_ASYNC:
      return M_HEAP_TAGGING_LEVEL_ASYNC;
    case NT_MEMTAG_LEVEL_SYNC:
    default:
      // We allow future extensions to specify mode 3 (currently unused), with
      // the idea that it might be used for ASYMM mode or something else. On
      // this version of Android, it falls back to SYNC mode.
      return M_HEAP_TAGGING_LEVEL_SYNC;
  }
}

// Returns true if there's an environment setting (either sysprop or env var)
// that should overwrite the ELF note, and places the equivalent heap tagging
// level into *level.
static bool get_environment_memtag_setting(HeapTaggingLevel* level) {
  static const char kMemtagPrognameSyspropPrefix[] = "arm64.memtag.process.";
  static const char kMemtagGlobalSysprop[] = "persist.arm64.memtag.default";
  static const char kMemtagOverrideSyspropPrefix[] =
      "persist.device_config.memory_safety_native.mode_override.process.";

  const char* progname = __libc_shared_globals()->init_progname;
  if (progname == nullptr) return false;

  const char* basename = __gnu_basename(progname);

  char options_str[PROP_VALUE_MAX];
  char sysprop_name[512];
  async_safe_format_buffer(sysprop_name, sizeof(sysprop_name), "%s%s", kMemtagPrognameSyspropPrefix,
                           basename);
  char remote_sysprop_name[512];
  async_safe_format_buffer(remote_sysprop_name, sizeof(remote_sysprop_name), "%s%s",
                           kMemtagOverrideSyspropPrefix, basename);
  const char* sys_prop_names[] = {sysprop_name, remote_sysprop_name, kMemtagGlobalSysprop};

  if (!get_config_from_env_or_sysprops("MEMTAG_OPTIONS", sys_prop_names, arraysize(sys_prop_names),
                                       options_str, sizeof(options_str))) {
    return false;
  }

  if (strcmp("sync", options_str) == 0) {
    *level = M_HEAP_TAGGING_LEVEL_SYNC;
  } else if (strcmp("async", options_str) == 0) {
    *level = M_HEAP_TAGGING_LEVEL_ASYNC;
  } else if (strcmp("off", options_str) == 0) {
    *level = M_HEAP_TAGGING_LEVEL_TBI;
  } else {
    async_safe_format_log(
        ANDROID_LOG_ERROR, "libc",
        "unrecognized memtag level: \"%s\" (options are \"sync\", \"async\", or \"off\").",
        options_str);
    return false;
  }

  return true;
}

// Returns the initial heap tagging level. Note: This function will never return
// M_HEAP_TAGGING_LEVEL_NONE, if MTE isn't enabled for this process we enable
// M_HEAP_TAGGING_LEVEL_TBI.
static HeapTaggingLevel __get_tagging_level(const memtag_dynamic_entries_t* memtag_dynamic_entries,
                                            const void* phdr_start, size_t phdr_ct,
                                            uintptr_t load_bias, bool* stack) {
  HeapTaggingLevel level = M_HEAP_TAGGING_LEVEL_TBI;

  // If the dynamic entries exist, use those. Otherwise, fall back to the old
  // Android note, which is still used for fully static executables. When
  // -fsanitize=memtag* is used in newer toolchains, currently both the dynamic
  // entries and the old note are created, but we'd expect to move to just the
  // dynamic entries for dynamically linked executables in the future. In
  // addition, there's still some cleanup of the build system (that uses a
  // manually-constructed note) needed. For more information about the dynamic
  // entries, see:
  // https://github.com/ARM-software/abi-aa/blob/main/memtagabielf64/memtagabielf64.rst#dynamic-section
  if (memtag_dynamic_entries && memtag_dynamic_entries->has_memtag_mode) {
    switch (memtag_dynamic_entries->memtag_mode) {
      case 0:
        level = M_HEAP_TAGGING_LEVEL_SYNC;
        break;
      case 1:
        level = M_HEAP_TAGGING_LEVEL_ASYNC;
        break;
      default:
        async_safe_format_log(ANDROID_LOG_INFO, "libc",
                              "unrecognised DT_AARCH64_MEMTAG_MODE value: %u",
                              memtag_dynamic_entries->memtag_mode);
    }
    *stack = memtag_dynamic_entries->memtag_stack;
  } else {
    level = __get_memtag_level_from_note(reinterpret_cast<const ElfW(Phdr)*>(phdr_start), phdr_ct,
                                         load_bias, stack);
  }

  // We can't short-circuit the environment override, as `stack` is still inherited from the
  // binary's settings.
  get_environment_memtag_setting(&level);
  return level;
}

static int64_t __get_memtag_upgrade_secs() {
  char* env = getenv("BIONIC_MEMTAG_UPGRADE_SECS");
  if (!env) return 0;
  int64_t timed_upgrade = 0;
  static const char kAppProcessName[] = "app_process64";
  const char* progname = __libc_shared_globals()->init_progname;
  progname = progname ? __gnu_basename(progname) : nullptr;
  // disable timed upgrade for zygote, as the thread spawned will violate the requirement
  // that it be single-threaded.
  if (!progname || strncmp(progname, kAppProcessName, sizeof(kAppProcessName)) != 0) {
    char* endptr;
    timed_upgrade = strtoll(env, &endptr, 10);
    if (*endptr != '\0' || timed_upgrade < 0) {
      async_safe_format_log(ANDROID_LOG_ERROR, "libc",
                            "Invalid value for BIONIC_MEMTAG_UPGRADE_SECS: %s", env);
      timed_upgrade = 0;
    }
  }
  // Make sure that this does not get passed to potential processes inheriting
  // this environment.
  unsetenv("BIONIC_MEMTAG_UPGRADE_SECS");
  return timed_upgrade;
}

// Figure out the desired memory tagging mode (sync/async, heap/globals/stack) for this executable.
// This function is called from the linker before the main executable is relocated.
__attribute__((no_sanitize("hwaddress", "memtag"))) void __libc_init_mte(
    const memtag_dynamic_entries_t* memtag_dynamic_entries, const void* phdr_start, size_t phdr_ct,
    uintptr_t load_bias, void* stack_top) {
  bool memtag_stack = false;
  HeapTaggingLevel level =
      __get_tagging_level(memtag_dynamic_entries, phdr_start, phdr_ct, load_bias, &memtag_stack);
  // initial_memtag_stack is used by the linker (in linker.cpp) to communicate than any library
  // linked by this executable enables memtag-stack.
  // memtag_stack is also set for static executables if they request memtag stack via the note,
  // in which case it will differ from initial_memtag_stack.
  if (__libc_shared_globals()->initial_memtag_stack || memtag_stack) {
    memtag_stack = true;
    __libc_shared_globals()->initial_memtag_stack_abi = true;
    __get_bionic_tcb()->tls_slot(TLS_SLOT_STACK_MTE) = __allocate_stack_mte_ringbuffer(0, nullptr);
  }
  if (int64_t timed_upgrade = __get_memtag_upgrade_secs()) {
    if (level == M_HEAP_TAGGING_LEVEL_ASYNC) {
      async_safe_format_log(ANDROID_LOG_INFO, "libc",
                            "Attempting timed MTE upgrade from async to sync.");
      __libc_shared_globals()->heap_tagging_upgrade_timer_sec = timed_upgrade;
      level = M_HEAP_TAGGING_LEVEL_SYNC;
    } else if (level != M_HEAP_TAGGING_LEVEL_SYNC) {
      async_safe_format_log(
          ANDROID_LOG_ERROR, "libc",
          "Requested timed MTE upgrade from invalid %s to sync. Ignoring.",
          DescribeTaggingLevel(level));
    }
  }
  if (level == M_HEAP_TAGGING_LEVEL_SYNC || level == M_HEAP_TAGGING_LEVEL_ASYNC) {
    unsigned long prctl_arg = PR_TAGGED_ADDR_ENABLE | PR_MTE_TAG_SET_NONZERO;
    prctl_arg |= (level == M_HEAP_TAGGING_LEVEL_SYNC) ? PR_MTE_TCF_SYNC : PR_MTE_TCF_ASYNC;

    // When entering ASYNC mode, specify that we want to allow upgrading to SYNC by OR'ing in the
    // SYNC flag. But if the kernel doesn't support specifying multiple TCF modes, fall back to
    // specifying a single mode.
    if (prctl(PR_SET_TAGGED_ADDR_CTRL, prctl_arg | PR_MTE_TCF_SYNC, 0, 0, 0) == 0 ||
        prctl(PR_SET_TAGGED_ADDR_CTRL, prctl_arg, 0, 0, 0) == 0) {
      __libc_shared_globals()->initial_heap_tagging_level = level;
      __libc_shared_globals()->initial_memtag_stack = memtag_stack;

      if (memtag_stack) {
        void* pg_start =
            reinterpret_cast<void*>(page_start(reinterpret_cast<uintptr_t>(stack_top)));
        if (mprotect(pg_start, page_size(), PROT_READ | PROT_WRITE | PROT_MTE | PROT_GROWSDOWN)) {
          async_safe_fatal("error: failed to set PROT_MTE on main thread stack: %m");
        }
      }

      return;
    }
  }

  // MTE was either not enabled, or wasn't supported on this device. Try and use
  // TBI.
  if (prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE, 0, 0, 0) == 0) {
    __libc_shared_globals()->initial_heap_tagging_level = M_HEAP_TAGGING_LEVEL_TBI;
  }
  // We did not enable MTE, so we do not need to arm the upgrade timer.
  __libc_shared_globals()->heap_tagging_upgrade_timer_sec = 0;
  // We also didn't enable memtag_stack.
  __libc_shared_globals()->initial_memtag_stack = false;
}
#else   // __aarch64__
void __libc_init_mte(const memtag_dynamic_entries_t*, const void*, size_t, uintptr_t, void*) {}
#endif  // __aarch64__
