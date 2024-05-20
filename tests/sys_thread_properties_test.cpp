/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "gtest_globals.h"
#include "utils.h"

TEST(sys_thread_properties_test, iterate_dts) {
#if defined(__BIONIC__)
  std::string helper = GetTestLibRoot() + "/tls_properties_helper";
  ExecTestHelper eth;
  eth.SetArgs({helper.c_str(), nullptr});
  eth.Run([&]() { execve(helper.c_str(), eth.GetArgs(), eth.GetEnv()); }, 0,
          "got test_static_tls_bounds\n"
          "iterate_cb i = 0\n"
          "done_iterate_dynamic_tls\n");
#endif
}

TEST(sys_thread_properties_test, thread_exit_cb) {
#if defined(__BIONIC__)
  std::string helper = GetTestLibRoot() + "/thread_exit_cb_helper";
  ExecTestHelper eth;
  eth.SetArgs({helper.c_str(), nullptr});
  eth.Run([&]() { execve(helper.c_str(), eth.GetArgs(), eth.GetEnv()); }, 0,
          "<main><t1>A<cb3><cb2><cb1><t2>B<cb3><cb2><cb1><exit>");
#endif
}
