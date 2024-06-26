/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <ctype.h>

#include <array>
#include <numeric>
#include <random>

#include <benchmark/benchmark.h>
#include "util.h"

static std::array<int, 128> RandomAscii() {
  std::array<int, 128> result;
  std::iota(result.begin(), result.end(), 0);
  std::shuffle(result.begin(), result.end(), std::mt19937{std::random_device{}()});
  return result;
}

template <int(*fn)(int)> void BM_ctype(benchmark::State& state) {
  auto chars = RandomAscii();
  size_t sum = 0;
  for (auto _ : state) {
    for (char ch : chars) {
      sum += fn(ch) ? 1 : 0;
    }
  }
  benchmark::DoNotOptimize(sum);
  state.SetBytesProcessed(state.iterations() * chars.size());
}

BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isalpha);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isalnum);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isascii);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isblank);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, iscntrl);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isgraph);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, islower);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isprint);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, ispunct);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isspace);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isupper);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, isxdigit);

BIONIC_BENCHMARK_TEMPLATE(BM_ctype, toascii);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, tolower);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, _tolower);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, toupper);
BIONIC_BENCHMARK_TEMPLATE(BM_ctype, _toupper);
