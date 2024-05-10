/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>

// Deliberately overwrite the stack canary.
__attribute__((noinline, optnone)) void modify_stack_protector_test() {
  // We can't use memset here because it's fortified, and we want to test
  // the line of defense *after* that.
  // We can't make a constant change, since the existing byte might already have
  // had that value.
  char* p = reinterpret_cast<char*>(&p + 1);
  *p = ~*p;
}

__attribute__((noinline, optnone)) void modify_stack_protector_tag_workaround_test() {
#if defined(__aarch64__)
  for (size_t i = 0; i < 100; i++) {
    char* p = reinterpret_cast<char*>(&p + 1);
    if ((reinterpret_cast<uintptr_t>(p) >> 52) == 0x80) {
      // When hwasan is enabled, a 0x80 tag doesn't trigger a failure.
      // So create another frame that should use a different tag.
      // This is a tempory workaround until tag issue is fixed.
      // See b/339529777 for extra details.
      char* x = reinterpret_cast<char*>(&x + 1);
      *x = ~*x;
    }
    *p = ~*p;
  }
#else
  modify_stack_protector_test();
#endif
}
