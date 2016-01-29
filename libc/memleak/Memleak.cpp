/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <inttypes.h>

#include <functional>
#include <iomanip>
#include <mutex>
#include <string>
#include <sstream>

#include "bionic/jemalloc.h"
#include "private/bionic_macros.h"
#include "private/ScopeGuard.h"

#include "Allocator.h"
#include "HeapWalker.h"
#include "LeakPipe.h"
#include "Memleak.h"
#include "ProcessMappings.h"
#include "PtracerThread.h"
#include "ScopedDisableMalloc.h"
#include "Semaphore.h"
#include "ThreadCapture.h"

#include "log.h"

const size_t Leak::contents_length;

using namespace std::chrono_literals;

class Memleak {
 public:
  Memleak(pid_t pid, Allocator<void> allocator) : pid_(pid), allocator_(allocator),
      heap_walker_(allocator_) {}
  bool CollectAllocations(const allocator::vector<ThreadInfo>& threads,
      const allocator::vector<Mapping>& mappings);
  bool DetectLeaks(allocator::vector<Leak>& leaks, size_t limit,
      size_t* num_leaks, size_t* leak_bytes);
  size_t Allocations() { return heap_walker_.Allocations(); }
  size_t AllocationBytes() { return heap_walker_.AllocationBytes(); }
 private:
  bool ClassifyMappings(const allocator::vector<Mapping>& mappings,
      allocator::vector<Mapping>& heap_mappings,
      allocator::vector<Mapping>& anon_mappings,
      allocator::vector<Mapping>& globals_mappings,
      allocator::vector<Mapping>& stack_mappings);
  DISALLOW_COPY_AND_ASSIGN(Memleak);
  pid_t pid_;
  Allocator<void> allocator_;
  HeapWalker heap_walker_;
};

static void HeapIterate(const Mapping& heap_mapping,
    const std::function<void(uintptr_t, size_t)>& func) {
  je_iterate(heap_mapping.begin, heap_mapping.end - heap_mapping.begin,
      [](uintptr_t base, size_t size, void* arg) {
    auto f = reinterpret_cast<const std::function<void(uintptr_t, size_t)>*>(arg);
    (*f)(base, size);
  }, const_cast<void*>(reinterpret_cast<const void*>(&func)));
}

bool Memleak::CollectAllocations(const allocator::vector<ThreadInfo>& threads,
    const allocator::vector<Mapping>& mappings) {
  log_info("searching process %d for allocations", pid_);
  allocator::vector<Mapping> heap_mappings{mappings};
  allocator::vector<Mapping> anon_mappings{mappings};
  allocator::vector<Mapping> globals_mappings{mappings};
  allocator::vector<Mapping> stack_mappings{mappings};
  if (!ClassifyMappings(mappings, heap_mappings, anon_mappings,
      globals_mappings, stack_mappings)) {
    return false;
  }

  for (auto it = heap_mappings.begin(); it != heap_mappings.end(); it++) {
    log_debug("Heap mapping %" PRIxPTR "-%" PRIxPTR, it->begin, it->end);
    HeapIterate(*it, [&](uintptr_t base, size_t size) {
      heap_walker_.Add(base, base + size);
    });
  }

  for (auto it = anon_mappings.begin(); it != anon_mappings.end(); it++) {
    log_debug("Anon mapping %" PRIxPTR "-%" PRIxPTR, it->begin, it->end);
    heap_walker_.Add(it->begin, it->end);
  }

  for (auto it = globals_mappings.begin(); it != globals_mappings.end(); it++) {
    log_debug("Globals mapping %" PRIxPTR "-%" PRIxPTR, it->begin, it->end);
    heap_walker_.Live(it->begin, it->end);
  }

  for (auto thread_it = threads.begin(); thread_it != threads.end(); thread_it++) {
    for (auto it = stack_mappings.begin(); it != stack_mappings.end(); it++) {
      if (thread_it->stack.first >= it->begin && thread_it->stack.first <= it->end) {
        log_debug("Stack %" PRIxPTR "-%" PRIxPTR, thread_it->stack.first, it->end);
        heap_walker_.Live(thread_it->stack.first, it->end);
      }
    }
  }

  log_info("searching done");

  return true;
}

