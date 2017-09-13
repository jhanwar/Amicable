#include "stdafx.h"
#include "PrimeTables.h"
#include "RangeGen.h"
#include "sprp64.h"
#include <sstream>

//#pragma optimize("", off)
//#undef FORCEINLINE
//#define FORCEINLINE NOINLINE

PRAGMA_WARNING(push, 1)
PRAGMA_WARNING(disable : 4091 4917 4625 4626 5026 5027)
#include <boinc_api.h>
PRAGMA_WARNING(pop)

CRITICAL_SECTION RangeGen::lock = CRITICAL_SECTION_INITIALIZER;
CACHE_ALIGNED RangeGen::StackFrame RangeGen::search_stack[MaxPrimeFactors];
CACHE_ALIGNED Factor RangeGen::factors[MaxPrimeFactors];
int RangeGen::search_stack_depth;
int RangeGen::prev_search_stack_depth;
unsigned int RangeGen::cur_largest_prime_power;
volatile num64 RangeGen::SharedCounterForSearch;
num64 RangeGen::total_numbers_checked;
double RangeGen::total_numbers_to_check = 2.5e11;
volatile bool RangeGen::allDone = false;

RangeGen RangeGen::RangeGen_instance;

template<unsigned int largest_prime_power>
NOINLINE bool RangeGen::Iterate(RangeData& range)
{
	StackFrame* s = search_stack + search_stack_depth;
	Factor* f = factors + search_stack_depth;

	int start_i, start_j;
	num64 q0;
	num128 q;
	num128 sum_q;

#define RECURSE ++search_stack_depth; ++s; ++f; goto recurse_begin
#define RETURN --search_stack_depth; --s; --f; goto recurse_begin

recurse_begin:
	const bool is_return = (search_stack_depth < prev_search_stack_depth);
	prev_search_stack_depth = search_stack_depth;
	if (is_return)
	{
		if (search_stack_depth < 0)
		{
			return false;
		}
		goto recurse_return;
	}

	if (search_stack_depth > 0)
	{
		start_i = factors[search_stack_depth - 1].index + 1;
		f->p = factors[search_stack_depth - 1].p;
		++f->p;
	}
	else
	{
		start_i = 0;
		f->p = PrimeIterator();
	}

	// A check to ensure that m is not divisible by 6
	if (search_stack_depth == 1)
	{
		// factors[0].p is 2
		// factors[1].p is 3
		// change factors[1].p to 5
		if (start_i == 1)
		{
			start_i = 2;
			++f->p;
		}
	}

	for (f->index = start_i; f->p.Get() <= SearchLimit::RangeGenPrimeBound; ++f->p, ++f->index)
	{
		s[1].value = s->value * f->p.Get();
		if (s[1].value >= SearchLimit::value)
		{
			RETURN;
		}
		s[1].sum = s->sum * (f->p.Get() + 1);

		f->k = 1;

		{
			const int index_inv128 = f->index - 1;
			if (index_inv128 >= 0)
			{
				f->p_inv128 = (static_cast<unsigned int>(index_inv128) < ReciprocalsTableSize128) ? PrimeInverses128[index_inv128] : -modular_inverse128(f->p.Get());
			}
		}

		for (;;)
		{
			start_j = f->index + 1;

			q0 = f->p.GetNext();

			// A check to ensure that m*q is not divisible by 6
			if (search_stack_depth == 0)
			{
				// factors[0].p is 2
				// q is 3
				// change q to 5
				if (start_j == 1)
				{
					start_j = 2;
					q0 = 5;
				}
			}

			{
				q = q0;
				sum_q = q0 + 1;

				IF_CONSTEXPR(largest_prime_power > 1)
				{
					q *= q0;
					sum_q += q;
				}

				IF_CONSTEXPR(largest_prime_power > 2)
				{
					q *= q0;
					sum_q += q;
				}

				const num128 value_to_check = s[1].value * q;
				if (value_to_check >= SearchLimit::value)
				{
					if (f->k == 1)
					{
						RETURN;
					}
					break;
				}

				// Skip overabundant numbers
				const byte is_deficient = (s[1].sum - s[1].value < s[1].value);
				if (is_deficient || !OverAbundant<(largest_prime_power & 1) ? 2 : 1>(factors, search_stack_depth, s[1].value, s[1].sum, (largest_prime_power & 1) ? 2 : 1))
				{
					if (!is_deficient || (s[1].sum * sum_q - value_to_check > value_to_check))
					{
						range.value = s[1].value;
						range.sum = s[1].sum;
						range.start_prime = q0;
						range.index_start_prime = static_cast<unsigned int>(start_j);
						for (unsigned int i = 0; i <= static_cast<unsigned int>(search_stack_depth); ++i)
						{
							range.factors[i] = factors[i];
						}
						for (unsigned int i = static_cast<unsigned int>(search_stack_depth) + 1; i < MaxPrimeFactors; ++i)
						{
							memset(&range.factors[i].p, 0, sizeof(range.factors[i].p));
							range.factors[i].k = 0;
						}
						range.last_factor_index = search_stack_depth;
						++search_stack_depth;
						return true;
					}

					if (!whole_branch_deficient(SearchLimit::value, s[1].value, s[1].sum, f))
					{
						RECURSE;
					}
				}
			}

recurse_return:
			s[1].value *= f->p.Get();
			if (s[1].value >= SearchLimit::value)
			{
				break;
			}
			s[1].sum = s[1].sum * f->p.Get() + s->sum;
			++f->k;
		}

		if (search_stack_depth == 0)
		{
			// Check only 2, 3, 5 as the smallest prime factor because the smallest abundant num64 coprime to 2*3*5 is ~2*10^25
			if (f->p.Get() >= 5)
			{
				break;
			}
		}
	}
	if (search_stack_depth > 0)
	{
		RETURN;
	}
	search_stack_depth = -1;
	return false;
}

