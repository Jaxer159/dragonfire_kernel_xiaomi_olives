/*
 * LZ4 Decompressor for Linux kernel
 *
 * Copyright (C) 2013, LG Electronics, Kyungsik Lee <kyungsik.lee@lge.com>
 *
 * Based on LZ4 implementation by Yann Collet.
 *
 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011-2012, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You can contact the author at :
 *  - LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
 *  - LZ4 source repository : http://code.google.com/p/lz4/
 */

#ifndef STATIC
#include <linux/module.h>
#include <linux/kernel.h>
#endif
#include <linux/lz4.h>

#include <asm/unaligned.h>

#include "lz4defs.h"

static const int dec32table[] = {0, 3, 2, 3, 0, 0, 0, 0};
#if LZ4_ARCH64
static const int dec64table[] = {0, 0, 0, -1, 0, 1, 2, 3};
#endif

static int lz4_uncompress(const char *source, char *dest, int osize)
{
	const BYTE *ip = (const BYTE *) source;
	const BYTE *ref;
	BYTE *op = (BYTE *) dest;
	BYTE * const oend = op + osize;
	BYTE *cpy;
	unsigned token;
	size_t length;

	while (1) {
		size_t length;
		const BYTE *match;
		size_t offset;

		/* get literal length */
		unsigned int const token = *ip++;
		length = token>>ML_BITS;

		/* ip < iend before the increment */
		assert(!endOnInput || ip <= iend);

		/*
		 * A two-stage shortcut for the most common case:
		 * 1) If the literal length is 0..14, and there is enough
		 * space, enter the shortcut and copy 16 bytes on behalf
		 * of the literals (in the fast mode, only 8 bytes can be
		 * safely copied this way).
		 * 2) Further if the match length is 4..18, copy 18 bytes
		 * in a similar manner; but we ensure that there's enough
		 * space in the output for those 18 bytes earlier, upon
		 * entering the shortcut (in other words, there is a
		 * combined check for both stages).
		 *
		 * The & in the likely() below is intentionally not && so that
		 * some compilers can produce better parallelized runtime code
		 */
		if ((endOnInput ? length != RUN_MASK : length <= 8)
		   /*
		    * strictly "less than" on input, to re-enter
		    * the loop with at least one byte
		    */
		   && likely((endOnInput ? ip < shortiend : 1) &
			     (op <= shortoend))) {
			/* Copy the literals */
			LZ4_memcpy(op, ip, endOnInput ? 16 : 8);
			op += length; ip += length;

			/*
			 * The second stage:
			 * prepare for match copying, decode full info.
			 * If it doesn't work out, the info won't be wasted.
			 */
			length = token & ML_MASK; /* match length */
			offset = LZ4_readLE16(ip);
			ip += 2;
			match = op - offset;
			assert(match <= op); /* check overflow */

			/* Do not deal with overlapping matches. */
			if ((length != ML_MASK) &&
			    (offset >= 8) &&
			    (dict == withPrefix64k || match >= lowPrefix)) {
				/* Copy the match. */
				LZ4_memcpy(op + 0, match + 0, 8);
				LZ4_memcpy(op + 8, match + 8, 8);
				LZ4_memcpy(op + 16, match + 16, 2);
				op += length + MINMATCH;
				/* Both stages worked, load the next token. */
				continue;
			}

			/*
			 * The second stage didn't work out, but the info
			 * is ready. Propel it right to the point of match
			 * copying.
			 */
			goto _copy_match;
		}

		/* get runlength */
		token = *ip++;
		length = (token >> ML_BITS);
		if (length == RUN_MASK) {
			size_t len;

			len = *ip++;
			for (; len == 255; length += 255)
				len = *ip++;
			if (unlikely(length > (size_t)(length + len)))
				goto _output_error;
			length += len;
		}

		/* copy literals */
		cpy = op + length;
		if (unlikely(cpy > oend - COPYLENGTH)) {
			/*
			 * Error: not enough place for another match
			 * (min 4) + 5 literals
			 */
			if (cpy != oend)
				goto _output_error;

			LZ4_memcpy(op, ip, length);
			ip += length;
			break; /* EOF */
		}
		LZ4_WILDCOPY(ip, op, cpy);
		ip -= (op - cpy);
		op = cpy;

		/* get offset */
		LZ4_READ_LITTLEENDIAN_16(ref, cpy, ip);
		ip += 2;

		/* Error: offset create reference outside destination buffer */
		if (unlikely(ref < (BYTE *const) dest))
			goto _output_error;

		/* get matchlength */
		length = token & ML_MASK;
		if (length == ML_MASK) {
			for (; *ip == 255; length += 255)
				ip++;
			if (unlikely(length > (size_t)(length + *ip)))
				goto _output_error;
			length += *ip++;
		}

		/* copy repeated sequence */
		if (unlikely((op - ref) < STEPSIZE)) {
#if LZ4_ARCH64
			int dec64 = dec64table[op - ref];
#else
			const int dec64 = 0;
#endif
			op[0] = ref[0];
			op[1] = ref[1];
			op[2] = ref[2];
			op[3] = ref[3];
			op += 4;
			ref += 4;
			ref -= dec32table[op-ref];
			PUT4(ref, op);
			op += STEPSIZE - 4;
			ref -= dec64;
		} else {
			LZ4_COPYSTEP(ref, op);
		}
		cpy = op + length - (STEPSIZE - 4);
		if (cpy > (oend - COPYLENGTH)) {

			/* Error: request to write beyond destination buffer */
			if (cpy > oend)
				goto _output_error;
#if LZ4_ARCH64
			if ((ref + COPYLENGTH) > oend)
#else
			if ((ref + COPYLENGTH) > oend ||
					(op + COPYLENGTH) > oend)
#endif
				goto _output_error;
			LZ4_SECURECOPY(ref, op, (oend - COPYLENGTH));
			while (op < cpy)
				*op++ = *ref++;
			op = cpy;
			/*
			 * Check EOF (should never happen, since last 5 bytes
			 * are supposed to be literals)
			 */
			if (op == oend)
				goto _output_error;
			continue;
		}
		LZ4_SECURECOPY(ref, op, cpy);
		op = cpy; /* correction */
	}
	/* end of decoding */
	return (int) (((char *)ip) - source);

	/* write overflow error detected */
_output_error:
	return -1;
}

