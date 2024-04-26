/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <fcntl.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <private/bionic_ifuncs.h>

extern "C" {

static bool have_fast_v() {
  return true;
}

typedef void* memchr_func(const void*, int, size_t);
DEFINE_IFUNC_FOR(memchr) {
  if (have_fast_v()) RETURN_FUNC(memchr_func, memchr_v);
  RETURN_FUNC(memchr_func, memchr_gc);
}

typedef int memcmp_func(const void*, const void*, size_t);
DEFINE_IFUNC_FOR(memcmp) {
  if (have_fast_v()) RETURN_FUNC(memcmp_func, memcmp_v);
  RETURN_FUNC(memcmp_func, memcmp_gc);
}

typedef void* memcpy_func(void*, const void*, size_t);
DEFINE_IFUNC_FOR(memcpy) {
  if (have_fast_v()) RETURN_FUNC(memcpy_func, memcpy_v);
  RETURN_FUNC(memcpy_func, memcpy_gc);
}

typedef void* memmove_func(void*, const void*, size_t);
DEFINE_IFUNC_FOR(memmove) {
  if (have_fast_v()) RETURN_FUNC(memmove_func, memmove_v);
  RETURN_FUNC(memmove_func, memmove_gc);
}

typedef void* memset_func(void*, int, size_t);
DEFINE_IFUNC_FOR(memset) {
  if (have_fast_v()) RETURN_FUNC(memset_func, memset_v);
  RETURN_FUNC(memset_func, memset_gc);
}

typedef char* stpcpy_func(char*, const char*);
DEFINE_IFUNC_FOR(stpcpy) {
  if (have_fast_v()) RETURN_FUNC(stpcpy_func, stpcpy_v);
  RETURN_FUNC(stpcpy_func, stpcpy_gc);
}

typedef char* strcat_func(char*, const char*);
DEFINE_IFUNC_FOR(strcat) {
  if (have_fast_v()) RETURN_FUNC(strcat_func, strcat_v);
  RETURN_FUNC(strcat_func, strcat_gc);
}

typedef char* strchr_func(const char*, int);
DEFINE_IFUNC_FOR(strchr) {
  if (have_fast_v()) RETURN_FUNC(strchr_func, strchr_v);
  RETURN_FUNC(strchr_func, strchr_gc);
}

typedef int strcmp_func(const char*, const char*);
DEFINE_IFUNC_FOR(strcmp) {
  if (have_fast_v()) RETURN_FUNC(strcmp_func, strcmp_v);
  RETURN_FUNC(strcmp_func, strcmp_gc);
}

typedef char* strcpy_func(char*, const char*);
DEFINE_IFUNC_FOR(strcpy) {
  if (have_fast_v()) RETURN_FUNC(strcpy_func, strcpy_v);
  RETURN_FUNC(strcpy_func, strcpy_gc);
}

typedef size_t strlen_func(const char*);
DEFINE_IFUNC_FOR(strlen) {
  if (have_fast_v()) RETURN_FUNC(strlen_func, strlen_v);
  RETURN_FUNC(strlen_func, strlen_gc);
}

typedef char* strncat_func(char*, const char*, size_t);
DEFINE_IFUNC_FOR(strncat) {
  if (have_fast_v()) RETURN_FUNC(strncat_func, strncat_v);
  RETURN_FUNC(strncat_func, strncat_gc);
}

typedef int strncmp_func(const char*, const char*, size_t);
DEFINE_IFUNC_FOR(strncmp) {
  if (have_fast_v()) RETURN_FUNC(strncmp_func, strncmp_v);
  RETURN_FUNC(strncmp_func, strncmp_gc);
}

typedef char* strncpy_func(char*, const char*, size_t);
DEFINE_IFUNC_FOR(strncpy) {
  if (have_fast_v()) RETURN_FUNC(strncpy_func, strncpy_v);
  RETURN_FUNC(strncpy_func, strncpy_gc);
}

typedef size_t strnlen_func(const char*, size_t);
DEFINE_IFUNC_FOR(strnlen) {
  if (have_fast_v()) RETURN_FUNC(strnlen_func, strnlen_v);
  RETURN_FUNC(strnlen_func, strnlen_gc);
}

}  // extern "C"
