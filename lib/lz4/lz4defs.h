/*
 * lz4defs.h -- architecture specific defines
 *
 * Copyright (C) 2013, LG Electronics, Kyungsik Lee <kyungsik.lee@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Detects 64 bits mode
 */
#if defined(CONFIG_64BIT)
#define LZ4_ARCH64 1
#else
#define LZ4_ARCH64 0
#endif

/*
 * Architecture-specific macros
 */
#define BYTE	u8
typedef struct _U16_S { u16 v; } U16_S;
typedef struct _U32_S { u32 v; } U32_S;
typedef struct _U64_S { u64 v; } U64_S;
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)

#define A16(x) (((U16_S *)(x))->v)
#define A32(x) (((U32_S *)(x))->v)
#define A64(x) (((U64_S *)(x))->v)

#define PUT4(s, d) (A32(d) = A32(s))
#define PUT8(s, d) (A64(d) = A64(s))

#define LZ4_READ_LITTLEENDIAN_16(d, s, p)	\
	(d = s - A16(p))

#define LZ4_WRITE_LITTLEENDIAN_16(p, v)	\
	do {	\
		A16(p) = v; \
		p += 2; \
	} while (0)
#else /* CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS */

#define A64(x) get_unaligned((u64 *)&(((U16_S *)(x))->v))
#define A32(x) get_unaligned((u32 *)&(((U16_S *)(x))->v))
#define A16(x) get_unaligned((u16 *)&(((U16_S *)(x))->v))

#define PUT4(s, d) \
	put_unaligned(get_unaligned((const u32 *) s), (u32 *) d)
#define PUT8(s, d) \
	put_unaligned(get_unaligned((const u64 *) s), (u64 *) d)

#define LZ4_READ_LITTLEENDIAN_16(d, s, p)	\
	(d = s - get_unaligned_le16(p))

#define LZ4_WRITE_LITTLEENDIAN_16(p, v)			\
	do {						\
		put_unaligned_le16(v, (u16 *)(p));	\
		p += 2;					\
	} while (0)
#endif

#define COPYLENGTH 8
#define ML_BITS  4
#define ML_MASK  ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

/*-************************************
 *	Reading and writing into memory
 **************************************/
static FORCE_INLINE U16 LZ4_read16(const void *ptr)
{
	return get_unaligned((const U16 *)ptr);
}

static FORCE_INLINE U32 LZ4_read32(const void *ptr)
{
	return get_unaligned((const U32 *)ptr);
}

static FORCE_INLINE size_t LZ4_read_ARCH(const void *ptr)
{
	return get_unaligned((const size_t *)ptr);
}

static FORCE_INLINE void LZ4_write16(void *memPtr, U16 value)
{
	put_unaligned(value, (U16 *)memPtr);
}

static FORCE_INLINE void LZ4_write32(void *memPtr, U32 value)
{
	put_unaligned(value, (U32 *)memPtr);
}

static FORCE_INLINE U16 LZ4_readLE16(const void *memPtr)
{
	return get_unaligned_le16(memPtr);
}

static FORCE_INLINE void LZ4_writeLE16(void *memPtr, U16 value)
{
	return put_unaligned_le16(value, memPtr);
}

/*
 * LZ4 relies on memcpy with a constant size being inlined. In freestanding
 * environments, the compiler can't assume the implementation of memcpy() is
 * standard compliant, so apply its specialized memcpy() inlining logic. When
 * possible, use __builtin_memcpy() to tell the compiler to analyze memcpy()
 * as-if it were standard compliant, so it can inline it in freestanding
 * environments. This is needed when decompressing the Linux Kernel, for example.
 */
#define LZ4_memcpy(dst, src, size) __builtin_memcpy(dst, src, size)

static FORCE_INLINE void LZ4_copy8(void *dst, const void *src)
{
#if LZ4_ARCH64
	U64 a = get_unaligned((const U64 *)src);

	put_unaligned(a, (U64 *)dst);
#else
#define LZ4_NBCOMMONBYTES(val) (__builtin_ctzll(val) >> 3)
#endif

#else	/* 32-bit */
#define STEPSIZE 4

#define LZ4_COPYSTEP(s, d)	\
	do {			\
		PUT4(s, d);	\
		d += 4;		\
		s += 4;		\
	} while (0)

#define LZ4_COPYPACKET(s, d)		\
	do {				\
		LZ4_COPYSTEP(s, d);	\
		LZ4_COPYSTEP(s, d);	\
	} while (0)

#define LZ4_SECURECOPY	LZ4_WILDCOPY
#define HTYPE const u8*

#ifdef __BIG_ENDIAN
#define LZ4_NBCOMMONBYTES(val) (__builtin_clz(val) >> 3)
#else
#define LZ4_NBCOMMONBYTES(val) (__builtin_ctz(val) >> 3)
#endif

#endif

#define LZ4_WILDCOPY(s, d, e)		\
	do {				\
		LZ4_COPYPACKET(s, d);	\
	} while (d < e)

#define LZ4_BLINDCOPY(s, d, l)	\
	do {	\
		u8 *e = (d) + l;	\
		LZ4_WILDCOPY(s, d, e);	\
		d = e;	\
	} while (0)
