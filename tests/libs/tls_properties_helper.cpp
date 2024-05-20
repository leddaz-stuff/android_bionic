/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <dlfcn.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <thread>

#include "CHECK.h"

#if __has_include(<sys/thread_properties.h>)

#include <sys/thread_properties.h>

thread_local int thread_local_var;

static void test_static_tls_bounds() {
  thread_local_var = 123;
  void* start_addr = nullptr;
  void* end_addr = nullptr;

  __libc_get_static_tls_bounds(reinterpret_cast<void**>(&start_addr),
                               reinterpret_cast<void**>(&end_addr));
  CHECK(start_addr != nullptr);
  CHECK(end_addr != nullptr);

  CHECK(&thread_local_var >= start_addr && &thread_local_var < end_addr);
}

// Tests iterate_dynamic tls chunks.
// Export a var from the shared so.
extern __thread char large_tls_var[4 * 1024 * 1024];

static void test_iter_tls() {
  void* lib = dlopen("libtest_elftls_dynamic.so", RTLD_LOCAL | RTLD_NOW);
  large_tls_var[1025] = 'a';

  struct Data {
    int call_count, found_count;
  } data = {};
  auto cb = +[](void* dtls_begin, void* dtls_end, size_t /*dso_id*/, void* arg) {
    Data* d = static_cast<Data*>(arg);
    d->call_count++;
    if (&large_tls_var >= dtls_begin && &large_tls_var < dtls_end) d->found_count++;
  };
  __libc_iterate_dynamic_tls(gettid(), cb, &data);

  printf("done_iterate_dynamic_tls call_count=%d found_count=%d\n", data.call_count,
         data.found_count);
}

static pid_t g_parent_tid;
static void* g_parent_addr = nullptr;

static void test_iterate_another_thread_tls() {
  large_tls_var[1025] = 'b';
  g_parent_addr = &large_tls_var;

  g_parent_tid = gettid();

  std::thread([] {
    struct Data {
      int call_count, found_count;
    } data = {};
    auto cb = +[](void* dtls_begin, void* dtls_end, size_t /*dso_id*/, void* arg) {
      Data* d = static_cast<Data*>(arg);
      d->call_count++;
      if (g_parent_addr >= dtls_begin && g_parent_addr < dtls_end) d->found_count++;
    };
    __libc_iterate_dynamic_tls(g_parent_tid, cb, &data);
    printf("done_iterate_another_thread_tls call_count=%d found_count=%d\n", data.call_count,
           data.found_count);
  }).join();
}

int main() {
  printf("test_static_tls_bounds()\n");
  test_static_tls_bounds();
  printf("test_iter_tls()\n");
  std::thread([] { test_iter_tls(); }).join();
  printf("test_iterate_another_thread_tls()\n");
  test_iterate_another_thread_tls();
  return 0;
}

#else

int main() {
  printf("test binary built without <sys/thread_properties.h>\n");
  return 1;
}

#endif