static int lz4_uncompress_unknownoutputsize(const char *source, char *dest,
				int isize, size_t maxoutputsize)
{
	const BYTE *ip = (const BYTE *) source;
	const BYTE *const iend = ip + isize;
	const BYTE *ref;


	BYTE *op = (BYTE *) dest;
	BYTE * const oend = op + maxoutputsize;
	BYTE *cpy;

	/* Main Loop */
	while (ip < iend) {

		unsigned token;
		size_t length;

		/* get runlength */
		token = *ip++;
		length = (token >> ML_BITS);
		if (length == RUN_MASK) {
			int s = 255;
			while ((ip < iend) && (s == 255)) {
				s = *ip++;
				if (unlikely(length > (size_t)(length + s)))
					goto _output_error;
				length = min(length, (size_t)(oend - op));
			}

			if (length <= (size_t)(lowPrefix - match)) {
				/*
				 * match fits entirely within external
				 * dictionary : just copy
				 */
				memmove(op, dictEnd - (lowPrefix - match),
					length);
				op += length;
			} else {
				/*
				 * match stretches into both external
				 * dictionary and current block
				 */
				size_t const copySize = (size_t)(lowPrefix - match);
				size_t const restSize = length - copySize;

				LZ4_memcpy(op, dictEnd - copySize, copySize);
				op += copySize;
				if (restSize > (size_t)(op - lowPrefix)) {
					/* overlap copy */
					BYTE * const endOfMatch = op + restSize;
					const BYTE *copyFrom = lowPrefix;

					while (op < endOfMatch)
						*op++ = *copyFrom++;
				} else {
					LZ4_memcpy(op, lowPrefix, restSize);
					op += restSize;
				}
			}
		}
		/* copy literals */
		cpy = op + length;
		if ((cpy > oend - COPYLENGTH) ||
			(ip + length > iend - COPYLENGTH)) {

			if (cpy > oend)
				goto _output_error;/* writes beyond buffer */

			if (ip + length != iend)
				goto _output_error;/*
						    * Error: LZ4 format requires
						    * to consume all input
						    * at this stage
						    */
			memcpy(op, ip, length);
			op += length;
			break;/* Necessarily EOF, due to parsing restrictions */
		}
		LZ4_WILDCOPY(ip, op, cpy);
		ip -= (op - cpy);
		op = cpy;

		/*
		 * partialDecoding :
		 * may not respect endBlock parsing restrictions
		 */
		assert(op <= oend);
		if (partialDecoding &&
		    (cpy > oend - MATCH_SAFEGUARD_DISTANCE)) {
			size_t const mlen = min(length, (size_t)(oend - op));
			const BYTE * const matchEnd = match + mlen;
			BYTE * const copyEnd = op + mlen;

			if (matchEnd > op) {
				/* overlap copy */
				while (op < copyEnd)
					*op++ = *match++;
			} else {
				LZ4_memcpy(op, match, mlen);
			}
			op = copyEnd;
			if (op == oend)
				break;
			}
		}

		if (unlikely(offset < 8)) {
			op[0] = match[0];
			op[1] = match[1];
			op[2] = match[2];
			op[3] = match[3];
			match += inc32table[offset];
			LZ4_memcpy(op + 4, match, 4);
			match -= dec64table[offset];
		} else {
			LZ4_COPYSTEP(ref, op);
		}
		cpy = op + length - (STEPSIZE-4);
		if (cpy > oend - COPYLENGTH) {
			if (cpy > oend)
				goto _output_error; /* write outside of buf */
#if LZ4_ARCH64
			if ((ref + COPYLENGTH) > oend)
#else
			if ((ref + COPYLENGTH) > oend ||
					(op + COPYLENGTH) > oend)
#endif
				goto _output_error;
			LZ4_SECURECOPY(ref, op, (oend - COPYLENGTH));
			while (op < cpy)
				*op++ = *ref++;
			op = cpy;
			/*
			 * Check EOF (should never happen, since last 5 bytes
			 * are supposed to be literals)
			 */
			if (op == oend)
				goto _output_error;
			continue;
		}
		LZ4_SECURECOPY(ref, op, cpy);
		op = cpy; /* correction */
	}
	/* end of decoding */
	return (int) (((char *) op) - dest);

	/* write overflow error detected */
_output_error:
	return -1;
}

int lz4_decompress(const unsigned char *src, size_t *src_len,
		unsigned char *dest, size_t actual_dest_len)
{
	int ret = -1;
	int input_len = 0;

	input_len = lz4_uncompress(src, dest, actual_dest_len);
	if (input_len < 0)
		goto exit_0;
	*src_len = input_len;

	return 0;
exit_0:
	return ret;
}
#ifndef STATIC
EXPORT_SYMBOL(lz4_decompress);
#endif

int lz4_decompress_unknownoutputsize(const unsigned char *src, size_t src_len,
		unsigned char *dest, size_t *dest_len)
{
	int ret = -1;
	int out_len = 0;

	out_len = lz4_uncompress_unknownoutputsize(src, dest, src_len,
					*dest_len);
	if (out_len < 0)
		goto exit_0;
	*dest_len = out_len;

	return 0;
exit_0:
	return ret;
}
#ifndef STATIC
EXPORT_SYMBOL(lz4_decompress_unknownoutputsize);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("LZ4 Decompressor");
#endif