bool RangeGen::Iterate(RangeData& range)
{
	if (search_stack_depth < 0)
	{
		return false;
	}

	if (cur_largest_prime_power == 1)
	{
		if (Iterate<1>(range))
		{
			return true;
		}
		else
		{
			cur_largest_prime_power = 2;
			search_stack_depth = 0;
			prev_search_stack_depth = 0;
		}
	}

	if (cur_largest_prime_power == 2)
	{
		if (Iterate<2>(range))
		{
			return true;
		}
		else
		{
			cur_largest_prime_power = 3;
			search_stack_depth = 0;
			prev_search_stack_depth = 0;
		}
	}

	if (cur_largest_prime_power == 3)
	{
		return Iterate<3>(range);
	}

	return false;
}

template<typename T>
FORCEINLINE unsigned int ParseFactorization(char* factorization, T callback)
{
	unsigned int numFactors = 0;
	int counter = 0;
	num64 prev_p = 0;
	num64 p = 0;
	PrimeIterator p1;
	int index_p1 = 0;
	unsigned int k = 0;
	for (char* ptr = factorization, *prevPtr = factorization; ; ++ptr)
	{
		const char c = *ptr;
		if ((c == '^') || (c == '*') || (c == '\0'))
		{
			*ptr = '\0';
			++counter;
			if (counter == 1)
			{
				p = static_cast<num64>(StrToNumber(prevPtr));
			}
			else if (counter == 2)
			{
				k = static_cast<unsigned int>(atoi(prevPtr));
			}

			if (((counter == 1) && (c != '^')) || (counter == 2))
			{
				if (counter == 1)
				{
					k = 1;
				}
				counter = 0;
				if ((numFactors > 0) && (p <= prev_p))
				{
					std::cerr << "Factorization '" << factorization << "' is incorrect: factors must be in increasing order" << std::endl;
					abort();
				}
				prev_p = p;

				if (p1.Get() < p)
				{
					if (p1.Get() == 2)
					{
						++p1;
						++index_p1;
					}
					while (p1.Get() < p)
					{
						++p1;
						++index_p1;
					}
				}

				if (p1.Get() != p)
				{
					std::cerr << "Factorization '" << factorization << "' is incorrect: " << p << " is not a prime" << std::endl;
					abort();
				}

				if (k < 1)
				{
					std::cerr << "Factorization '" << factorization << "'is incorrect: prime power for " << p << " must be >= 1" << std::endl;
					abort();
				}

				callback(p, k, index_p1, numFactors);

				++numFactors;

				if (numFactors >= MaxPrimeFactors)
				{
					std::cerr << "Factorization '" << factorization << "' is incorrect: too many prime factors" << std::endl;
					abort();
				}
			}
			*ptr = c;
			prevPtr = ptr + 1;
			if (c == '\0')
			{
				break;
			}
		}
	}

	if (numFactors == 0)
	{
		std::cerr << "Factorization '" << factorization << "' is incorrect: too few prime factors" << std::endl;
		abort();
	}

	return numFactors;
}

