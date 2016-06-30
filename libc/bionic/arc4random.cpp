/*
 * Copyright (c) 1996, David Mazieres <dm@uun.org>
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
 * Copyright (c) 2013, Markus Friedl <markus@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * ChaCha based random number generator for OpenBSD.
 */

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "private/arc4random_try.h"
#include "private/bionic_prctl.h"
#include "private/thread_private.h"

#define KEYSTREAM_ONLY
#include "upstream-openbsd/lib/libc/crypt/chacha_private.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

#define KEYSZ 32
#define IVSZ 8
#define BLOCKSZ 64
#define RSBUFSZ (16 * BLOCKSZ)

extern "C" int getentropy(void* buf, size_t len);

/* Marked MAP_INHERIT_ZERO, so zero'd out in fork children. */
struct _rs {
  size_t rs_have;  /* valid bytes at end of rs_buf */
  size_t rs_count; /* bytes till reseed */
} * rs;

/* Maybe be preserved in fork children, if _rs_allocate() decides. */
static struct _rsx {
  chacha_ctx rs_chacha;   /* chacha context for random keystream */
  u_char rs_buf[RSBUFSZ]; /* keystream blocks */
} * rsx;

volatile sig_atomic_t _rs_forked;

static void _rs_forkdetect(void) {
  static pid_t _rs_pid = 0;
  pid_t pid = getpid();

  if (_rs_pid == 0 || _rs_pid != pid || _rs_forked) {
    _rs_pid = pid;
    _rs_forked = 0;
    if (rs) {
      memset(rs, 0, sizeof(*rs));
    }
  }
}

static inline int _rs_allocate(struct _rs** rsp, struct _rsx** rsxp) {
  *rsp = static_cast<_rs*>(
      mmap(nullptr, sizeof(**rsp), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
  if (rsp == MAP_FAILED) {
    return -1;
  }

  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, *rsp, sizeof(**rsp), "arc4random _rs structure");

  *rsxp = static_cast<_rsx*>(
      mmap(nullptr, sizeof(**rsxp), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
  if (rsxp == MAP_FAILED) {
    munmap(*rsp, sizeof(**rsp));
    *rsp = nullptr;
    return -1;
  }

  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, *rsxp, sizeof(**rsxp), "arc4random _rsx structure");

  return 0;
}

static void _rs_rekey(u_char* dat, size_t datlen);

static void _rs_init(u_char* buf, size_t n) {
  if (n < KEYSZ + IVSZ) return;

  if (rs == NULL) {
    if (_rs_allocate(&rs, &rsx) == -1) abort();
  }

  chacha_keysetup(&rsx->rs_chacha, buf, KEYSZ * 8, 0);
  chacha_ivsetup(&rsx->rs_chacha, buf + KEYSZ);
}

static int _rs_stir(bool abort_on_fail) {
  u_char rnd[KEYSZ + IVSZ];

  if (getentropy(rnd, sizeof rnd) == -1) {
    if (!abort_on_fail) {
      return -1;
    } else {
      raise(SIGKILL);
    }
  }

  if (!rs) {
    _rs_init(rnd, sizeof(rnd));
  } else {
    _rs_rekey(rnd, sizeof(rnd));
  }
  memset(rnd, 0, sizeof(rnd)); /* discard source seed */

  /* invalidate rs_buf */
  rs->rs_have = 0;
  memset(rsx->rs_buf, 0, sizeof(rsx->rs_buf));

  rs->rs_count = 1600000;
  return 0;
}

static int _rs_stir_if_needed(size_t len, bool abort_on_fail) {
  _rs_forkdetect();
  if (!rs || rs->rs_count <= len) {
    if (_rs_stir(abort_on_fail) == -1) {
      return -1;
    }
  }
  if (rs->rs_count <= len)
    rs->rs_count = 0;
  else
    rs->rs_count -= len;
  return 0;
}

static void _rs_rekey(u_char* dat, size_t datlen) {
#ifndef KEYSTREAM_ONLY
  memset(rsx->rs_buf, 0, sizeof(rsx->rs_buf));
#endif
  /* fill rs_buf with the keystream */
  chacha_encrypt_bytes(&rsx->rs_chacha, rsx->rs_buf, rsx->rs_buf, sizeof(rsx->rs_buf));
  /* mix in optional user provided data */
  if (dat) {
    size_t i, m;

    m = min(datlen, KEYSZ + IVSZ);
    for (i = 0; i < m; i++) rsx->rs_buf[i] ^= dat[i];
  }
  /* immediately reinit for backtracking resistance */
  _rs_init(rsx->rs_buf, KEYSZ + IVSZ);
  memset(rsx->rs_buf, 0, KEYSZ + IVSZ);
  rs->rs_have = sizeof(rsx->rs_buf) - KEYSZ - IVSZ;
}

static int _rs_random_buf(void* _buf, size_t n, bool abort_on_fail) {
  u_char* buf = (u_char*)_buf;
  u_char* keystream;
  size_t m;

  if (_rs_stir_if_needed(n, abort_on_fail) == -1) return -1;
  while (n > 0) {
    if (rs->rs_have > 0) {
      m = min(n, rs->rs_have);
      keystream = rsx->rs_buf + sizeof(rsx->rs_buf) - rs->rs_have;
      memcpy(buf, keystream, m);
      memset(keystream, 0, m);
      buf += m;
      n -= m;
      rs->rs_have -= m;
    }
    if (rs->rs_have == 0) _rs_rekey(NULL, 0);
  }
  return 0;
}

static void _rs_random_u32(uint32_t* val) {
  u_char* keystream;

  _rs_stir_if_needed(sizeof(*val), 0);
  if (rs->rs_have < sizeof(*val)) _rs_rekey(NULL, 0);
  keystream = rsx->rs_buf + sizeof(rsx->rs_buf) - rs->rs_have;
  memcpy(val, keystream, sizeof(*val));
  memset(keystream, 0, sizeof(*val));
  rs->rs_have -= sizeof(*val);
}

uint32_t arc4random(void) {
  uint32_t val;

  _ARC4_LOCK();
  _rs_random_u32(&val);
  _ARC4_UNLOCK();
  return val;
}

void arc4random_buf(void* buf, size_t n) {
  _ARC4_LOCK();
  _rs_random_buf(buf, n, true);
  _ARC4_UNLOCK();
}

int arc4random_try_buf(void* buf, size_t n) {
  int result = n;

  _ARC4_LOCK();
  if (_rs_random_buf(buf, n, false) == -1) {
    result = -1;
  }
  _ARC4_UNLOCK();
  return n;
}

/*
 * Calculate a uniformly distributed random number less than upper_bound
 * avoiding "modulo bias".
 *
 * Uniformity is achieved by generating new random numbers until the one
 * returned is outside the range [0, 2**32 % upper_bound).  This
 * guarantees the selected random number will be inside
 * [2**32 % upper_bound, 2**32) which maps back to [0, upper_bound)
 * after reduction modulo upper_bound.
 */
uint32_t arc4random_uniform(uint32_t upper_bound) {
  uint32_t r, min;

  if (upper_bound < 2) return 0;

  /* 2**32 % x == (2**32 - x) % x */
  min = -upper_bound % upper_bound;

  /*
   * This could theoretically loop forever but each retry has
   * p > 0.5 (worst case, usually far better) of selecting a
   * number inside the range we need, so it should rarely need
   * to re-roll.
   */
  for (;;) {
    r = arc4random();
    if (r >= min) break;
  }

  return r % upper_bound;
}
