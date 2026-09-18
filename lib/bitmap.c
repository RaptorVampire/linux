// SPDX-License-Identifier: GPL-2.0-only
/*
 * lib/bitmap.c
 * Helper functions for bitmap.h.
 *
 * A bitmap is an array of unsigned longs holding one bit per logical
 * position. Bit numbers are little-endian within each word, so bit 0
 * lives in the LSB of word 0, bit BITS_PER_LONG-1 in its MSB, bit
 * BITS_PER_LONG in the LSB of word 1, and so on.
 *
 * All the helpers below are written to be agnostic to the number of
 * "unused" bits in the final word: BITMAP_LAST_WORD_MASK() is applied
 * whenever a trailing partial word is involved, so the caller never
 * needs to zero those bits.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/slab.h>

/**
 * DOC: bitmap introduction
 *
 * Bitmaps provide an array of bits, implemented using an array of
 * unsigned longs.  The number of valid bits in a given bitmap does
 * _not_ need to be an exact multiple of BITS_PER_LONG.
 *
 * The possible unused bits in the last, partially used word of a
 * bitmap are 'don't care'.  The implementation makes no particular
 * effort to keep them zero.  It ensures that their value will not
 * affect the results of any operation.  The bitmap operations that
 * return Boolean (bitmap_empty, for example) or scalar (bitmap_weight,
 * for example) results carefully filter out these unused bits from
 * impacting their results.
 *
 * The byte ordering of bitmaps is more natural on little endian
 * architectures.  See the big-endian headers include/asm-ppc64/bitops.h
 * and include/asm-s390/bitops.h for the best explanations of this
 * ordering.
 */

/*
 * =====================================================================
 * Equality and comparison
 * =====================================================================
 */

/**
 * __bitmap_equal - compare two bitmaps for equality
 * @bitmap1: first bitmap
 * @bitmap2: second bitmap
 * @bits: number of bits to compare
 *
 * Compares the first @bits bits of the two bitmaps. The unused bits
 * of the final partial word are ignored via BITMAP_LAST_WORD_MASK().
 *
 * Return: true if the bitmaps are identical over @bits bits.
 */
bool __bitmap_equal(const unsigned long *bitmap1,
		    const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k, lim = bits / BITS_PER_LONG;

	for (k = 0; k < lim; ++k)
		if (bitmap1[k] != bitmap2[k])
			return false;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] ^ bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return false;

	return true;
}
EXPORT_SYMBOL(__bitmap_equal);

/**
 * __bitmap_or_equal - test whether (bitmap1 | bitmap2) equals bitmap3
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bitmap3: bitmap to compare the OR against
 * @bits: number of bits to consider
 *
 * This is equivalent to computing the bitwise OR of @bitmap1 and
 * @bitmap2 into a temporary and then calling __bitmap_equal() on
 * @bitmap3, but avoids allocating or touching a scratch buffer.
 *
 * Return: true if (bitmap1 | bitmap2) == bitmap3 over @bits bits.
 */
bool __bitmap_or_equal(const unsigned long *bitmap1,
		       const unsigned long *bitmap2,
		       const unsigned long *bitmap3,
		       unsigned int bits)
{
	unsigned int k, lim = bits / BITS_PER_LONG;
	unsigned long tmp;

	for (k = 0; k < lim; ++k) {
		if ((bitmap1[k] | bitmap2[k]) != bitmap3[k])
			return false;
	}

	if (!(bits % BITS_PER_LONG))
		return true;

	tmp = (bitmap1[k] | bitmap2[k]) ^ bitmap3[k];
	return (tmp & BITMAP_LAST_WORD_MASK(bits)) == 0;
}
EXPORT_SYMBOL(__bitmap_or_equal);

/*
 * =====================================================================
 * Basic bitwise operations
 * =====================================================================
 */

/**
 * __bitmap_complement - compute the bitwise complement of a bitmap
 * @dst: destination bitmap; may be the same as @src
 * @src: source bitmap
 * @bits: number of bits
 *
 * NOTE: Unlike most bitmap helpers this one does *not* mask off the
 * trailing unused bits of the last word: every bit of @dst within
 * BITS_TO_LONGS(@bits) longs is written. Callers that care about
 * canonical zeroed padding must clear those bits themselves.
 */
void __bitmap_complement(unsigned long *dst, const unsigned long *src,
			 unsigned int bits)
{
	unsigned int k, lim = BITS_TO_LONGS(bits);

	for (k = 0; k < lim; ++k)
		dst[k] = ~src[k];
}
EXPORT_SYMBOL(__bitmap_complement);

/**
 * __bitmap_shift_right - logical right shift of the bits in a bitmap
 * @dst: destination bitmap
 * @src: source bitmap
 * @shift: shift by this many bits
 * @nbits: bitmap size, in bits
 *
 * Shifting right (dividing) means moving bits in the MS -> LS bit
 * direction.  Zeros are fed into the vacated MS positions and the
 * LS bits shifted off the bottom are lost.
 *
 * Only the first @nbits bits of @src are considered; the trailing
 * partial word is masked so that the shift never sees "garbage"
 * above bit @nbits-1.
 */
