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

#pragma once

#include <stdint.h>
#include <sys/ifunc.h>

#include <private/bionic_call_ifunc_resolver.h>

#if defined(__aarch64__)
#define IFUNC_ARGS (uint64_t hwcap __attribute__((unused)), \
                    __ifunc_arg_t* arg __attribute__((unused)))
#elif defined(__arm__)
#define IFUNC_ARGS (unsigned long hwcap __attribute__((unused)))
#else
#define IFUNC_ARGS ()
#endif

#define DECLARE_FUNC(type, name) \
    __attribute__((visibility("hidden"))) \
    type name

#define RETURN_FUNC(type, name) { \
        DECLARE_FUNC(type, name); \
        return name; \
    }

#if defined(BIONIC_DYNAMIC_DISPATCH)

// We can't have HWASAN enabled in resolvers because they may be called before
// HWASAN is initialized.
#define DEFINE_IFUNC_FOR(name)                                  \
  name##_func_t name __attribute__((ifunc(#name "_resolver"))); \
  __attribute__((visibility("hidden")))                         \
  __attribute__((no_sanitize("hwaddress"))) name##_func_t* name##_resolver IFUNC_ARGS

#define DEFINE_STATIC_SHIM(x)

#elif defined(BIONIC_STATIC_DISPATCH)

#define DEFINE_IFUNC_FOR(name)               \
  name##_func_t* name##_resolver IFUNC_ARGS; \
  __attribute__((visibility("hidden")))      \
  __attribute__((no_sanitize("hwaddress"))) name##_func_t* name##_resolver IFUNC_ARGS

#define DEFINE_STATIC_SHIM(x) x

#define FORWARD(name)                                                               \
  static name##_func_t* fn = reinterpret_cast<name##_func_t*>(                      \
      __bionic_call_ifunc_resolver(reinterpret_cast<ElfW(Addr)>(name##_resolver))); \
  return fn

#else
#error neither dynamic nor static dispatch?!
#endif

#define MEMCHR_SHIM()                                                  \
  DEFINE_STATIC_SHIM(void* memchr(const void* src, int ch, size_t n) { \
    FORWARD(memchr)(src, ch, n);                                       \
  })
#define MEMCMP_SHIM()                                                         \
  DEFINE_STATIC_SHIM(int memcmp(const void* lhs, const void* rhs, size_t n) { \
    FORWARD(memcmp)(lhs, rhs, n);                                             \
  })
#define MEMCPY_SHIM()                                                     \
  DEFINE_STATIC_SHIM(void* memcpy(void* dst, const void* src, size_t n) { \
    FORWARD(memcpy)(dst, src, n);                                         \
  })
#define MEMMOVE_SHIM()                                                     \
  DEFINE_STATIC_SHIM(void* memmove(void* dst, const void* src, size_t n) { \
    FORWARD(memmove)(dst, src, n);                                         \
  })
#define MEMRCHR_SHIM()                                                \
  DEFINE_STATIC_SHIM(int memrchr(const void* src, int ch, size_t n) { \
    FORWARD(memrchr)(src, ch, n);                                     \
  })
#define MEMSET_SHIM() \
  DEFINE_STATIC_SHIM(void* memset(void* dst, int ch, size_t n) { FORWARD(memset)(dst, ch, n); })
#define __MEMSET_CHK_SHIM()                                                       \
  DEFINE_STATIC_SHIM(void* __memset_chk(void* dst, int ch, size_t n, size_t n2) { \
    FORWARD(__memset_chk)(dst, ch, n, n2);                                        \
  })
#define STPCPY_SHIM() \
  DEFINE_STATIC_SHIM(char* stpcpy(char* dst, const char* src) { FORWARD(stpcpy)(dst, src); })
#define STRCAT_SHIM() \
  DEFINE_STATIC_SHIM(char* strcat(char* dst, const char* src) { FORWARD(strcat)(dst, src); })
#define __STRCAT_CHK_SHIM()                                                                \
  DEFINE_STATIC_SHIM(char* __strcat_chk(char* dst, const char* src, size_t dst_buf_size) { \
    FORWARD(__strcat_chk)(dst, src, dst_buf_size);                                         \
  })
#define STRCHR_SHIM() \
  DEFINE_STATIC_SHIM(char* strchr(const char* src, int ch) { FORWARD(strchr)(src, ch); })
#define STRCHRNUL_SHIM() \
  DEFINE_STATIC_SHIM(char* strchrnul(const char* src, int ch) { FORWARD(strchrnul)(src, ch); })
#define STRCMP_SHIM() \
  DEFINE_STATIC_SHIM(int strcmp(char* lhs, const char* rhs) { FORWARD(strcmp)(lhs, rhs); })
#define STRCPY_SHIM() \
  DEFINE_STATIC_SHIM(char* strcpy(char* dst, const char* src) { FORWARD(strcpy)(dst, src); })
#define __STRCPY_CHK_SHIM()                                                           \
  DEFINE_STATIC_SHIM(char* __strcpy_chk(char* dst, const char* src, size_t dst_len) { \
    FORWARD(__strcpy_chk)(dst, src, dst_len);                                         \
  })
#define STRLEN_SHIM() DEFINE_STATIC_SHIM(size_t strlen(const char* s) { FORWARD(strlen)(s); })
#define STRNCAT_SHIM()                                                     \
  DEFINE_STATIC_SHIM(char* strncat(char* dst, const char* src, size_t n) { \
    FORWARD(strncat)(dst, src, n);                                         \
  })
#define STRNCMP_SHIM()                                                         \
  DEFINE_STATIC_SHIM(int strncmp(const char* lhs, const char* rhs, size_t n) { \
    FORWARD(strncmp)(lhs, rhs, n);                                             \
  })
#define STRNCPY_SHIM()                                                     \
  DEFINE_STATIC_SHIM(char* strncpy(char* dst, const char* src, size_t n) { \
    FORWARD(strncpy)(dst, src, n);                                         \
  })
#define STRNLEN_SHIM() \
  DEFINE_STATIC_SHIM(size_t strnlen(const char* s, size_t n) { FORWARD(strnlen)(s, n); })
#define STRRCHR_SHIM() \
  DEFINE_STATIC_SHIM(char* strrchr(const char* src, int ch) { FORWARD(strrchr)(src, ch); })
