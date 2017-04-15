#include "stdafx.h"
#include "num128.h"

num128 atoi128(const char* s)
{
	num128 result = 0;
	for (;;)
	{
		const unsigned char d = static_cast<unsigned char>(*(s++) - '0');
		if (d > 9)
		{
			break;
		}
		result = result * 10 + d;
	}
	return result;
}

std::ostream& operator<<(std::ostream& s, num128 a)
{
	char buf[40];
	char* c = buf + sizeof(buf);
	*(--c) = '\0';

	if (a == 0)
	{
		*(--c) = '0';
		return s;
	}

	do 
	{
		const num128 prev_a = a;
		a /= 10;
		*(--c) = '0' + static_cast<char>((prev_a - a * 10).lo);
	} while (a != 0);

	s << c;
	return s;
}

#ifndef __GNUG__

NOINLINE num128 num128::operator/(const num128& a) const
{
	// Edge case: N < a, result is 0
	if (*this < a)
	{
		return 0;
	}

	if (a.hi == 0)
	{
		// "a" fits in 64 bits
		if (hi < a.lo)
		{
			// Simplest case, result fits in 64 bits and we can use 128:64 division
			return udiv128_noremainder(lo, hi, a.lo);
		}
		else
		{
			// Result doesn't fit in 64 bits
			//
			// hi * 2^64 + lo = (q_hi * a + r_hi) * 2^64 + lo where q_hi = hi / a, r_hi = hi % a
			//
			// After dividing by "a": (hi * 2^64 + lo) / a = q_hi * 2^64 + (r_hi * 2^64 + lo) / a
			// High 64 bits: q_hi = hi / a
			// Low 64 bits: (r_hi * 2^64 + lo) / a
			// r_hi is guaranteed to be less than "a" because r_hi = hi % a
			// So we can use 128:64 division to calculate low 64 bits
			num128 result;
			result.hi = hi / a.lo;
			const num64 r_hi = hi % a.lo;
			result.lo = udiv128_noremainder(lo, r_hi, a.lo);
			return result;
		}
	}

	// a >= 2^64. It also means that the result will fit in 64 bits
	//
	// We estimate this result by doing 128:64 division:
	//
	// (hi * 2^64 + lo) / a ~= ((hi * 2^64 + lo) / 2^k) / (a / 2^k) where k is an integer such that 2^63 <= (a / 2^k) < 2^64
	//
	// After getting the initial estimate, it's easy to calculate the exact value

	// Edge case: 2^128 > N >= a >= 2^127, so the result can only be 1
	if (a.hi >= num64(1) << 63)
	{
		return 1;
	}

	unsigned long index;
	_BitScanReverse64(&index, a.hi); // a.hi < 2^63, so index < 63 here
	++index; // index < 64 here

	const num64 a1 = __shiftright128(a.lo, a.hi, static_cast<unsigned char>(index));
	const num128 n = *this >> static_cast<unsigned char>(index);
	const num64 q = udiv128_noremainder(n.lo, n.hi, a1);
	const num128 n1 = a * q;

	if (*this < n1)
	{
		return q - 1;
	}
	else if (UNLIKELY(*this - n1 >= a))
	{
		// I'm not sure if this is actually needed: I couldn't find a case where this line of code is executed
		// TODO: prove that it never executes
		return q + 1;
	}

	return q;
}

#endif