void __bitmap_shift_right(unsigned long *dst, const unsigned long *src,
			  unsigned shift, unsigned nbits)
{
	unsigned k, lim = BITS_TO_LONGS(nbits);
	unsigned off = shift / BITS_PER_LONG, rem = shift % BITS_PER_LONG;
	unsigned long mask = BITMAP_LAST_WORD_MASK(nbits);

	for (k = 0; off + k < lim; ++k) {
		unsigned long upper, lower;

		/*
		 * If shift is not word aligned, take lower rem bits of
		 * word above and make them the top rem bits of result.
		 */
		if (!rem || off + k + 1 >= lim)
			upper = 0;
		else {
			upper = src[off + k + 1];
			if (off + k + 1 == lim - 1)
				upper &= mask;
			upper <<= (BITS_PER_LONG - rem);
		}
		lower = src[off + k];
		if (off + k == lim - 1)
			lower &= mask;
		lower >>= rem;
		dst[k] = lower | upper;
	}
	if (off)
		memset(&dst[lim - off], 0, off * sizeof(unsigned long));
}
EXPORT_SYMBOL(__bitmap_shift_right);

/**
 * __bitmap_shift_left - logical left shift of the bits in a bitmap
 * @dst: destination bitmap
 * @src: source bitmap
 * @shift: shift by this many bits
 * @nbits: bitmap size, in bits
 *
 * Shifting left (multiplying) means moving bits in the LS -> MS bit
 * direction.  Zeros are fed into the vacated LS bit positions and
 * those MS bits shifted off the top are lost.
 *
 * NOTE: @k is intentionally signed so the descending loop can
 * terminate on k < 0 when @lim - @off - 1 would underflow.
 */
void __bitmap_shift_left(unsigned long *dst, const unsigned long *src,
			 unsigned int shift, unsigned int nbits)
{
	int k;
	unsigned int lim = BITS_TO_LONGS(nbits);
	unsigned int off = shift / BITS_PER_LONG, rem = shift % BITS_PER_LONG;

	for (k = lim - off - 1; k >= 0; --k) {
		unsigned long upper, lower;

		/*
		 * If shift is not word aligned, take upper rem bits of
		 * word below and make them the bottom rem bits of result.
		 */
		if (rem && k > 0)
			lower = src[k - 1] >> (BITS_PER_LONG - rem);
		else
			lower = 0;
		upper = src[k] << rem;
		dst[k + off] = lower | upper;
	}
	if (off)
		memset(dst, 0, off * sizeof(unsigned long));
}
EXPORT_SYMBOL(__bitmap_shift_left);

/**
 * bitmap_cut() - remove bit region from bitmap and right shift remaining bits
 * @dst: destination bitmap, might overlap with src
 * @src: source bitmap
 * @first: start bit of region to be removed
 * @cut: number of bits to remove
 * @nbits: bitmap size, in bits
 *
 * Set the n-th bit of @dst iff the n-th bit of @src is set and
 * n is less than @first, or the m-th bit of @src is set for any
 * m such that @first <= n < nbits, and m = n + @cut.
 *
 * In pictures, example for a big-endian 32-bit architecture:
 *
 * The @src bitmap is::
 *
 *   31                                   63
 *   |                                    |
 *   10000000 11000001 11110010 00010101  10000000 11000001 01110010 00010101
 *                   |  |              |                                    |
 *                  16  14             0                                   32
 *
 * if @cut is 3, and @first is 14, bits 14-16 in @src are cut and @dst is::
 *
 *   31                                   63
 *   |                                    |
 *   10110000 00011000 00110010 00010101  00010000 00011000 00101110 01000010
 *                      |              |                                    |
 *                      14 (bit 17     0                                   32
 *                          from @src)
 *
 * Note that @dst and @src might overlap partially or entirely.
 *
 * This is implemented in the obvious way, with a shift and carry
 * step for each moved bit. Optimisation is left as an exercise
 * for the compiler.
 */
void bitmap_cut(unsigned long *dst, const unsigned long *src,
		unsigned int first, unsigned int cut, unsigned int nbits)
{
	unsigned int len = BITS_TO_LONGS(nbits);
	unsigned long keep = 0, carry;
	int i;

	/*
	 * Preserve the bits of @src strictly below @first: they are
	 * outside the cut region and must survive the shift. We save
	 * them before memmove() overwrites the source.
	 */
	if (first % BITS_PER_LONG) {
		keep = src[first / BITS_PER_LONG] &
		       (~0UL >> (BITS_PER_LONG - first % BITS_PER_LONG));
	}

	memmove(dst, src, len * sizeof(*dst));

	/*
	 * Right-shift everything at and above @first by @cut bits, one
	 * bit at a time. The compiler is expected to fuse the loop body
	 * into a shift-and-carry chain.
	 */
	while (cut--) {
		for (i = first / BITS_PER_LONG; i < len; i++) {
			if (i < len - 1)
				carry = dst[i + 1] & 1UL;
			else
				carry = 0;

			dst[i] = (dst[i] >> 1) |
				 (carry << (BITS_PER_LONG - 1));
		}
	}

	/* Reinsert the preserved bits below @first. */
	dst[first / BITS_PER_LONG] &= ~0UL << (first % BITS_PER_LONG);
	dst[first / BITS_PER_LONG] |= keep;
}
EXPORT_SYMBOL(bitmap_cut);

