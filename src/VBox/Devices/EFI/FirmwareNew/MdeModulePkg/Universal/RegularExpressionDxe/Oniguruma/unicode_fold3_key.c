/* This file was converted by gperf_fold_key_conv.py
      from gperf output file. */
/* ANSI-C code produced by gperf version 3.1 */
/* Command-line: /usr/local/bin/gperf -n -C -T -c -t -j1 -L ANSI-C -F,-1 -N onigenc_unicode_fold3_key unicode_fold3_key.gperf  */
/* Computed positions: -k'3,6,9' */



/* This gperf source file was generated by make_unicode_fold_data.py */

/*-
 * Copyright (c) 2017-2018  K.Kosako  <sndgk393 AT ybb DOT ne DOT jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
//#include <string.h>
#include "regenc.h"

#define TOTAL_KEYWORDS 14
#define MIN_WORD_LENGTH 9
#define MAX_WORD_LENGTH 9
#define MIN_HASH_VALUE 0
#define MAX_HASH_VALUE 13
/* maximum key range = 14, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
/*ARGSUSED*/
static unsigned int
hash(OnigCodePoint codes[])
{
  static const unsigned char asso_values[] =
    {
       6,  3, 14, 14, 14, 14, 14, 14,  1, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14,  0,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14,  0, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14,  4, 14, 14,  5, 14, 14,  4, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 10, 14, 14,
      14, 14, 14,  9, 14,  1, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14,  0, 14, 14,
      14,  8, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14
    };
  return asso_values[(unsigned char)onig_codes_byte_at(codes, 8)] + asso_values[(unsigned char)onig_codes_byte_at(codes, 5)] + asso_values[(unsigned char)onig_codes_byte_at(codes, 2)];
}

int
onigenc_unicode_fold3_key(OnigCodePoint codes[])
{
  static const short int wordlist[] =
    {

      62,

      47,

      31,

      57,

      41,

      25,

      52,

      36,

      20,

      67,

      15,

      10,

      5,

      0
    };

  if (0 == 0)
    {
      int key = hash(codes);

      if (key <= MAX_HASH_VALUE)
        {
          int index = wordlist[key];

          if (index >= 0 && onig_codes_cmp(codes, OnigUnicodeFolds3 + index, 3) == 0)
            return index;
        }
    }
  return -1;
}