bool Memleak::DetectLeaks(allocator::vector<Leak>& leaks, size_t limit,
    size_t* num_leaks, size_t* leak_bytes) {
  log_info("sweeping process %d for memory leaks", pid_);
  leaks.clear();

  allocator::vector<Range> leaked{allocator_};
  if (!heap_walker_.Leaked(leaked, limit, num_leaks, leak_bytes)) {
    return false;
  }

  for (auto it = leaked.begin(); it != leaked.end(); it++) {
    Leak leak{};
    leak.begin = it->begin;
    leak.size = it->end - it->begin;;
    memcpy(leak.contents, reinterpret_cast<void*>(it->begin),
        std::min(leak.size, Leak::contents_length));
    leaks.emplace_back(leak);
  }

  log_info("sweeping done");

  return true;
}

static bool has_prefix(const allocator::string& s, const char* prefix) {
  int ret = s.compare(0, strlen(prefix), prefix);
  return ret == 0;
}

bool Memleak::ClassifyMappings(const allocator::vector<Mapping>& mappings,
    allocator::vector<Mapping>& heap_mappings,
    allocator::vector<Mapping>& anon_mappings,
    allocator::vector<Mapping>& globals_mappings,
    allocator::vector<Mapping>& stack_mappings)
{
  heap_mappings.clear();
  anon_mappings.clear();
  globals_mappings.clear();
  stack_mappings.clear();

  allocator::string current_lib{allocator_};
  bool last_was_data_section = false;
  bool is_data_section = false;

  for (auto it = mappings.begin(); it != mappings.end(); it++) {
    last_was_data_section = is_data_section;
    is_data_section = false;

    if (it->execute) {
      current_lib = it->name;
      continue;
    }

    if (!it->read) {
      continue;
    }

    static const char* heap_mapping_name = "[anon:libc_malloc]";
    static const char* dalvik_mapping_prefix = "/dev/ashmem/dalvik";
    static const char* stack_mapping_prefix = "[stack";

    const allocator::string mapping_name{it->name, allocator_};
    if (mapping_name.size() == 0) {
      if (last_was_data_section) {
        globals_mappings.emplace_back(*it);
      } else {
        // TODO(ccross): it would be nice to treat unnamed anonymous mappings as
        // possible leaks, but calling mprotect on a BSS section page can split
        // up the BSS section and make the end look like an anonymous mapping.
        // Maybe this can be switched back once we have BSS section naming.
        globals_mappings.emplace_back(*it);
      }
    } else if (mapping_name == heap_mapping_name) {
      heap_mappings.emplace_back(*it);
    } else if (mapping_name == current_lib) {
      globals_mappings.emplace_back(*it);
      is_data_section = true;
    } else if (has_prefix(mapping_name, dalvik_mapping_prefix)) {
      globals_mappings.emplace_back(*it);
    } else if (has_prefix(mapping_name, stack_mapping_prefix)) {
      stack_mappings.emplace_back(*it);
    }
  }

  return true;
}