/**
 * __bitmap_and - dst = bitmap1 & bitmap2
 * @dst: destination bitmap; may alias either source
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bits: number of bits
 *
 * Return: true if the result is non-zero anywhere within @bits bits.
 */
bool __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
		  const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int lim = bits / BITS_PER_LONG;
	unsigned long result = 0;

	for (k = 0; k < lim; k++)
		result |= (dst[k] = bitmap1[k] & bitmap2[k]);
	if (bits % BITS_PER_LONG)
		result |= (dst[k] = bitmap1[k] & bitmap2[k] &
			   BITMAP_LAST_WORD_MASK(bits));
	return result != 0;
}
EXPORT_SYMBOL(__bitmap_and);

/**
 * __bitmap_or - dst = bitmap1 | bitmap2
 * @dst: destination bitmap; may alias either source
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bits: number of bits
 *
 * NOTE: This helper does *not* return a Boolean, and it does *not*
 * mask off the trailing unused bits of the final word. Callers that
 * need canonical zeroed padding must mask the tail themselves.
 */
void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
		 const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] | bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_or);

/**
 * __bitmap_xor - dst = bitmap1 ^ bitmap2
 * @dst: destination bitmap; may alias either source
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bits: number of bits
 *
 * NOTE: Same caveats as __bitmap_or(): no Boolean return, no tail
 * masking.
 */
void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
		  const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] ^ bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_xor);

/**
 * __bitmap_andnot - dst = bitmap1 & ~bitmap2
 * @dst: destination bitmap; may alias either source
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bits: number of bits
 *
 * Return: true if the result is non-zero anywhere within @bits bits.
 */
bool __bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
		     const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int lim = bits / BITS_PER_LONG;
	unsigned long result = 0;

	for (k = 0; k < lim; k++)
		result |= (dst[k] = bitmap1[k] & ~bitmap2[k]);
	if (bits % BITS_PER_LONG)
		result |= (dst[k] = bitmap1[k] & ~bitmap2[k] &
			   BITMAP_LAST_WORD_MASK(bits));
	return result != 0;
}
EXPORT_SYMBOL(__bitmap_andnot);

/**
 * __bitmap_replace - dst = (old & ~mask) | (new & mask)
 * @dst: destination bitmap; may alias any of the sources
 * @old: bitmap contributing bits where @mask is 0
 * @new: bitmap contributing bits where @mask is 1
 * @mask: selector mask
 * @nbits: number of bits
 */
void __bitmap_replace(unsigned long *dst,
		      const unsigned long *old, const unsigned long *new,
		      const unsigned long *mask, unsigned int nbits)
{
	unsigned int k;
	unsigned int nr = BITS_TO_LONGS(nbits);

	for (k = 0; k < nr; k++)
		dst[k] = (old[k] & ~mask[k]) | (new[k] & mask[k]);
}
EXPORT_SYMBOL(__bitmap_replace);

/**
 * __bitmap_intersects - test whether two bitmaps share any set bit
 * @bitmap1: first bitmap
 * @bitmap2: second bitmap
 * @bits: number of bits
 *
 * Return: true if (bitmap1 & bitmap2) is non-zero within @bits bits.
 */
bool __bitmap_intersects(const unsigned long *bitmap1,
			 const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k, lim = bits / BITS_PER_LONG;

	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & bitmap2[k])
			return true;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return true;
	return false;
}
EXPORT_SYMBOL(__bitmap_intersects);

/**
 * __bitmap_subset - test whether every set bit of bitmap1 is also set in bitmap2
 * @bitmap1: candidate subset
 * @bitmap2: candidate superset
 * @bits: number of bits
 *
 * Return: true if bitmap1 & ~bitmap2 is zero over @bits bits.
 */
bool __bitmap_subset(const unsigned long *bitmap1,
		     const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k, lim = bits / BITS_PER_LONG;

	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & ~bitmap2[k])
			return false;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & ~bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return false;
	return true;
}
EXPORT_SYMBOL(__bitmap_subset);

/*
 * =====================================================================
 * Population count (weight)
 *
 * The BITMAP_WEIGHT() helper factors out the loop-and-tail pattern
 * shared by every "count the set bits" variant below. @FETCH is a
 * C expression that evaluates, for the current @idx, to the value
 * whose Hamming weight should be accumulated.
 * =====================================================================
 */
#define BITMAP_WEIGHT(FETCH, bits)					\
({									\
	unsigned int __bits = (bits), idx, w = 0;			\
									\
	for (idx = 0; idx < __bits / BITS_PER_LONG; idx++)		\
		w += hweight_long(FETCH);				\
									\
	if (__bits % BITS_PER_LONG)					\
		w += hweight_long((FETCH) & BITMAP_LAST_WORD_MASK(__bits)); \
									\
	w;								\
})

/**
 * __bitmap_weight - count the number of set bits in a bitmap
 * @bitmap: bitmap to inspect
 * @bits: number of bits
 *
 * Return: Hamming weight of the first @bits bits of @bitmap.
 */
unsigned int __bitmap_weight(const unsigned long *bitmap, unsigned int bits)
{
	return BITMAP_WEIGHT(bitmap[idx], bits);
}
EXPORT_SYMBOL(__bitmap_weight);