NOINLINE void RangeGen::Init(char* startFrom, char* stopAt, RangeData* outStartFromRange, Factor* outStopAtFactors, unsigned int largestPrimePower)
{
	search_stack[0].value = 1;
	search_stack[0].sum = 1;
	search_stack_depth = 0;
	prev_search_stack_depth = 0;
	cur_largest_prime_power = largestPrimePower;
	SharedCounterForSearch = 0;

	if (startFrom)
	{
		const unsigned int numFactors = ParseFactorization(startFrom,
			[startFrom](num64 p, unsigned int k, int p_index, unsigned int factor_index)
			{
				Factor& f = factors[factor_index];
				f.p = PrimeIterator(p);
				f.k = k;
				f.index = p_index;

				f.p_inv128 = -modular_inverse128(p);

				if ((f.p.Get() > 2) && (f.p_inv128 * f.p.Get() != 1))
				{
					std::cerr << "Internal error: modular_inverse128 failed" << std::endl;
					abort();
				}

				StackFrame& cur_s = search_stack[factor_index];
				StackFrame& next_s = search_stack[factor_index + 1];
				next_s.value = cur_s.value;
				next_s.sum = cur_s.sum;
				for (num64 i = 0; i < k; ++i)
				{
					next_s.value *= p;
					if (next_s.value >= SearchLimit::value)
					{
						std::cerr << "Factorization '" << startFrom << "' is incorrect: number is too large" << std::endl;
						abort();
					}
					next_s.sum = next_s.sum * p + cur_s.sum;
				}
			}
		);

		search_stack_depth = static_cast<int>(numFactors);
		prev_search_stack_depth = static_cast<int>(numFactors);

		StackFrame* s = search_stack + search_stack_depth;
		Factor* f = factors + search_stack_depth - 1;
		RangeData& range = *outStartFromRange;
		range.value = 0;

		int start_j = f->index + 1;
		num64 q0 = GetNthPrime(start_j);

		// A check to ensure that m*q is not divisible by 6
		if (search_stack_depth == 1)
		{
			// factors[0].p is 2
			// q is 3
			// change q to 5
			if (start_j == 1)
			{
				start_j = 2;
				q0 = 5;
			}
		}

		num64 q = q0;
		num64 sum_q = q0 + 1;

		const num128 value_to_check = s->value * q;
		bool is_initialized = false;
		if (value_to_check < SearchLimit::value)
		{
			// Skip overabundant numbers
			const bool is_deficient = (s->sum - s->value < s->value);
			if (is_deficient || !OverAbundant<2>(factors, static_cast<int>(numFactors) - 1, s->value, s->sum, static_cast<num64>((cur_largest_prime_power & 1) ? 2 : 1)))
			{
				if (!is_deficient || (s->sum * sum_q - value_to_check > value_to_check))
				{
					is_initialized = true;
					range.value = s->value;
					range.sum = s->sum;
					range.start_prime = q0;
					range.index_start_prime = static_cast<unsigned int>(start_j);
					memcpy(range.factors, factors, sizeof(Factor) * numFactors);
					range.last_factor_index = static_cast<int>(numFactors) - 1;
				}
			}
		}
		if (!is_initialized)
		{
			std::cerr << "Command-line parameter \"/from " << startFrom << "\" is invalid\n" << std::endl;
			boinc_finish(-1);
		}
	}

	if (stopAt)
	{
		ParseFactorization(stopAt,
			[outStopAtFactors](num64 p, unsigned int k, int /*p_index*/, unsigned int factor_index)
			{
				outStopAtFactors[factor_index].p = PrimeIterator(p);
				outStopAtFactors[factor_index].k = k;
			}
		);
	}
}

bool RangeGen::HasReached(const RangeData& range, const Factor* stopAtfactors)
{
	for (int i = 0; i < MaxPrimeFactors; ++i)
	{
		if (range.factors[i].p.Get() != stopAtfactors[i].p.Get())
		{
			return (range.factors[i].p.Get() > stopAtfactors[i].p.Get());
		}
		else if (range.factors[i].k != stopAtfactors[i].k)
		{
			return (range.factors[i].k > stopAtfactors[i].k);
		}
	}
	return true;
}
