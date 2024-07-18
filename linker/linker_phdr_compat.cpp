/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "linker_phdr_compat.h"
#include "linker_phdr.h"

#include "linker_dlwarning.h"
#include "linker_globals.h"
#include "linker_debug.h"

#include "platform/bionic/page.h"

#include <linux/prctl.h>  /* Definition of PR_* constants */
#include <sys/prctl.h>
#include <sys/mman.h>

static const size_t kPageSize = page_size();

static constexpr size_t kCompatPageSize = 4096;

static inline uintptr_t page_start_compat(uintptr_t x) {
  return x & ~(kCompatPageSize - 1);
}

static inline uintptr_t page_end_compat(uintptr_t x) {
  return page_start_compat(x + kCompatPageSize - 1);
}

bool loader_4kb_compat_enabled() {
  // TODO: Add the correct conditions for enabling this
  return true;
}

ElfW(Addr) min_palign(const ElfW(Phdr)* phdr_table, size_t phdr_count) {
  ElfW(Addr) min_align = UINTPTR_MAX;

  for (size_t i = 0; i < phdr_count; ++i) {
    const ElfW(Phdr)* phdr = &phdr_table[i];

    if (phdr->p_type != PT_LOAD) {
      continue;
    }

    if (phdr->p_align < min_align) {
      min_align = phdr->p_align;
    }
  }

  if (min_align == UINTPTR_MAX) {
    min_align = 0;
  }

  return min_align;
}

static inline bool has_relro_prefix(const ElfW(Phdr)* phdr, const ElfW(Phdr)* relro) {
    return relro && phdr->p_type == PT_LOAD && phdr->p_vaddr == relro->p_vaddr;
}

static const ElfW(Phdr)* relro_segment(const ElfW(Phdr)* phdr_table, size_t phdr_count) {
  for (size_t i = 0; i < phdr_count; ++i) {
    const ElfW(Phdr)* phdr = &phdr_table[i];

    if (phdr->p_type == PT_GNU_RELRO) {
      return phdr;
    }
  }

  return nullptr;
}

/*
 * Populates @vaddr with the boundary between RX|RW portions.
 *
 * Returns true if successful (there is only 1 such boundary), and false otherwise.
 */
static bool rx_rw_boundary_vaddr(const ElfW(Phdr)* phdr_table, size_t phdr_count, ElfW(Addr) *vaddr, const char *name) {
  const ElfW(Phdr)* relro_phdr = relro_segment(phdr_table, phdr_count);
  ElfW(Phdr)* last_rw = nullptr;
  ElfW(Phdr)* first_rw = nullptr;

  for (size_t i = 0; i < phdr_count; ++i) {
    const ElfW(Phdr)* curr = &phdr_table[i];
    const ElfW(Phdr)* prev = (i > 0) ? &phdr_table[i-1] : nullptr;

    if (curr->p_type != PT_LOAD) {
      continue;
    }

    int prot = PFLAGS_TO_PROT(curr->p_flags);

    if (prot & PROT_WRITE) {
      if (!first_rw) {
        first_rw = const_cast<ElfW(Phdr)*>(curr);
      } else {
        if (has_relro_prefix(curr, relro_phdr)) {
          DL_ERR_AND_LOG("\"%s\": Compat loading failed: RELRO is not in the first RW segment", name);
          return false;
        }
      }

      if (last_rw && last_rw != prev) {
        DL_ERR_AND_LOG("\"%s\": Compat loading failed: There are multiple non-adjacent RW segments", name);
        return false;
      } else {
        last_rw = const_cast<ElfW(Phdr)*>(curr);
      }
    }
  }

  if (has_relro_prefix(first_rw, relro_phdr)) {
    *vaddr = page_end_compat(relro_phdr->p_vaddr + relro_phdr->p_memsz);
  } else {
    *vaddr = page_start_compat(first_rw->p_vaddr);
  }

  return true;
}

static ElfW(Addr) perm_boundary_offset(const ElfW(Addr) vaddr) {
  ElfW(Addr) offset = page_offset(vaddr);

  return offset ? page_size() - page_offset(vaddr) : 0;
}

static inline int _phdr_table_set_gnu_relro_prot_compat(ElfW(Addr) start, ElfW(Addr) size, int prot_flags) {
  return mprotect(reinterpret_cast<void*>(start), size, prot_flags);
}

int phdr_table_protect_gnu_relro_compat(ElfW(Addr) start, ElfW(Addr) size) {
  return _phdr_table_set_gnu_relro_prot_compat(start, size, PROT_READ|PROT_EXEC);
}