/**
 * __bitmap_weight_and - count set bits in (bitmap1 & bitmap2)
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bits: number of bits
 *
 * Return: Hamming weight of the first @bits bits of the AND.
 */
unsigned int __bitmap_weight_and(const unsigned long *bitmap1,
				 const unsigned long *bitmap2,
				 unsigned int bits)
{
	return BITMAP_WEIGHT(bitmap1[idx] & bitmap2[idx], bits);
}
EXPORT_SYMBOL(__bitmap_weight_and);

/**
 * __bitmap_weight_andnot - count set bits in (bitmap1 & ~bitmap2)
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bits: number of bits
 *
 * Return: Hamming weight of the first @bits bits of the ANDNOT.
 */
unsigned int __bitmap_weight_andnot(const unsigned long *bitmap1,
				    const unsigned long *bitmap2,
				    unsigned int bits)
{
	return BITMAP_WEIGHT(bitmap1[idx] & ~bitmap2[idx], bits);
}
EXPORT_SYMBOL(__bitmap_weight_andnot);

/**
 * __bitmap_weighted_or - dst = bitmap1 | bitmap2, and return its weight
 * @dst: destination bitmap; may alias either source
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bits: number of bits
 *
 * Fuses the OR operation and its population count into a single pass.
 *
 * Return: Hamming weight of @dst over the first @bits bits.
 */
unsigned int __bitmap_weighted_or(unsigned long *dst,
				  const unsigned long *bitmap1,
				  const unsigned long *bitmap2,
				  unsigned int bits)
{
	return BITMAP_WEIGHT(({ dst[idx] = bitmap1[idx] | bitmap2[idx];
				dst[idx]; }), bits);
}
EXPORT_SYMBOL(__bitmap_weighted_or);

/**
 * __bitmap_weighted_xor - dst = bitmap1 ^ bitmap2, and return its weight
 * @dst: destination bitmap; may alias either source
 * @bitmap1: first source bitmap
 * @bitmap2: second source bitmap
 * @bits: number of bits
 *
 * Fuses the XOR operation and its population count into a single pass.
 *
 * Return: Hamming weight of @dst over the first @bits bits.
 */
unsigned int __bitmap_weighted_xor(unsigned long *dst,
				   const unsigned long *bitmap1,
				   const unsigned long *bitmap2,
				   unsigned int bits)
{
	return BITMAP_WEIGHT(({ dst[idx] = bitmap1[idx] ^ bitmap2[idx];
				dst[idx]; }), bits);
}
EXPORT_SYMBOL(__bitmap_weighted_xor);

/*
 * =====================================================================
 * Range set / clear
 *
 * Both helpers operate on the half-open bit range [start, start+len).
 * The masks are constructed so that the first and last words are only
 * modified within the requested range.
 * =====================================================================
 */

/**
 * __bitmap_set - set a contiguous range of bits
 * @map: bitmap to modify
 * @start: first bit to set (inclusive)
 * @len: number of bits to set
 */
void __bitmap_set(unsigned long *map, unsigned int start, int len)
{
	unsigned long *p = map + BIT_WORD(start);
	const unsigned int size = start + len;
	int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

	while (len - bits_to_set >= 0) {
		*p |= mask_to_set;
		len -= bits_to_set;
		bits_to_set = BITS_PER_LONG;
		mask_to_set = ~0UL;
		p++;
	}
	if (len) {
		mask_to_set &= BITMAP_LAST_WORD_MASK(size);
		*p |= mask_to_set;
	}
}
EXPORT_SYMBOL(__bitmap_set);

/**
 * __bitmap_clear - clear a contiguous range of bits
 * @map: bitmap to modify
 * @start: first bit to clear (inclusive)
 * @len: number of bits to clear
 */
void __bitmap_clear(unsigned long *map, unsigned int start, int len)
{
	unsigned long *p = map + BIT_WORD(start);
	const unsigned int size = start + len;
	int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);

	while (len - bits_to_clear >= 0) {
		*p &= ~mask_to_clear;
		len -= bits_to_clear;
		bits_to_clear = BITS_PER_LONG;
		mask_to_clear = ~0UL;
		p++;
	}
	if (len) {
		mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
		*p &= ~mask_to_clear;
	}
}
EXPORT_SYMBOL(__bitmap_clear);

/*
 * =====================================================================
 * Range search
 * =====================================================================
 */

/**
 * bitmap_find_next_zero_area_off - find a contiguous aligned zero area
 * @map: The address to base the search on
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr: The number of zeroed bits we're looking for
 * @align_mask: Alignment mask for zero area
 * @align_offset: Alignment offset for zero area.
 *
 * The @align_mask should be one less than a power of 2; the effect is
 * that the bit offset of all zero areas this function finds plus
 * @align_offset is a multiple of that power of 2.
 *
 * Return: The bit offset of the found area or a value greater than or
 * equal to @size if no area is found.
 */
unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset)
{
	unsigned long end, i, off;

	for_each_clear_bit_from(start, map, size) {
		/*
		 * Round @start up to the requested alignment, taking
		 * @align_offset into account. When @align_offset is 0
		 * this reduces to the ordinary "align up" operation.
		 */
		start = __ALIGN_MASK(start + align_offset, align_mask) -
			align_offset;
		end = start + nr;
		if (end > size)
			break;

		/*
		 * Search the sub-range [start, end) for the last set
		 * bit. If there is one, the window is not fully clear
		 * and we retry from just past it. If there is none,
		 * we have found a valid zero area.
		 */
		off = round_down(start, BITS_PER_LONG);
		i = find_last_bit(map + start / BITS_PER_LONG, end - off) + off;
		if (i >= end || i < start)
			return start;

		start = i;
	}

	return size;
}
EXPORT_SYMBOL(bitmap_find_next_zero_area_off);

/*
 * =====================================================================
 * Position-to-ordinal mapping and permutation application
 * =====================================================================
 */

/**
 * bitmap_pos_to_ord - find ordinal of set bit at given position in bitmap
 * @buf: pointer to a bitmap
 * @pos: a bit position in @buf (0 <= @pos < @nbits)
 * @nbits: number of valid bit positions in @buf
 *
 * Map the bit at position @pos in @buf (of length @nbits) to the
 * ordinal of which set bit it is.  If it is not set or if @pos
 * is not a valid bit position, map to -1.
 *
 * If for example, just bits 4 through 7 are set in @buf, then @pos
 * values 4 through 7 will get mapped to 0 through 3, respectively,
 * and other @pos values will get mapped to -1.  When @pos value 7
 * gets mapped to (returns) @ord value 3 in this example, that means
 * that bit 7 is the 3rd (starting with 0th) set bit in @buf.
 *
 * The bit positions 0 through @nbits-1 are valid positions in @buf.
 *
 * Return: ordinal in [0, bitmap_weight(@buf, @nbits)), or -1.
 */
static int bitmap_pos_to_ord(const unsigned long *buf, unsigned int pos,
			     unsigned int nbits)
{
	if (pos >= nbits || !test_bit(pos, buf))
		return -1;

	return bitmap_weight(buf, pos);
}

/**
 * bitmap_remap - Apply map defined by a pair of bitmaps to another bitmap
 * @dst: remapped result
 * @src: subset to be remapped
 * @old: defines domain of map
 * @new: defines range of map
 * @nbits: number of bits in each of these bitmaps
 *
 * Let @old and @new define a mapping of bit positions, such that
 * whatever position is held by the n-th set bit in @old is mapped
 * to the n-th set bit in @new.  In the more general case, allowing
 * for the possibility that the weight 'w' of @new is less than the
 * weight of @old, map the position of the n-th set bit in @old to
 * the position of the m-th set bit in @new, where m == n % w.
 *
 * If either of the @old and @new bitmaps are empty, or if @src and
 * @dst point to the same location, then this routine copies @src
 * to @dst.
 *
 * The positions of unset bits in @old are mapped to themselves
 * (the identity map).
 *
 * Apply the above specified mapping to @src, placing the result in
 * @dst, clearing any bits previously set in @dst.
 *
 * For example, lets say that @old has bits 4 through 7 set, and
 * @new has bits 12 through 15 set.  This defines the mapping of bit
 * position 4 to 12, 5 to 13, 6 to 14 and 7 to 15, and of all other
 * bit positions unchanged.  So if say @src comes into this routine
 * with bits 1, 5 and 7 set, then @dst should leave with bits 1,
 * 13 and 15 set.
 */
void bitmap_remap(unsigned long *dst, const unsigned long *src,
		  const unsigned long *old, const unsigned long *new,
		  unsigned int nbits)
{
	unsigned int oldbit, w;

	if (dst == src)		/* following doesn't handle inplace remaps */
		return;
	bitmap_zero(dst, nbits);

	w = bitmap_weight(new, nbits);
	for_each_set_bit(oldbit, src, nbits) {
		int n = bitmap_pos_to_ord(old, oldbit, nbits);

		if (n < 0 || w == 0)
			set_bit(oldbit, dst);	/* identity map */
		else
			set_bit(find_nth_bit(new, nbits, n % w), dst);
	}
}
EXPORT_SYMBOL(bitmap_remap);

/**
 * bitmap_bitremap - Apply map defined by a pair of bitmaps to a single bit
 * @oldbit: bit position to be mapped
 * @old: defines domain of map
 * @new: defines range of map
 * @bits: number of bits in each of these bitmaps
 *
 * Let @old and @new define a mapping of bit positions, such that
 * whatever position is held by the n-th set bit in @old is mapped
 * to the n-th set bit in @new.  In the more general case, allowing
 * for the possibility that the weight 'w' of @new is less than the
 * weight of @old, map the position of the n-th set bit in @old to
 * the position of the m-th set bit in @new, where m == n % w.
 *
 * The positions of unset bits in @old are mapped to themselves
 * (the identity map).
 *
 * Apply the above specified mapping to bit position @oldbit, returning
 * the new bit position.
 *
 * For example, lets say that @old has bits 4 through 7 set, and
 * @new has bits 12 through 15 set.  This defines the mapping of bit
 * position 4 to 12, 5 to 13, 6 to 14 and 7 to 15, and of all other
 * bit positions unchanged.  So if say @oldbit is 5, then this routine
 * returns 13.
 *
 * Return: new bit position.
 */
int bitmap_bitremap(int oldbit, const unsigned long *old,
		    const unsigned long *new, int bits)
{
	int w = bitmap_weight(new, bits);
	int n = bitmap_pos_to_ord(old, oldbit, bits);

	if (n < 0 || w == 0)
		return oldbit;
	else
		return find_nth_bit(new, bits, n % w);
}
EXPORT_SYMBOL(bitmap_bitremap);

