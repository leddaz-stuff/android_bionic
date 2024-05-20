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

#if __has_include(<sys/thread_properties.h>)

#include <sys/thread_properties.h>
#include <unistd.h>

#include <thread>

void exit_cb_1() {
  write(1, "<cb1>", 5);
}

void exit_cb_2() {
  write(1, "<cb2>", 5);
}

void exit_cb_3() {
  write(1, "<cb3>", 5);
}

void test_register_thread_exit_cb() {
  __libc_register_thread_exit_callback(&exit_cb_1);
  __libc_register_thread_exit_callback(&exit_cb_2);
  __libc_register_thread_exit_callback(&exit_cb_3);
}

int main() {
  write(1, "<main>", 6);
  test_register_thread_exit_cb();
  write(1, "<t1>", 4);
  std::thread([] { write(1, "A", 1); }).join();
  write(1, "<t2>", 4);
  std::thread([] { write(1, "B", 1); }).join();
  write(1, "<exit>", 6);
  return 0;
}

#else

#include <stdio.h>

int main() {
  printf("test binary built without <sys/thread_properties.h>\n");
  return 1;
}

#endif
