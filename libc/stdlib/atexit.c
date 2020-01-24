/*	$OpenBSD: atexit.c,v 1.20 2014/07/11 09:51:37 kettenis Exp $ */
/*
 * Copyright (c) 2002 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "atexit.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>

static pthread_mutex_t g_atexit_lock = PTHREAD_MUTEX_INITIALIZER;
#define _ATEXIT_LOCK() pthread_mutex_lock(&g_atexit_lock)
#define _ATEXIT_UNLOCK() pthread_mutex_unlock(&g_atexit_lock)

struct atexit {
	struct atexit *next;		/* next in list */
	int ind;			/* next index in this table */
	int max;			/* max entries >= ATEXIT_SIZE */
	struct atexit_fn {
		void (*fn_ptr)(void *);
		void *fn_arg;		/* argument for CXA callback */
		void *fn_dso;		/* shared module handle */
	} fns[1];			/* the table itself */
};

static struct atexit *__atexit;
static int restartloop;

/* BEGIN android-changed: __unregister_atfork is used by __cxa_finalize */
extern void __unregister_atfork(void* dso);
/* END android-changed */

/*
 * Function pointers are stored in a linked list of pages. The list
 * is initially empty, and pages are allocated on demand. The first
 * function pointer in the first allocated page (the last one in
 * the linked list) is reserved for the cleanup function.
 *
 * Outside the following functions, all pages are mprotect()'ed
 * to prevent unintentional/malicious corruption.
 */

/*
 * Register a function to be performed at exit or when a shared object
 * with the given dso handle is unloaded dynamically.  Also used as
 * the backend for atexit().  For more info on this API, see:
 *
 *	http://www.codesourcery.com/cxx-abi/abi.html#dso-dtor
 */
int
__cxa_atexit(void (*func)(void *), void *arg, void *dso)
{
	struct atexit_fn *fnp = NULL;
	size_t pgsize = getpagesize();
	int ret = -1, n;

	if (pgsize < sizeof(struct atexit))
		return (-1);
	_ATEXIT_LOCK();
	struct atexit *p = __atexit, *non_full_p = NULL;

	/*
	 * Find already executed atexit function slot to reuse.
	 * Only the empty slots at the very end of the list are viable,
	 * choising any other would mean different callback order.
	 *
	 * Additionally: Do not reuse the very last function-pointer
	 * which is reserved for the cleanup function.
	 */
	for (; p != NULL; p = p->next) {
		for (n = p->ind; --n >= 0;) {
			if (p->fns[n].fn_ptr != NULL || (p->next == NULL && n == 0)) {
				goto reuse_slot;
			} else {
				fnp = &p->fns[n];
				non_full_p = p;
			}
		}
	}

	/*
	 * If we found a slot to reuse then unlock the page and
	 * skip to the registration.
	 */
reuse_slot:
	if (fnp != NULL) {
		p = non_full_p;
		if (mprotect(p, pgsize, PROT_READ | PROT_WRITE))
			goto unlock;
		goto registration;
	}

	/*
	 * Reserve new slot if available, allocate new page otherwise.
	 */
	p = __atexit;
	if (p != NULL) {
		if (p->ind + 1 >= p->max)
			p = NULL;
		else if (mprotect(p, pgsize, PROT_READ | PROT_WRITE))
			goto unlock;
	}
	if (p == NULL) {
		p = mmap(NULL, pgsize, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED)
			goto unlock;
/* BEGIN android-changed */
		prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, p, pgsize,
		    "atexit handlers");
/* END android-changed */
		if (__atexit == NULL) {
			memset(&p->fns[0], 0, sizeof(p->fns[0]));
			p->ind = 1;
		} else
			p->ind = 0;
		p->max = (pgsize - ((char *)&p->fns[0] - (char *)p)) /
		    sizeof(p->fns[0]);
		p->next = __atexit;
		__atexit = p;
	}
	fnp = &p->fns[p->ind++];