#ifdef CONFIG_NUMA
/**
 * bitmap_onto - translate one bitmap relative to another
 * @dst: resulting translated bitmap
 * @orig: original untranslated bitmap
 * @relmap: bitmap relative to which translated
 * @bits: number of bits in each of these bitmaps
 *
 * Set the n-th bit of @dst iff there exists some m such that the
 * n-th bit of @relmap is set, the m-th bit of @orig is set, and
 * the n-th bit of @relmap is also the m-th _set_ bit of @relmap.
 * (If you understood the previous sentence the first time your
 * read it, you're overqualified for your current job.)
 *
 * In other words, @orig is mapped onto (surjectively) @dst,
 * using the map { <n, m> | the n-th bit of @relmap is the
 * m-th set bit of @relmap }.
 *
 * Any set bits in @orig above bit number W, where W is the
 * weight of (number of set bits in) @relmap are mapped nowhere.
 * In particular, if for all bits m set in @orig, m >= W, then
 * @dst will end up empty.  In situations where the possibility
 * of such an empty result is not desired, one way to avoid it is
 * to use the bitmap_fold() operator, below, to first fold the
 * @orig bitmap over itself so that all its set bits x are in the
 * range 0 <= x < W.  The bitmap_fold() operator does this by
 * setting the bit (m % W) in @dst, for each bit (m) set in @orig.
 *
 * Example [1] for bitmap_onto():
 *  Let's say @relmap has bits 30-39 set, and @orig has bits
 *  1, 3, 5, 7, 9 and 11 set.  Then on return from this routine,
 *  @dst will have bits 31, 33, 35, 37 and 39 set.
 *
 *  When bit 0 is set in @orig, it means turn on the bit in
 *  @dst corresponding to whatever is the first bit (if any)
 *  that is turned on in @relmap.  Since bit 0 was off in the
 *  above example, we leave off that bit (bit 30) in @dst.
 *
 *  When bit 1 is set in @orig (as in the above example), it
 *  means turn on the bit in @dst corresponding to whatever
 *  is the second bit that is turned on in @relmap.  The second
 *  bit in @relmap that was turned on in the above example was
 *  bit 31, so we turned on bit 31 in @dst.
 *
 *  Similarly, we turned on bits 33, 35, 37 and 39 in @dst,
 *  because they were the 4th, 6th, 8th and 10th set bits
 *  set in @relmap, and the 4th, 6th, 8th and 10th bits of
 *  @orig (i.e. bits 3, 5, 7 and 9) were also set.
 *
 *  When bit 11 is set in @orig, it means turn on the bit in
 *  @dst corresponding to whatever is the twelfth bit that is
 *  turned on in @relmap.  In the above example, there were
 *  only ten bits turned on in @relmap (30..39), so that bit
 *  11 was set in @orig had no affect on @dst.
 *
 * Example [2] for bitmap_fold() + bitmap_onto():
 *  Let's say @relmap has these ten bits set::
 *
 *		40 41 42 43 45 48 53 61 74 95
 *
 *  (for the curious, that's 40 plus the first ten terms of the
 *  Fibonacci sequence.)
 *
 *  Further lets say we use the following code, invoking
 *  bitmap_fold() then bitmap_onto, as suggested above to
 *  avoid the possibility of an empty @dst result::
 *
 *	unsigned long *tmp;	// a temporary bitmap's bits
 *
 *	bitmap_fold(tmp, orig, bitmap_weight(relmap, bits), bits);
 *	bitmap_onto(dst, tmp, relmap, bits);
 *
 *  Then this table shows what various values of @dst would be, for
 *  various @orig's.  I list the zero-based positions of each set bit.
 *  The tmp column shows the intermediate result, as computed by
 *  using bitmap_fold() to fold the @orig bitmap modulo ten
 *  (the weight of @relmap):
 *
 *	=============== ============== =================
 *	@orig           tmp            @dst
 *	0                0             40
 *	1                1             41
 *	9                9             95
 *	10               0             40 [#f1]_
 *	1 3 5 7          1 3 5 7       41 43 48 61
 *	0 1 2 3 4        0 1 2 3 4     40 41 42 43 45
 *	0 9 18 27        0 9 8 7       40 61 74 95
 *	0 10 20 30       0             40
 *	0 11 22 33       0 1 2 3       40 41 42 43
 *	0 12 24 36       0 2 4 6       40 42 45 53
 *	78 102 211       1 2 8         41 42 74 [#f1]_
 *	=============== ============== =================
 *
 * .. [#f1]
 *
 *	For these marked lines, if we hadn't first done bitmap_fold()
 *	into tmp, then the @dst result would have been empty.
 *
 * If either of @orig or @relmap is empty (no set bits), then @dst
 * will be returned empty.
 *
 * If (as explained above) the only set bits in @orig are in positions
 * m where m >= W, (where W is the weight of @relmap) then @dst will
 * once again be returned empty.
 *
 * All bits in @dst not set by the above rule are cleared.
 */
