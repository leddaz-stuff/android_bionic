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
#if defined(__aarch64__) && __has_feature(hwaddress_sanitizer)
  char* p = reinterpret_cast<char*>(&p + 1);
  *p = ~*p;
  {
    // When hwasan is enabled, sometimes a tag is chosen that doesn't
    // trigger a failure. If that happens, create another frame that
    // should use a different tag.
    // See b/339529777 for extra details.
    char* x = reinterpret_cast<char*>(&x + 1);
    *x = ~*x;
  }
#else
  modify_stack_protector_test();
#endif
}