bool ElfReader::LoadSegments4kbCompat() {
  std::string compat_name = name_  + " (compat loaded)";
  ElfW(Addr) perm_boundary_vaddr;

  if (!rx_rw_boundary_vaddr(phdr_table_, phdr_num_, &perm_boundary_vaddr, name_.c_str())) {
    return false;
  }

  // Adjust the load_bias to position the RX|RW boundary on a page boundary
  load_bias_ += perm_boundary_offset(perm_boundary_vaddr);

  // Make .data ... .bss region RW only (not X)
  ElfW(Addr) rw_start = load_bias_ + perm_boundary_vaddr;
  ElfW(Addr) rw_size = load_size_ - (rw_start - reinterpret_cast<ElfW(Addr)>(load_start_));

  CHECK(rw_start % kPageSize == 0);
  CHECK(rw_size % kPageSize == 0);

  // Label the RW portion of the mapping
  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, rw_start, rw_size, compat_name.c_str());

  // Save the compat RELRO (.text ... .data.relro) section limits
  compat_relro_start_ = reinterpret_cast<ElfW(Addr)>(load_start_);
  compat_relro_size_ = load_size_ - rw_size;

  // NOTE: The legacy page size (4096) must be used when aligning the legacy segments for loading (reading).
  // The larger 16kB page size will lead to overwriting adjacent segments due to larger-page alignment.
  for (size_t i = 0; i < phdr_num_; ++i) {
    const ElfW(Phdr)* phdr = &phdr_table_[i];

    if (phdr->p_type != PT_LOAD) {
      continue;
    }

    // Segment addresses in memory.
    ElfW(Addr) seg_start = phdr->p_vaddr + load_bias_;
    ElfW(Addr) seg_end = seg_start + phdr->p_memsz;

    ElfW(Addr) seg_page_start = page_start_compat(seg_start);
    ElfW(Addr) seg_page_end = page_end_compat(seg_end);

    ElfW(Addr) seg_file_end = seg_start + phdr->p_filesz;

    // File offsets.
    ElfW(Addr) file_start = phdr->p_offset;
    ElfW(Addr) file_end = file_start + phdr->p_filesz;

    ElfW(Addr) file_page_start = page_start_compat(file_start);
    ElfW(Addr) file_length = file_end - file_page_start;

    if (file_size_ <= 0) {
      DL_ERR("\"%s\" invalid file size: %" PRId64, name_.c_str(), file_size_);
      return false;
    }

    if (file_start + phdr->p_filesz > static_cast<size_t>(file_size_)) {
      DL_ERR("invalid ELF file \"%s\" load segment[%zd]:"
          " p_offset (%p) + p_filesz (%p) ( = %p) past end of file (0x%" PRIx64 ")",
          name_.c_str(), i, reinterpret_cast<void*>(phdr->p_offset),
          reinterpret_cast<void*>(phdr->p_filesz),
          reinterpret_cast<void*>(file_start + phdr->p_filesz), file_size_);
      return false;
    }

    if (file_length != 0) {
      int prot = PFLAGS_TO_PROT(phdr->p_flags);
      if ((prot & (PROT_EXEC | PROT_WRITE)) == (PROT_EXEC | PROT_WRITE)) {
        // W + E PT_LOAD segments are not allowed in O.
        if (get_application_target_sdk_version() >= 26) {
          DL_ERR_AND_LOG("\"%s\": W+E load segments are not allowed", name_.c_str());
          return false;
        }
        DL_WARN_documented_change(26,
                                  "writable-and-executable-segments-enforced-for-api-level-26",
                                  "\"%s\" has load segments that are both writable and executable",
                                  name_.c_str());
        add_dlwarning(name_.c_str(), "W+E load segments");
      }

      if (lseek(fd_, file_offset_ + file_page_start, SEEK_SET) == -1) {
        DL_ERR_AND_LOG("\"%s\" failed lseek LOAD segment %zu: %m", name_.c_str(), i);
        return false;
      }

      if (read(fd_, reinterpret_cast<void*>(seg_page_start), file_length) == -1) {
        DL_ERR_AND_LOG("\"%s\" failed read LOAD segment %zu: %m", name_.c_str(), i);
        return false;
      }
    }

    // NOTE: if the segment is writable, and does not end on a page boundary,
    // usually need to zero-fill it until the page limit; however in this case the
    // mapping is anonymous, so it is not needed.

    seg_file_end = page_end_compat(seg_file_end);

    // NOTE: We do not need to handle .bss since the mapping reservation is
    // anonymous and RW to begin with.

    // Label the RW compat mapping
    prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, load_start_, load_size_ - rw_size, compat_name.c_str());
  }

  return true;
}
