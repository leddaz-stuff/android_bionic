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

#include <wctype.h>

#include <numeric>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>
#include "util.h"

void BM_wctype(benchmark::State& state, std::function<wint_t(wint_t)> fn, std::vector<wint_t>& chars) {
  size_t sum = 0;
  for (auto _ : state) {
    for (char ch : chars) {
      sum += fn(ch) ? 1 : 0;
    }
  }
  benchmark::DoNotOptimize(sum);
  state.SetBytesProcessed(state.iterations() * chars.size());
}

static std::vector<wint_t> RandomAscii() {
  std::vector<wint_t> result{128};
  std::iota(result.begin(), result.end(), 0);
  std::shuffle(result.begin(), result.end(), std::mt19937{std::random_device{}()});
  return result;
}

static std::vector<wint_t> RandomNonAscii() {
  std::vector<wint_t> result;
  std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<> d(0x80, 0xffff);
  for (size_t i = 0; i < result.size(); i++) result.push_back(d(rng));
  return result;
}

template <int(*fn)(wint_t)> void BM_wctype_ascii(benchmark::State& state) {
  auto chars = RandomAscii();
  BM_wctype(state, fn, chars);
}

template <wint_t(*fn)(wint_t)> void BM_wctype_ascii_transform(benchmark::State& state) {
  auto chars = RandomAscii();
  BM_wctype(state, fn, chars);
}

template <int(*fn)(wint_t)> void BM_wctype_non_ascii(benchmark::State& state) {
  auto chars = RandomNonAscii();
  BM_wctype(state, fn, chars);
}

template <wint_t(*fn)(wint_t)> void BM_wctype_non_ascii_transform(benchmark::State& state) {
  auto chars = RandomNonAscii();
  BM_wctype(state, fn, chars);
}

BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswalnum);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswalpha);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswblank);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswcntrl);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswdigit);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswgraph);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswlower);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswprint);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswpunct);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswspace);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswupper);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii, iswxdigit);

BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii_transform, towlower);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_ascii_transform, towupper);

BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswalnum);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswalpha);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswblank);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswcntrl);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswdigit);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswgraph);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswlower);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswprint);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswpunct);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswspace);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswupper);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii, iswxdigit);

BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii_transform, towlower);
BIONIC_BENCHMARK_TEMPLATE(BM_wctype_non_ascii_transform, towupper);
