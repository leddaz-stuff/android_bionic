/*
 * Copyright (C) 2024 The Android Open Source Project
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

#pragma once

/**
 * @file sys/fanotify.h
 * @brief Filesystem event monitoring.
 */

#include <stdint.h>
#include <sys/cdefs.h>

#include <linux/fanotify.h>

__BEGIN_DECLS

/**
 * [fanotify_init(2)](https://man7.org/linux/man-pages/man2/fanotify_init.2.html)
 * creates a new fanotify group.
 *
 * Available since API level 36.
 *
 * Returns a new file descriptor on success, and returns -1 and sets `errno` on failure.
 */
int fanotify_init(unsigned __flags, unsigned __event_fd_flags) __INTRODUCED_IN(36);

/**
 * [fanotify_mark(2)](https://man7.org/linux/man-pages/man2/fanotify_mark.2.html)
 * adds/removes/modifies an fanotify mark.
 *
 * Returns 0 on success, and returns -1 and sets `errno` on failure.
 *
 * Only available for x86/x86-64.
 */
int fanotify_mark(int __fanotify_fd, unsigned __flags, uint64_t __mask, int __dir_fd, const char* _Nullable __pathname) __INTRODUCED_IN(36);

__END_DECLS