registration:
	fnp->fn_ptr = func;
	fnp->fn_arg = arg;
	fnp->fn_dso = dso;
	if (mprotect(p, pgsize, PROT_READ))
		goto unlock;
	restartloop = 1;
	ret = 0;
unlock:
	_ATEXIT_UNLOCK();
	return (ret);
}

/*
 * Call all handlers registered with __cxa_atexit() for the shared
 * object owning 'dso'.
 * Note: if 'dso' is NULL, then all remaining handlers are called.
 */
void
__cxa_finalize(void *dso)
{
	struct atexit *p, *prev_p, *q;
	struct atexit_fn fn;
	int n, pgsize = getpagesize();
	static int call_depth;

	_ATEXIT_LOCK();
	call_depth++;

restart:
	restartloop = 0;
	for (p = __atexit; p != NULL; p = p->next) {
		for (n = p->ind; --n >= 0;) {
			if (p->fns[n].fn_ptr == NULL)
				continue;	/* already called */
			if (dso != NULL && dso != p->fns[n].fn_dso)
				continue;	/* wrong DSO */

			/*
			 * Mark handler as having been already called to avoid
			 * dupes and loops, then call the appropriate function.
			 */
			fn = p->fns[n];
			if (mprotect(p, pgsize, PROT_READ | PROT_WRITE) == 0) {
				p->fns[n].fn_ptr = NULL;
				mprotect(p, pgsize, PROT_READ);
			}
			_ATEXIT_UNLOCK();
			(*fn.fn_ptr)(fn.fn_arg);
			_ATEXIT_LOCK();
			if (restartloop)
				goto restart;
		}
	}

	call_depth--;

	/*
	 * If called via exit(), unmap the pages since we have now run
	 * all the handlers.
	 *
	 * Otherwise we finished runninng handlers of a single DSO.
	 * In this case we can try to find newly empty pages that can
	 * be unmapped.
	 *
	 * We defer this until calldepth == 0 so that we don't unmap
	 * things prematurely if called recursively.
	 */
	if (call_depth == 0) {
		if (dso == NULL) {
			for (p = __atexit; p != NULL; ) {
				q = p;
				p = p->next;
				munmap(q, pgsize);
			}
			__atexit = NULL;
		} else {
			/*
			 * Go through all but the last page - it contains
			 * a function-pointer reserved for the cleanup function
			 */
			prev_p = NULL;
			p = __atexit;
			while(p != NULL && p->next != NULL) {
				/*
				 * Check if there are any non-NULL handlers
				 * in the current page.
				 */
				for (n = p->ind; --n >= 0;) {
					if (p->fns[n].fn_ptr != NULL)
						break;
				}

				/*
				 * If there aren't, then this page can be
				 * removed from the list and unmapped.
				 * Otherwise proceed to the next page.
				 */
				if (n < 0) {
					if (prev_p != NULL) {
						/*
						 * If we are unable to unlock the page then give up
						 * trying to reclaim memory and continue with the list
						 * still in a consistent state.
						 */
						if (mprotect(prev_p, pgsize, PROT_READ | PROT_WRITE) == 0) {
							prev_p->next = p->next;
							mprotect(prev_p, pgsize, PROT_READ);
						}
						else
							goto unlock;
					}

					if(p == __atexit)
						__atexit = p->next;

					q = p;
					p = p->next;
					munmap(q, pgsize);
				}
				else
				{
					prev_p = p;
					p = p->next;
				}
			}
		}
	}
unlock:
	_ATEXIT_UNLOCK();

	/* If called via exit(), flush output of all open files. */
	if (dso == NULL) {
		extern void __libc_stdio_cleanup(void);
		__libc_stdio_cleanup();
	}

  /* BEGIN android-changed: call __unregister_atfork if dso is not null */
  if (dso != NULL) {
    __unregister_atfork(dso);
  }
  /* END android-changed */
}
