/* fmtdytst.c: DYLAN FORMAT TEST CODE
 *
 * $Id$
 * Copyright (c) 2001-2020 Ravenbrook Limited.  See end of file for license.
 *
 * .readership: MPS developers, Dylan developers.
 */

#include "fmtdy.h"
#include "fmtdytst.h"
#include "mps.h"
#include "testlib.h"
#include "mpslib.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define unused(param)   ((void)param)


static mps_word_t *ww = NULL;
static mps_word_t *tvw;


static mps_word_t dylan_make_WV(mps_word_t version, mps_word_t vb,
                                mps_word_t es, mps_word_t vf)
{
  assert((version & ((1 << 8) - 1)) == version);
  assert((vb & ((1 << 8) - 1)) == vb);
  assert((es & ((1 << 5) - 1)) == es);
  assert((vf & ((1 << 3) - 1)) == vf);

  /* VERSION- ... VB------ reserved ES---VF- */
  return((version << (MPS_WORD_WIDTH - 8)) |
         (vb << 16) |
         (es << 3) |
         vf);
}

mps_res_t dylan_make_wrappers(void)
{
  if(ww == NULL) {
    ww = malloc(sizeof(mps_word_t) * (BASIC_WRAPPER_SIZE + 1));
    if(ww == NULL) return MPS_RES_MEMORY;
    tvw = malloc(sizeof(mps_word_t) * BASIC_WRAPPER_SIZE);
    if(tvw == NULL) {
      free(ww);
      return MPS_RES_MEMORY;
    }

    /* Build a wrapper wrapper. */
    ww[WW] = (mps_word_t)ww;
    ww[WC] = (mps_word_t)ww;     /* dummy class */
    ww[WM] = (1 << 2) | 1;       /* dummy subtype_mask */
    ww[WF] = ((WS - 1) << 2) | 2;
    ww[WV] = dylan_make_WV(2, 0, 0, 0);
    ww[WS] = (1 << 2) | 1;
    ww[WP] = 1;

    /* Build a wrapper for traceable vectors. */
    tvw[WW] = (mps_word_t)ww;
    tvw[WC] = (mps_word_t)ww;    /* dummy class */
    tvw[WM] = (1 << 2) | 1;      /* dummy subtype_mask */
    tvw[WF] = 0;                 /* no fixed part */
    tvw[WV] = dylan_make_WV(2, 0, 0, 2); /* traceable variable part */
    tvw[WS] = 1;                 /* no patterns */
  }
  return MPS_RES_OK;
}

/* dylan_init -- turn raw memory into initialised dylan-vector (or pad)
 *
 * If the raw memory is large enough, initialises it to a dylan-vector,
 * whose slots are initialised to either dylan-ints, or valid refs, at
 * random.
 *
 * Caller must supply an array of valid refs to copy, via the "refs"
 * and "nr_refs" arguments. If "nr_refs" is 0, all slots are
 * initialized to dylan-ints: this may be useful for making leaf
 * objects.
 *
 * (Makes a pad if the raw memory is too small to hold a dylan-vector)
 */

mps_res_t dylan_init(mps_addr_t addr, size_t size,
                     mps_addr_t *refs, size_t nr_refs)
{
  mps_res_t res;
  /* Make sure the size is aligned. */
  assert((size & (ALIGN-1)) == 0);

  res = dylan_make_wrappers();
  if (res != MPS_RES_OK)
    return res;

  /* If there is enough room, make a vector, otherwise just */
  /* make a padding object. */
  if(size >= sizeof(mps_word_t) * 2) {
    mps_word_t *p = (mps_word_t *)addr;
    mps_word_t i, t = (size / sizeof(mps_word_t)) - 2;

    p[0] = (mps_word_t)tvw;     /* install vector wrapper */
    p[1] = (t << 2) | 1;        /* tag the vector length */
    for(i = 0; i < t; ++i) {
      mps_word_t r = rnd();

      if(nr_refs == 0 || (r & 1))
        p[2+i] = ((r & ~(mps_word_t)3) | 1); /* random int */
      else
        p[2+i] = (mps_word_t)refs[(r >> 1) % nr_refs]; /* random ptr */
    }
  } else {
    dylan_pad(addr, size);
  }

  return MPS_RES_OK;
}

mps_res_t make_dylan_vector(mps_word_t *v, mps_ap_t ap, size_t slots)
{
  mps_res_t res;
  mps_addr_t addr;
  mps_word_t *p;
  size_t size;
  size_t i;

  res = dylan_make_wrappers();
  if (res != MPS_RES_OK)
    return res;

  size = (slots + 2) * sizeof(mps_word_t);

  do {
    MPS_RESERVE_BLOCK(res, addr, ap, size);
    if (res != MPS_RES_OK)
      return res;

    p = (mps_word_t *)addr;
    p[0] = (mps_word_t)tvw;     /* install vector wrapper */
    p[1] = (slots << 2) | 1;    /* tag the vector length */
    /* fill all slots with zero ints. */
    for (i=0; i<slots; ++i) {
      DYLAN_VECTOR_SLOT(p, i) = DYLAN_INT(0);
    }
  } while (!mps_commit(ap, addr, size));

  *v = (mps_word_t)p;
  return MPS_RES_OK;
}


void dylan_write(mps_addr_t addr, mps_addr_t *refs, size_t nr_refs)
{
  mps_word_t *p = (mps_word_t *)addr;
  mps_word_t t = p[1] >> 2;

  /* If the object is a vector, update a random entry. */
  if(p[0] == (mps_word_t)tvw && t > 0) {
    mps_word_t r = rnd();
    size_t i = 2 + (rnd() % t);

    if(r & 1)
      p[i] = ((r & ~(mps_word_t)3) | 1); /* random int */
    else
      p[i] = (mps_word_t)refs[(r >> 1) % nr_refs]; /* random ptr */
  }
}

/*  Writes to a dylan object.
    Currently just swaps two refs if it can.
    This is only used in a certain way by certain tests, it doesn't have
    to be very general. */
void dylan_mutate(mps_addr_t addr)
{
  mps_word_t *p = (mps_word_t *)addr;

  if(p[0] == (mps_word_t)tvw) {
    mps_word_t t = p[1] >> 2;

    if(t > 0) {
      mps_word_t tmp;
      size_t i, j;

      i = 2 + (rnd() % t);
      j = 2 + (rnd() % t);

      tmp = p[i];
      p[i] = p[j];
      p[j] = tmp;
    }
  }
  return;
}

mps_addr_t dylan_read(mps_addr_t addr)
{
  mps_word_t *p = (mps_word_t *)addr;

  /* If the object is a vector, return a random entry. */
  if(p[0] == (mps_word_t)tvw) {
    mps_word_t t = p[1] >> 2;
    if(t > 0)
      return (mps_addr_t)p[2 + (rnd() % t)];
  }

  return addr;
}

mps_bool_t dylan_check(mps_addr_t addr)
{
  assert(addr != 0);
  assert(((mps_word_t)addr & (ALIGN-1)) == 0);
  assert(dylan_wrapper_check((mps_word_t *)((mps_word_t *)addr)[0]));
  /* .assert.unused: Asserts throw away their conditions */
  /* in hot varieties, so UNUSED is needed. */
  unused(addr);
  return 1;
}


/* C. COPYRIGHT AND LICENSE
 *
 * Copyright (C) 2001-2020 Ravenbrook Limited <https://www.ravenbrook.com/>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