void bitmap_onto(unsigned long *dst, const unsigned long *orig,
		 const unsigned long *relmap, unsigned int bits)
{
	unsigned int n, m;	/* same meaning as in above comment */

	if (dst == orig)	/* following doesn't handle inplace mappings */
		return;
	bitmap_zero(dst, bits);

	/*
	 * The following code is a more efficient, but less obvious,
	 * equivalent to the loop:
	 *
	 *	for (m = 0; m < bitmap_weight(relmap, bits); m++) {
	 *		n = find_nth_bit(relmap, bits, m);
	 *		if (test_bit(m, orig))
	 *			set_bit(n, dst);
	 *	}
	 */
	m = 0;
	for_each_set_bit(n, relmap, bits) {
		/* m == bitmap_pos_to_ord(relmap, n, bits) */
		if (test_bit(m, orig))
			set_bit(n, dst);
		m++;
	}
}

/**
 * bitmap_fold - fold larger bitmap into smaller, modulo specified size
 * @dst: resulting smaller bitmap
 * @orig: original larger bitmap
 * @sz: specified size
 * @nbits: number of bits in each of these bitmaps
 *
 * For each bit oldbit in @orig, set bit oldbit mod @sz in @dst.
 * Clear all other bits in @dst.  See further the comment and
 * Example [2] for bitmap_onto() for why and how to use this.
 */
void bitmap_fold(unsigned long *dst, const unsigned long *orig,
		 unsigned int sz, unsigned int nbits)
{
	unsigned int oldbit;

	if (dst == orig)	/* following doesn't handle inplace mappings */
		return;
	bitmap_zero(dst, nbits);

	for_each_set_bit(oldbit, orig, nbits)
		set_bit(oldbit % sz, dst);
}
#endif /* CONFIG_NUMA */

/*
 * =====================================================================
 * Allocation / deallocation
 * =====================================================================
 */

/**
 * bitmap_alloc - allocate a bitmap
 * @nbits: number of bits the bitmap must hold
 * @flags: GFP allocation flags
 *
 * Return: pointer to the bitmap, or NULL on allocation failure. The
 * content of the bitmap is unspecified; use bitmap_zalloc() if you
 * want zero-initialised storage.
 */
unsigned long *bitmap_alloc(unsigned int nbits, gfp_t flags)
{
	return kmalloc_array(BITS_TO_LONGS(nbits), sizeof(unsigned long),
			     flags);
}
EXPORT_SYMBOL(bitmap_alloc);

/**
 * bitmap_zalloc - allocate a zero-initialised bitmap
 * @nbits: number of bits the bitmap must hold
 * @flags: GFP allocation flags
 *
 * Return: pointer to the bitmap, or NULL on allocation failure.
 */
unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags)
{
	return bitmap_alloc(nbits, flags | __GFP_ZERO);
}
EXPORT_SYMBOL(bitmap_zalloc);

/**
 * bitmap_alloc_node - allocate a bitmap on a specific NUMA node
 * @nbits: number of bits the bitmap must hold
 * @flags: GFP allocation flags
 * @node: NUMA node to allocate from
 *
 * Return: pointer to the bitmap, or NULL on allocation failure.
 */
unsigned long *bitmap_alloc_node(unsigned int nbits, gfp_t flags, int node)
{
	return kmalloc_array_node(BITS_TO_LONGS(nbits), sizeof(unsigned long),
				  flags, node);
}
EXPORT_SYMBOL(bitmap_alloc_node);

/**
 * bitmap_zalloc_node - allocate a zero-initialised bitmap on a NUMA node
 * @nbits: number of bits the bitmap must hold
 * @flags: GFP allocation flags
 * @node: NUMA node to allocate from
 *
 * Return: pointer to the bitmap, or NULL on allocation failure.
 */
unsigned long *bitmap_zalloc_node(unsigned int nbits, gfp_t flags, int node)
{
	return bitmap_alloc_node(nbits, flags | __GFP_ZERO, node);
}
EXPORT_SYMBOL(bitmap_zalloc_node);

/**
 * bitmap_free - free a bitmap
 * @bitmap: bitmap previously returned by bitmap_alloc() and friends
 */
void bitmap_free(const unsigned long *bitmap)
{
	kfree(bitmap);
}
EXPORT_SYMBOL(bitmap_free);

/*
 * =====================================================================
 * Devres-managed allocation
 * =====================================================================
 */

static void devm_bitmap_free(void *data)
{
	unsigned long *bitmap = data;

	bitmap_free(bitmap);
}

/**
 * devm_bitmap_alloc - allocate a bitmap tied to a device's lifetime
 * @dev: owning device
 * @nbits: number of bits the bitmap must hold
 * @flags: GFP allocation flags
 *
 * The bitmap is freed automatically when @dev is detached. On failure
 * to register the devres action, the freshly allocated bitmap is
 * released via devm_add_action_or_reset().
 *
 * Return: pointer to the bitmap, or NULL on any failure.
 */
unsigned long *devm_bitmap_alloc(struct device *dev,
				 unsigned int nbits, gfp_t flags)
{
	unsigned long *bitmap;
	int ret;

	bitmap = bitmap_alloc(nbits, flags);
	if (!bitmap)
		return NULL;

	/*
	 * _or_reset() semantics: on failure the release callback
	 * (devm_bitmap_free) is invoked immediately, so we must not
	 * free @bitmap here.
	 */
	ret = devm_add_action_or_reset(dev, devm_bitmap_free, bitmap);
	if (ret)
		return NULL;

	return bitmap;
}
EXPORT_SYMBOL_GPL(devm_bitmap_alloc);

