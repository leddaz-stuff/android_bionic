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

#include <gtest/gtest.h>

#include <sys/fanotify.h>

#include <thread>

#include "utils.h"

TEST(sys_fanotify, smoke) {
  int fd = fanotify_init(FAN_CLOEXEC | FAN_CLASS_CONTENT, O_RDONLY | O_LARGEFILE);
  ASSERT_NE(-1, fd) << strerror(errno);

  TemporaryDir td;
  ASSERT_NE(-1, fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_MOUNT, FAN_CREATE, AT_FDCWD, td.path));

  std::thread t([&td] { close(creat(td.path, 0666)); });

  fanotify_event_metadata buf[128];
  ssize_t n = TEMP_FAILURE_RETRY(read(fd, buf, sizeof(buf)));

  for (fanotify_event_metadata* m = buf; FAN_EVENT_OK(m, n); m = FAN_EVENT_NEXT(m, n)) {
    ASSERT_EQ(m->vers, FANOTIFY_METADATA_VERSION);
    printf("%llx\n", static_cast<unsigned long long>(m->mask));
    char proc_path[PATH_MAX];
    snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", m->fd);
    char path[PATH_MAX];
    readlink(proc_path, path, sizeof(path));
    printf("path=%s\n", path);
  }

  t.join();
  close(fd);
}