bool DetectLeaks(std::vector<Leak>& leaks, size_t limit, size_t* num_leaks_out,
    size_t* leak_bytes_out) {
  int parent_pid = getpid();
  int memleak_tid = gettid();

  Heap heap;

  Semaphore continue_parent_sem;
  LeakPipe pipe;

  PtracerThread thread{[&]() -> int {
    /////////////////////////////////////////////
    // Collection thread
    /////////////////////////////////////////////
    log_info("collecting thread info for process %d...", parent_pid);

    ThreadCapture thread_capture(parent_pid, heap);
    allocator::vector<ThreadInfo> thread_info(heap);
    allocator::vector<Mapping> mappings(heap);

    // ptrace all the threads
    if (!thread_capture.CaptureThreads()) {
      return 1;
    }

    // collect register contents and stacks
    if (!thread_capture.CapturedThreadInfo(thread_info)) {
      return 1;
    }

    // snapshot /proc/pid/maps
    if (!ProcessMappings(parent_pid, mappings)) {
      return 1;
    }

    // malloc must be enabled to call fork, at_fork handlers take the same
    // locks as ScopedDisableMalloc.  All threads are paused in ptrace, so
    // memory state is still consistent.  Unfreeze the original thread so it
    // can drop the malloc locks, it will block until the collection thread
    // exits.
    thread_capture.ReleaseThread(memleak_tid);
    continue_parent_sem.Post();

    // fork a process to do the heap walking
    int ret = fork();
    if (ret < 0) {
      return 1;
    } else if (ret == 0) {
      /////////////////////////////////////////////
      // Heap walker process
      /////////////////////////////////////////////
      // Examine memory state in the child using the data collected above and
      // the CoW snapshot of the process memory contents.
      ScopedDisableMalloc disable_malloc;

      if (!pipe.OpenSender()) {
        _exit(1);
      }

      Memleak memleak{parent_pid, heap};

      if (!memleak.CollectAllocations(thread_info, mappings)) {
        _exit(2);
      }
      size_t num_allocations = memleak.Allocations();
      size_t allocation_bytes = memleak.AllocationBytes();

      allocator::vector<Leak> leaks{heap};

      size_t num_leaks = 0;
      size_t leak_bytes = 0;
      bool ok = memleak.DetectLeaks(leaks, limit, &num_leaks, &leak_bytes);

      ok = ok && pipe.Sender().Send(num_allocations);
      ok = ok && pipe.Sender().Send(allocation_bytes);
      ok = ok && pipe.Sender().Send(num_leaks);
      ok = ok && pipe.Sender().Send(leak_bytes);
      ok = ok && pipe.Sender().SendVector(leaks);

      if (!ok) {
        _exit(3);
      }

      _exit(0);
    } else {
      // Nothing left to do in the collection thread, return immediately,
      // releasing all the captured threads.
      log_info("collection thread done");
      return 0;
    }
  }};

  /////////////////////////////////////////////
  // Original thread
  /////////////////////////////////////////////

  {
    // Disable malloc to get a consistent view of memory
    ScopedDisableMalloc disable_malloc;

    // Start the collection thread
    thread.Start();

    // Wait for the collection thread to signal that it is ready to fork the
    // heap walker process.
    continue_parent_sem.Wait(1s);

    // Re-enable malloc so the collection thread can fork.
  }

  // Wait for the collection thread to exit
  int ret = thread.Join();
  if (ret != 0) {
    return false;
  }

  // Get a pipe from the heap walker process.  Transferring a new pipe fd
  // ensures no other forked processes can have it open, so when the heap
  // walker process dies the remote side of the pipe will close.
  if (!pipe.OpenReceiver()) {
    return false;
  }

  size_t num_allocations = 0;
  size_t allocation_bytes = 0;
  size_t num_leaks = 0;
  size_t leak_bytes = 0;

  bool ok = true;
  ok = ok && pipe.Receiver().Receive(&num_allocations);
  ok = ok && pipe.Receiver().Receive(&allocation_bytes);
  ok = ok && pipe.Receiver().Receive(&num_leaks);
  ok = ok && pipe.Receiver().Receive(&leak_bytes);
  ok = ok && pipe.Receiver().ReceiveVector(leaks);
  if (!ok) {
    return false;
  }

  log_info("memory leak detection done");
  if (num_leaks > 0 || leak_bytes > 0) {
    log_error("%zu bytes in %zu allocation%s leaked out of %zu bytes in %zu allocation%s",
        leak_bytes, num_leaks, num_leaks == 1 ? "" : "s",
        allocation_bytes, num_allocations, num_allocations == 1 ? "" : "s");
  }

  if (num_leaks_out) {
    *num_leaks_out = num_leaks;
  }

  if (leak_bytes_out) {
    *leak_bytes_out = leak_bytes;
  }

  return true;
}

static void LogLeak(Leak& leak, bool log_contents) {
  log_error("leaked allocation at %" PRIxPTR " of approximate size %zu",
      leak.begin, leak.size);

  if (log_contents) {
    const int bytes_per_line = 16;
    const size_t bytes = std::min(leak.size, leak.contents_length);

    for (size_t i = 0; i < bytes; i += bytes_per_line) {
      std::ostringstream os;
      os << std::hex << leak.begin + i << ": ";
      size_t j;
      for (j = i; j < bytes && j < i + bytes_per_line; j++) {
        os << std::setfill('0') << std::setw(2) << static_cast<int>(leak.contents[j]) << " ";
      }
      for (; j < i + bytes_per_line; j++) {
        os << "   ";
      }
      for (j = i; j < bytes && j < i + bytes_per_line; j++) {
        char c = leak.contents[j];
        if (c < ' ' || c >= 0x7f) {
          c = '.';
        }
        os << c;
      }
      os << std::endl;
      log_error("%s", os.str().c_str());
    }
  }
}

bool DetectAndLogLeaks(bool log_contents, size_t limit) {
  std::vector<Leak> leaks;
  if (!DetectLeaks(leaks, limit)) {
    return false;
  }

  for (auto it = leaks.begin(); it != leaks.end(); it++) {
    LogLeak(*it, log_contents);
  }

  return true;
}