/**
 * devm_bitmap_zalloc - allocate a zero-initialised devres bitmap
 * @dev: owning device
 * @nbits: number of bits the bitmap must hold
 * @flags: GFP allocation flags
 *
 * Return: pointer to the bitmap, or NULL on any failure.
 */
unsigned long *devm_bitmap_zalloc(struct device *dev,
				  unsigned int nbits, gfp_t flags)
{
	return devm_bitmap_alloc(dev, nbits, flags | __GFP_ZERO);
}
EXPORT_SYMBOL_GPL(devm_bitmap_zalloc);

/*
 * =====================================================================
 * Bit-order conversion between bitmaps and u32/u64 arrays
 *
 * These helpers live only on the bit-width where they make sense:
 *   - 64-bit long: u32 <-> bitmap
 *   - 32-bit long: u64 <-> bitmap
 * so a bitmap built for one word size can be consumed on another.
 * All values are in host byte order.
 * =====================================================================
 */

#if BITS_PER_LONG == 64
/**
 * bitmap_from_arr32 - copy the contents of u32 array of bits to bitmap
 * @bitmap: array of unsigned longs, the destination bitmap
 * @buf: array of u32 (in host byte order), the source bitmap
 * @nbits: number of bits in @bitmap
 */
void bitmap_from_arr32(unsigned long *bitmap, const u32 *buf,
		       unsigned int nbits)
{
	unsigned int i, halfwords;

	halfwords = DIV_ROUND_UP(nbits, 32);
	for (i = 0; i < halfwords; i++) {
		bitmap[i / 2] = (unsigned long)buf[i];
		if (++i < halfwords)
			bitmap[i / 2] |= ((unsigned long)buf[i]) << 32;
	}

	/* Clear tail bits in last word beyond nbits. */
	if (nbits % BITS_PER_LONG)
		bitmap[(halfwords - 1) / 2] &= BITMAP_LAST_WORD_MASK(nbits);
}
EXPORT_SYMBOL(bitmap_from_arr32);

/**
 * bitmap_to_arr32 - copy the contents of bitmap to a u32 array of bits
 * @buf: array of u32 (in host byte order), the dest bitmap
 * @bitmap: array of unsigned longs, the source bitmap
 * @nbits: number of bits in @bitmap
 */
void bitmap_to_arr32(u32 *buf, const unsigned long *bitmap, unsigned int nbits)
{
	unsigned int i, halfwords;

	halfwords = DIV_ROUND_UP(nbits, 32);
	for (i = 0; i < halfwords; i++) {
		buf[i] = (u32)(bitmap[i / 2] & UINT_MAX);
		if (++i < halfwords)
			buf[i] = (u32)(bitmap[i / 2] >> 32);
	}

	/* Clear tail bits in last element of array beyond nbits. */
	if (nbits % BITS_PER_LONG)
		buf[halfwords - 1] &= (u32)(UINT_MAX >> ((-nbits) & 31));
}
EXPORT_SYMBOL(bitmap_to_arr32);
#endif /* BITS_PER_LONG == 64 */

#if BITS_PER_LONG == 32
/**
 * bitmap_from_arr64 - copy the contents of u64 array of bits to bitmap
 * @bitmap: array of unsigned longs, the destination bitmap
 * @buf: array of u64 (in host byte order), the source bitmap
 * @nbits: number of bits in @bitmap
 */
void bitmap_from_arr64(unsigned long *bitmap, const u64 *buf,
		       unsigned int nbits)
{
	int n;

	for (n = nbits; n > 0; n -= 64) {
		u64 val = *buf++;

		*bitmap++ = val;
		if (n > 32)
			*bitmap++ = val >> 32;
	}

	/*
	 * Clear tail bits in the last word beyond nbits.
	 *
	 * Negative index is OK because here we point to the word next
	 * to the last word of the bitmap, except for nbits == 0, which
	 * is tested implicitly.
	 */
	if (nbits % BITS_PER_LONG)
		bitmap[-1] &= BITMAP_LAST_WORD_MASK(nbits);
}
EXPORT_SYMBOL(bitmap_from_arr64);

/**
 * bitmap_to_arr64 - copy the contents of bitmap to a u64 array of bits
 * @buf: array of u64 (in host byte order), the dest bitmap
 * @bitmap: array of unsigned longs, the source bitmap
 * @nbits: number of bits in @bitmap
 */
void bitmap_to_arr64(u64 *buf, const unsigned long *bitmap, unsigned int nbits)
{
	const unsigned long *end = bitmap + BITS_TO_LONGS(nbits);

	while (bitmap < end) {
		*buf = *bitmap++;
		if (bitmap < end)
			*buf |= (u64)(*bitmap++) << 32;
		buf++;
	}

	/* Clear tail bits in the last element of array beyond nbits. */
	if (nbits % 64)
		buf[-1] &= GENMASK_ULL((nbits - 1) % 64, 0);
}
EXPORT_SYMBOL(bitmap_to_arr64);
#endif /* BITS_PER_LONG == 32 */