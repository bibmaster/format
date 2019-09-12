// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <algorithm>
#include <cmath>

#include "format_double.hpp"

namespace univang {
namespace fmt {
namespace detail {

namespace {

using byte = double_format_context::byte;

int NormalizedExponent(uint64_t significand, int exponent) {
    assert(significand != 0);
    while((significand & Double::kHiddenBit) == 0) {
        significand = significand << 1;
        exponent = exponent - 1;
    }
    return exponent;
}

class Bignum {
public:
    // 3584 = 128 * 28. We can represent 2^3584 > 10^1000 accurately.
    // This bignum can encode much bigger numbers, since it contains an
    // exponent.
    static const int kMaxSignificantBits = 3584;

    Bignum() : used_bigits_(0), exponent_(0) {
    }

    Bignum(const Bignum&) = delete;
    Bignum& operator=(const Bignum&) = delete;

    void AssignUInt16(const uint16_t value);
    void AssignUInt64(uint64_t value);
    void AssignBignum(const Bignum& other);

    void AssignPowerUInt16(uint16_t base, const int exponent);

    // Precondition: this >= other.
    void SubtractBignum(const Bignum& other);

    void Square();
    void ShiftLeft(const int shift_amount);
    void MultiplyByUInt32(const uint32_t factor);
    void MultiplyByUInt64(const uint64_t factor);
    void MultiplyByPowerOfTen(const int exponent);
    void Times10() {
        return MultiplyByUInt32(10);
    }
    // Pseudocode:
    //  int result = this / other;
    //  this = this % other;
    // In the worst case this function is in O(this/other).
    uint16_t DivideModuloIntBignum(const Bignum& other);

    bool ToHexString(char* buffer, const int buffer_size) const;

    // Returns
    //  -1 if a < b,
    //   0 if a == b, and
    //  +1 if a > b.
    static int Compare(const Bignum& a, const Bignum& b);
    static bool Equal(const Bignum& a, const Bignum& b) {
        return Compare(a, b) == 0;
    }
    static bool LessEqual(const Bignum& a, const Bignum& b) {
        return Compare(a, b) <= 0;
    }
    static bool Less(const Bignum& a, const Bignum& b) {
        return Compare(a, b) < 0;
    }
    // Returns Compare(a + b, c);
    static int PlusCompare(const Bignum& a, const Bignum& b, const Bignum& c);
    // Returns a + b == c
    static bool PlusEqual(const Bignum& a, const Bignum& b, const Bignum& c) {
        return PlusCompare(a, b, c) == 0;
    }
    // Returns a + b <= c
    static bool PlusLessEqual(
        const Bignum& a, const Bignum& b, const Bignum& c) {
        return PlusCompare(a, b, c) <= 0;
    }
    // Returns a + b < c
    static bool PlusLess(const Bignum& a, const Bignum& b, const Bignum& c) {
        return PlusCompare(a, b, c) < 0;
    }

private:
    typedef uint32_t Chunk;
    typedef uint64_t DoubleChunk;

    static const int kChunkSize = sizeof(Chunk) * 8;
    static const int kDoubleChunkSize = sizeof(DoubleChunk) * 8;
    // With bigit size of 28 we loose some bits, but a double still fits easily
    // into two chunks, and more importantly we can use the Comba
    // multiplication.
    static const int kBigitSize = 28;
    static const Chunk kBigitMask = (1 << kBigitSize) - 1;
    // Every instance allocates kBigitLength chunks on the stack. Bignums cannot
    // grow. There are no checks if the stack-allocated space is sufficient.
    static const int kBigitCapacity = kMaxSignificantBits / kBigitSize;

    static void EnsureCapacity(const int size) {
        assert(size <= kBigitCapacity);
    }
    void Align(const Bignum& other);
    void Clamp();
    bool IsClamped() const {
        return used_bigits_ == 0 || RawBigit(used_bigits_ - 1) != 0;
    }
    void Zero() {
        used_bigits_ = 0;
        exponent_ = 0;
    }
    // Requires this to have enough capacity (no tests done).
    // Updates used_bigits_ if necessary.
    // shift_amount must be < kBigitSize.
    void BigitsShiftLeft(const int shift_amount);
    // BigitLength includes the "hidden" bigits encoded in the exponent.
    int BigitLength() const {
        return used_bigits_ + exponent_;
    }
    Chunk& RawBigit(const int index);
    const Chunk& RawBigit(const int index) const;
    Chunk BigitOrZero(const int index) const;
    void SubtractTimes(const Bignum& other, const int factor);

    // The Bignum's value is value(bigits_buffer_) * 2^(exponent_ * kBigitSize),
    // where the value of the buffer consists of the lower kBigitSize bits of
    // the first used_bigits_ Chunks in bigits_buffer_, first chunk has lowest
    // significant bits.
    int16_t used_bigits_;
    int16_t exponent_;
    Chunk bigits_buffer_[kBigitCapacity];
};

Bignum::Chunk& Bignum::RawBigit(const int index) {
    assert(static_cast<unsigned>(index) < kBigitCapacity);
    return bigits_buffer_[index];
}

const Bignum::Chunk& Bignum::RawBigit(const int index) const {
    assert(static_cast<unsigned>(index) < kBigitCapacity);
    return bigits_buffer_[index];
}

template<typename S>
static int BitSize(const S value) {
    (void)value; // Mark variable as used.
    return 8 * sizeof(value);
}

// Guaranteed to lie in one Bigit.
void Bignum::AssignUInt16(const uint16_t value) {
    assert(kBigitSize >= BitSize(value));
    Zero();
    if(value > 0) {
        RawBigit(0) = value;
        used_bigits_ = 1;
    }
}

void Bignum::AssignUInt64(uint64_t value) {
    Zero();
    for(int i = 0; value > 0; ++i) {
        RawBigit(i) = value & kBigitMask;
        value >>= kBigitSize;
        ++used_bigits_;
    }
}

void Bignum::AssignBignum(const Bignum& other) {
    exponent_ = other.exponent_;
    for(int i = 0; i < other.used_bigits_; ++i) {
        RawBigit(i) = other.RawBigit(i);
    }
    used_bigits_ = other.used_bigits_;
}

void Bignum::SubtractBignum(const Bignum& other) {
    assert(IsClamped());
    assert(other.IsClamped());
    // We require this to be bigger than other.
    assert(LessEqual(other, *this));

    Align(other);

    const int offset = other.exponent_ - exponent_;
    Chunk borrow = 0;
    int i;
    for(i = 0; i < other.used_bigits_; ++i) {
        assert((borrow == 0) || (borrow == 1));
        const Chunk difference =
            RawBigit(i + offset) - other.RawBigit(i) - borrow;
        RawBigit(i + offset) = difference & kBigitMask;
        borrow = difference >> (kChunkSize - 1);
    }
    while(borrow != 0) {
        const Chunk difference = RawBigit(i + offset) - borrow;
        RawBigit(i + offset) = difference & kBigitMask;
        borrow = difference >> (kChunkSize - 1);
        ++i;
    }
    Clamp();
}

void Bignum::ShiftLeft(const int shift_amount) {
    if(used_bigits_ == 0) {
        return;
    }
    exponent_ += (shift_amount / kBigitSize);
    const int local_shift = shift_amount % kBigitSize;
    EnsureCapacity(used_bigits_ + 1);
    BigitsShiftLeft(local_shift);
}

void Bignum::MultiplyByUInt32(const uint32_t factor) {
    if(factor == 1) {
        return;
    }
    if(factor == 0) {
        Zero();
        return;
    }
    if(used_bigits_ == 0) {
        return;
    }
    // The product of a bigit with the factor is of size kBigitSize + 32.
    // Assert that this number + 1 (for the carry) fits into double chunk.
    assert(kDoubleChunkSize >= kBigitSize + 32 + 1);
    DoubleChunk carry = 0;
    for(int i = 0; i < used_bigits_; ++i) {
        const DoubleChunk product =
            static_cast<DoubleChunk>(factor) * RawBigit(i) + carry;
        RawBigit(i) = static_cast<Chunk>(product & kBigitMask);
        carry = (product >> kBigitSize);
    }
    while(carry != 0) {
        EnsureCapacity(used_bigits_ + 1);
        RawBigit(used_bigits_) = carry & kBigitMask;
        used_bigits_++;
        carry >>= kBigitSize;
    }
}

void Bignum::MultiplyByUInt64(const uint64_t factor) {
    if(factor == 1) {
        return;
    }
    if(factor == 0) {
        Zero();
        return;
    }
    if(used_bigits_ == 0) {
        return;
    }
    assert(kBigitSize < 32);
    uint64_t carry = 0;
    const uint64_t low = factor & 0xFFFFFFFF;
    const uint64_t high = factor >> 32;
    for(int i = 0; i < used_bigits_; ++i) {
        const uint64_t product_low = low * RawBigit(i);
        const uint64_t product_high = high * RawBigit(i);
        const uint64_t tmp = (carry & kBigitMask) + product_low;
        RawBigit(i) = tmp & kBigitMask;
        carry = (carry >> kBigitSize) + (tmp >> kBigitSize)
            + (product_high << (32 - kBigitSize));
    }
    while(carry != 0) {
        EnsureCapacity(used_bigits_ + 1);
        RawBigit(used_bigits_) = carry & kBigitMask;
        used_bigits_++;
        carry >>= kBigitSize;
    }
}

void Bignum::MultiplyByPowerOfTen(const int exponent) {
    static const uint64_t kFive27 = 0x6765c793fa10079dllu;
    static const uint16_t kFive1 = 5;
    static const uint16_t kFive2 = kFive1 * 5;
    static const uint16_t kFive3 = kFive2 * 5;
    static const uint16_t kFive4 = kFive3 * 5;
    static const uint16_t kFive5 = kFive4 * 5;
    static const uint16_t kFive6 = kFive5 * 5;
    static const uint32_t kFive7 = kFive6 * 5;
    static const uint32_t kFive8 = kFive7 * 5;
    static const uint32_t kFive9 = kFive8 * 5;
    static const uint32_t kFive10 = kFive9 * 5;
    static const uint32_t kFive11 = kFive10 * 5;
    static const uint32_t kFive12 = kFive11 * 5;
    static const uint32_t kFive13 = kFive12 * 5;
    static const uint32_t kFive1_to_12[] = {kFive1, kFive2,  kFive3,  kFive4,
                                            kFive5, kFive6,  kFive7,  kFive8,
                                            kFive9, kFive10, kFive11, kFive12};

    assert(exponent >= 0);

    if(exponent == 0) {
        return;
    }
    if(used_bigits_ == 0) {
        return;
    }
    // We shift by exponent at the end just before returning.
    int remaining_exponent = exponent;
    while(remaining_exponent >= 27) {
        MultiplyByUInt64(kFive27);
        remaining_exponent -= 27;
    }
    while(remaining_exponent >= 13) {
        MultiplyByUInt32(kFive13);
        remaining_exponent -= 13;
    }
    if(remaining_exponent > 0) {
        MultiplyByUInt32(kFive1_to_12[remaining_exponent - 1]);
    }
    ShiftLeft(exponent);
}

void Bignum::Square() {
    assert(IsClamped());
    const int product_length = 2 * used_bigits_;
    EnsureCapacity(product_length);

    // Comba multiplication: compute each column separately.
    // Example: r = a2a1a0 * b2b1b0.
    //    r =  1    * a0b0 +
    //        10    * (a1b0 + a0b1) +
    //        100   * (a2b0 + a1b1 + a0b2) +
    //        1000  * (a2b1 + a1b2) +
    //        10000 * a2b2
    //
    // In the worst case we have to accumulate nb-digits products of
    // digit*digit.
    //
    // Assert that the additional number of bits in a DoubleChunk are enough to
    // sum up used_digits of Bigit*Bigit.
    if((1 << (2 * (kChunkSize - kBigitSize))) <= used_bigits_) {
        std::abort();
    }
    DoubleChunk accumulator = 0;
    // First shift the digits so we don't overwrite them.
    const int copy_offset = used_bigits_;
    for(int i = 0; i < used_bigits_; ++i) {
        RawBigit(copy_offset + i) = RawBigit(i);
    }
    // We have two loops to avoid some 'if's in the loop.
    for(int i = 0; i < used_bigits_; ++i) {
        // Process temporary digit i with power i.
        // The sum of the two indices must be equal to i.
        int bigit_index1 = i;
        int bigit_index2 = 0;
        // Sum all of the sub-products.
        while(bigit_index1 >= 0) {
            const Chunk chunk1 = RawBigit(copy_offset + bigit_index1);
            const Chunk chunk2 = RawBigit(copy_offset + bigit_index2);
            accumulator += static_cast<DoubleChunk>(chunk1) * chunk2;
            bigit_index1--;
            bigit_index2++;
        }
        RawBigit(i) = static_cast<Chunk>(accumulator) & kBigitMask;
        accumulator >>= kBigitSize;
    }
    for(int i = used_bigits_; i < product_length; ++i) {
        int bigit_index1 = used_bigits_ - 1;
        int bigit_index2 = i - bigit_index1;
        // Invariant: sum of both indices is again equal to i.
        // Inner loop runs 0 times on last iteration, emptying accumulator.
        while(bigit_index2 < used_bigits_) {
            const Chunk chunk1 = RawBigit(copy_offset + bigit_index1);
            const Chunk chunk2 = RawBigit(copy_offset + bigit_index2);
            accumulator += static_cast<DoubleChunk>(chunk1) * chunk2;
            bigit_index1--;
            bigit_index2++;
        }
        // The overwritten RawBigit(i) will never be read in further loop
        // iterations, because bigit_index1 and bigit_index2 are always greater
        // than i - used_bigits_.
        RawBigit(i) = static_cast<Chunk>(accumulator) & kBigitMask;
        accumulator >>= kBigitSize;
    }
    // Since the result was guaranteed to lie inside the number the
    // accumulator must be 0 now.
    assert(accumulator == 0);

    // Don't forget to update the used_digits and the exponent.
    used_bigits_ = product_length;
    exponent_ *= 2;
    Clamp();
}

void Bignum::AssignPowerUInt16(uint16_t base, const int power_exponent) {
    assert(base != 0);
    assert(power_exponent >= 0);
    if(power_exponent == 0) {
        AssignUInt16(1);
        return;
    }
    Zero();
    int shifts = 0;
    // We expect base to be in range 2-32, and most often to be 10.
    // It does not make much sense to implement different algorithms for
    // counting the bits.
    while((base & 1) == 0) {
        base >>= 1;
        shifts++;
    }
    int bit_size = 0;
    int tmp_base = base;
    while(tmp_base != 0) {
        tmp_base >>= 1;
        bit_size++;
    }
    const int final_size = bit_size * power_exponent;
    // 1 extra bigit for the shifting, and one for rounded final_size.
    EnsureCapacity(final_size / kBigitSize + 2);

    // Left to Right exponentiation.
    int mask = 1;
    while(power_exponent >= mask)
        mask <<= 1;

    // The mask is now pointing to the bit above the most significant 1-bit of
    // power_exponent.
    // Get rid of first 1-bit;
    mask >>= 2;
    uint64_t this_value = base;

    bool delayed_multiplication = false;
    const uint64_t max_32bits = 0xFFFFFFFF;
    while(mask != 0 && this_value <= max_32bits) {
        this_value = this_value * this_value;
        // Verify that there is enough space in this_value to perform the
        // multiplication.  The first bit_size bits must be 0.
        if((power_exponent & mask) != 0) {
            assert(bit_size > 0);
            const uint64_t base_bits_mask =
                ~((static_cast<uint64_t>(1) << (64 - bit_size)) - 1);
            const bool high_bits_zero = (this_value & base_bits_mask) == 0;
            if(high_bits_zero) {
                this_value *= base;
            }
            else {
                delayed_multiplication = true;
            }
        }
        mask >>= 1;
    }
    AssignUInt64(this_value);
    if(delayed_multiplication) {
        MultiplyByUInt32(base);
    }

    // Now do the same thing as a bignum.
    while(mask != 0) {
        Square();
        if((power_exponent & mask) != 0) {
            MultiplyByUInt32(base);
        }
        mask >>= 1;
    }

    // And finally add the saved shifts.
    ShiftLeft(shifts * power_exponent);
}

// Precondition: this/other < 16bit.
uint16_t Bignum::DivideModuloIntBignum(const Bignum& other) {
    assert(IsClamped());
    assert(other.IsClamped());
    assert(other.used_bigits_ > 0);

    // Easy case: if we have less digits than the divisor than the result is 0.
    // Note: this handles the case where this == 0, too.
    if(BigitLength() < other.BigitLength()) {
        return 0;
    }

    Align(other);

    uint16_t result = 0;

    // Start by removing multiples of 'other' until both numbers have the same
    // number of digits.
    while(BigitLength() > other.BigitLength()) {
        // This naive approach is extremely inefficient if `this` divided by
        // other is big. This function is implemented for doubleToString where
        // the result should be small (less than 10).
        assert(
            other.RawBigit(other.used_bigits_ - 1) >= ((1 << kBigitSize) / 16));
        assert(RawBigit(used_bigits_ - 1) < 0x10000);
        // Remove the multiples of the first digit.
        // Example this = 23 and other equals 9. -> Remove 2 multiples.
        result += static_cast<uint16_t>(RawBigit(used_bigits_ - 1));
        SubtractTimes(other, RawBigit(used_bigits_ - 1));
    }

    assert(BigitLength() == other.BigitLength());

    // Both bignums are at the same length now.
    // Since other has more than 0 digits we know that the access to
    // RawBigit(used_bigits_ - 1) is safe.
    const Chunk this_bigit = RawBigit(used_bigits_ - 1);
    const Chunk other_bigit = other.RawBigit(other.used_bigits_ - 1);

    if(other.used_bigits_ == 1) {
        // Shortcut for easy (and common) case.
        int quotient = this_bigit / other_bigit;
        RawBigit(used_bigits_ - 1) = this_bigit - other_bigit * quotient;
        assert(quotient < 0x10000);
        result += static_cast<uint16_t>(quotient);
        Clamp();
        return result;
    }

    const int division_estimate = this_bigit / (other_bigit + 1);
    assert(division_estimate < 0x10000);
    result += static_cast<uint16_t>(division_estimate);
    SubtractTimes(other, division_estimate);

    if(other_bigit * (division_estimate + 1) > this_bigit) {
        // No need to even try to subtract. Even if other's remaining digits
        // were 0 another subtraction would be too much.
        return result;
    }

    while(LessEqual(other, *this)) {
        SubtractBignum(other);
        result++;
    }
    return result;
}

template<typename S>
static int SizeInHexChars(S number) {
    assert(number > 0);
    int result = 0;
    while(number != 0) {
        number >>= 4;
        result++;
    }
    return result;
}

static char HexCharOfValue(const int value) {
    assert(0 <= value && value <= 16);
    if(value < 10) {
        return static_cast<char>(value + '0');
    }
    return static_cast<char>(value - 10 + 'A');
}

bool Bignum::ToHexString(char* buffer, const int buffer_size) const {
    assert(IsClamped());
    // Each bigit must be printable as separate hex-character.
    assert(kBigitSize % 4 == 0);
    static const int kHexCharsPerBigit = kBigitSize / 4;

    if(used_bigits_ == 0) {
        if(buffer_size < 2) {
            return false;
        }
        buffer[0] = '0';
        buffer[1] = '\0';
        return true;
    }
    // We add 1 for the terminating '\0' character.
    const int needed_chars = (BigitLength() - 1) * kHexCharsPerBigit
        + SizeInHexChars(RawBigit(used_bigits_ - 1)) + 1;
    if(needed_chars > buffer_size) {
        return false;
    }
    int string_index = needed_chars - 1;
    buffer[string_index--] = '\0';
    for(int i = 0; i < exponent_; ++i) {
        for(int j = 0; j < kHexCharsPerBigit; ++j) {
            buffer[string_index--] = '0';
        }
    }
    for(int i = 0; i < used_bigits_ - 1; ++i) {
        Chunk current_bigit = RawBigit(i);
        for(int j = 0; j < kHexCharsPerBigit; ++j) {
            buffer[string_index--] = HexCharOfValue(current_bigit & 0xF);
            current_bigit >>= 4;
        }
    }
    // And finally the last bigit.
    Chunk most_significant_bigit = RawBigit(used_bigits_ - 1);
    while(most_significant_bigit != 0) {
        buffer[string_index--] = HexCharOfValue(most_significant_bigit & 0xF);
        most_significant_bigit >>= 4;
    }
    return true;
}

Bignum::Chunk Bignum::BigitOrZero(const int index) const {
    if(index >= BigitLength()) {
        return 0;
    }
    if(index < exponent_) {
        return 0;
    }
    return RawBigit(index - exponent_);
}

int Bignum::Compare(const Bignum& a, const Bignum& b) {
    assert(a.IsClamped());
    assert(b.IsClamped());
    const int bigit_length_a = a.BigitLength();
    const int bigit_length_b = b.BigitLength();
    if(bigit_length_a < bigit_length_b) {
        return -1;
    }
    if(bigit_length_a > bigit_length_b) {
        return +1;
    }
    for(int i = bigit_length_a - 1; i >= (std::min)(a.exponent_, b.exponent_);
        --i) {
        const Chunk bigit_a = a.BigitOrZero(i);
        const Chunk bigit_b = b.BigitOrZero(i);
        if(bigit_a < bigit_b) {
            return -1;
        }
        if(bigit_a > bigit_b) {
            return +1;
        }
        // Otherwise they are equal up to this digit. Try the next digit.
    }
    return 0;
}

int Bignum::PlusCompare(const Bignum& a, const Bignum& b, const Bignum& c) {
    assert(a.IsClamped());
    assert(b.IsClamped());
    assert(c.IsClamped());
    if(a.BigitLength() < b.BigitLength()) {
        return PlusCompare(b, a, c);
    }
    if(a.BigitLength() + 1 < c.BigitLength()) {
        return -1;
    }
    if(a.BigitLength() > c.BigitLength()) {
        return +1;
    }
    // The exponent encodes 0-bigits. So if there are more 0-digits in 'a' than
    // 'b' has digits, then the bigit-length of 'a'+'b' must be equal to the one
    // of 'a'.
    if(a.exponent_ >= b.BigitLength() && a.BigitLength() < c.BigitLength()) {
        return -1;
    }

    Chunk borrow = 0;
    // Starting at min_exponent all digits are == 0. So no need to compare them.
    const int min_exponent =
        (std::min)((std::min)(a.exponent_, b.exponent_), c.exponent_);
    for(int i = c.BigitLength() - 1; i >= min_exponent; --i) {
        const Chunk chunk_a = a.BigitOrZero(i);
        const Chunk chunk_b = b.BigitOrZero(i);
        const Chunk chunk_c = c.BigitOrZero(i);
        const Chunk sum = chunk_a + chunk_b;
        if(sum > chunk_c + borrow) {
            return +1;
        }
        else {
            borrow = chunk_c + borrow - sum;
            if(borrow > 1) {
                return -1;
            }
            borrow <<= kBigitSize;
        }
    }
    if(borrow == 0) {
        return 0;
    }
    return -1;
}

void Bignum::Clamp() {
    while(used_bigits_ > 0 && RawBigit(used_bigits_ - 1) == 0) {
        used_bigits_--;
    }
    if(used_bigits_ == 0) {
        // Zero.
        exponent_ = 0;
    }
}

void Bignum::Align(const Bignum& other) {
    if(exponent_ > other.exponent_) {
        // If "X" represents a "hidden" bigit (by the exponent) then we are in
        // the following case (a == this, b == other): a:  aaaaaaXXXX   or a:
        // aaaaaXXX b:     bbbbbbX      b: bbbbbbbbXX We replace some of the
        // hidden digits (X) of a with 0 digits. a:  aaaaaa000X   or a: aaaaa0XX
        const int zero_bigits = exponent_ - other.exponent_;
        EnsureCapacity(used_bigits_ + zero_bigits);
        for(int i = used_bigits_ - 1; i >= 0; --i) {
            RawBigit(i + zero_bigits) = RawBigit(i);
        }
        for(int i = 0; i < zero_bigits; ++i) {
            RawBigit(i) = 0;
        }
        used_bigits_ += zero_bigits;
        exponent_ -= zero_bigits;

        assert(used_bigits_ >= 0);
        assert(exponent_ >= 0);
    }
}

void Bignum::BigitsShiftLeft(const int shift_amount) {
    assert(shift_amount < kBigitSize);
    assert(shift_amount >= 0);
    Chunk carry = 0;
    for(int i = 0; i < used_bigits_; ++i) {
        const Chunk new_carry = RawBigit(i) >> (kBigitSize - shift_amount);
        RawBigit(i) = ((RawBigit(i) << shift_amount) + carry) & kBigitMask;
        carry = new_carry;
    }
    if(carry != 0) {
        RawBigit(used_bigits_) = carry;
        used_bigits_++;
    }
}

void Bignum::SubtractTimes(const Bignum& other, const int factor) {
    assert(exponent_ <= other.exponent_);
    if(factor < 3) {
        for(int i = 0; i < factor; ++i) {
            SubtractBignum(other);
        }
        return;
    }
    Chunk borrow = 0;
    const int exponent_diff = other.exponent_ - exponent_;
    for(int i = 0; i < other.used_bigits_; ++i) {
        const DoubleChunk product =
            static_cast<DoubleChunk>(factor) * other.RawBigit(i);
        const DoubleChunk remove = borrow + product;
        const Chunk difference =
            RawBigit(i + exponent_diff) - (remove & kBigitMask);
        RawBigit(i + exponent_diff) = difference & kBigitMask;
        borrow = static_cast<Chunk>(
            (difference >> (kChunkSize - 1)) + (remove >> kBigitSize));
    }
    for(int i = other.used_bigits_ + exponent_diff; i < used_bigits_; ++i) {
        if(borrow == 0) {
            return;
        }
        const Chunk difference = RawBigit(i) - borrow;
        RawBigit(i) = difference & kBigitMask;
        borrow = difference >> (kChunkSize - 1);
    }
    Clamp();
}

// The procedure starts generating digits from the left to the right and stops
// when the generated digits yield the shortest decimal representation of v. A
// decimal representation of v is a number lying closer to v than to any other
// double, so it converts to v when read.
//
// This is true if d, the decimal representation, is between m- and m+, the
// upper and lower boundaries. d must be strictly between them if !is_even.
//           m- := (numerator - delta_minus) / denominator
//           m+ := (numerator + delta_plus) / denominator
//
// Precondition: 0 <= (numerator+delta_plus) / denominator < 10.
//   If 1 <= (numerator+delta_plus) / denominator < 10 then no leading 0 digit
//   will be produced. This should be the standard precondition.
void GenerateShortestDigits(
    double_format_context& dbl, Bignum& numerator, Bignum& denominator,
    Bignum* delta_minus, Bignum* delta_plus, bool is_even) {
    // Small optimization: if delta_minus and delta_plus are the same just reuse
    // one of the two bignums.
    if(Bignum::Equal(*delta_minus, *delta_plus)) {
        delta_plus = delta_minus;
    }
    for(;;) {
        uint16_t digit = numerator.DivideModuloIntBignum(denominator);
        assert(digit <= 9);
        // digit = numerator / denominator (integer division).
        // numerator = numerator % denominator.
        dbl.add_num_digit(digit);

        // Can we stop already?
        // If the remainder of the division is less than the distance to the
        // lower boundary we can stop. In this case we simply round down
        // (discarding the remainder). Similarly we test if we can round up
        // (using the upper boundary).
        bool in_delta_room_minus;
        bool in_delta_room_plus;
        if(is_even) {
            in_delta_room_minus = Bignum::LessEqual(numerator, *delta_minus);
        }
        else {
            in_delta_room_minus = Bignum::Less(numerator, *delta_minus);
        }
        if(is_even) {
            in_delta_room_plus =
                Bignum::PlusCompare(numerator, *delta_plus, denominator) >= 0;
        }
        else {
            in_delta_room_plus =
                Bignum::PlusCompare(numerator, *delta_plus, denominator) > 0;
        }
        if(!in_delta_room_minus && !in_delta_room_plus) {
            // Prepare for next iteration.
            numerator.Times10();
            delta_minus->Times10();
            // We optimized delta_plus to be equal to delta_minus (if they share
            // the same value). So don't multiply delta_plus if they point to
            // the same object.
            if(delta_minus != delta_plus) {
                delta_plus->Times10();
            }
        }
        else if(in_delta_room_minus && in_delta_room_plus) {
            // Let's see if 2*numerator < denominator.
            // If yes, then the next digit would be < 5 and we can round down.
            int compare =
                Bignum::PlusCompare(numerator, numerator, denominator);
            if(compare < 0) {
                // Remaining digits are less than .5. -> Round down (== do
                // nothing).
            }
            else if(compare > 0) {
                // Remaining digits are more than .5 of denominator. -> Round
                // up. Note that the last digit could not be a '9' as otherwise
                // the whole loop would have stopped earlier. We still have an
                // assert here in case the preconditions were not satisfied.
                assert(dbl.last_digit() != '9');
                dbl.round_up_last_digit();
            }
            else {
                // Halfway case.
                // TODO(floitsch): need a way to solve half-way cases.
                //   For now let's round towards even (since this is what Gay
                //   seems to do).

                if((dbl.last_digit() - '0') % 2 == 0) {
                    // Round down => Do nothing.
                }
                else {
                    assert(dbl.last_digit() != '9');
                    dbl.round_up_last_digit();
                }
            }
            return;
        }
        else if(in_delta_room_minus) {
            // Round down (== do nothing).
            return;
        }
        else { // in_delta_room_plus
            // Round up.
            // Note again that the last digit could not be '9' since this would
            // have stopped the loop earlier. We still have an
            // assert here, in case the preconditions were not
            // satisfied.
            assert(dbl.last_digit() != '9');
            dbl.round_up_last_digit();
            return;
        }
    }
}

// Let v = numerator / denominator < 10.
// Then we generate 'count' digits of d = x.xxxxx... (without the decimal point)
// from left to right. Once 'count' digits have been produced we decide wether
// to round up or down. Remainders of exactly .5 round upwards. Numbers such
// as 9.999999 propagate a carry all the way, and change the
// exponent (decimal_point), when rounding upwards.
void GenerateCountedDigits(
    double_format_context& dbl, int count, Bignum& numerator,
    Bignum& denominator) {
    assert(count >= 0);
    for(int i = 0; i < count - 1; ++i) {
        uint16_t digit = numerator.DivideModuloIntBignum(denominator);
        // digit is a uint16_t and therefore always positive.
        assert(digit <= 9);
        // digit = numerator / denominator (integer division).
        // numerator = numerator % denominator.
        dbl.add_num_digit(uint8_t(digit));
        // Prepare for next iteration.
        numerator.Times10();
    }
    // Generate the last digit.
    uint16_t digit = numerator.DivideModuloIntBignum(denominator);
    if(Bignum::PlusCompare(numerator, numerator, denominator) >= 0)
        ++digit;
    assert(digit <= 10);
    dbl.add_num_digit(uint8_t(digit));
    // Correct bad digits (in case we had a sequence of '9's). Propagate the
    // carry until we hat a non-'9' or til we reach the first digit.
    for(int i = count - 1; i > 0; --i) {
        if(!dbl.check_digit_overflow(i))
            break;
        dbl.digits[i] = byte('0');
        dbl.round_up_digit(i - 1);
    }
    if(dbl.check_digit_overflow(0)) {
        // Propagate a carry past the top place.
        dbl.set_digit(0, '1');
        ++dbl.decimal_point;
    }
}

// Generates 'requested_digits' after the decimal point. It might omit
// trailing '0's. If the input number is too small then no digits at all are
// generated (ex.: 2 fixed digits for 0.00001).
//
// Input verifies:  1 <= (numerator + delta) / denominator < 10.
void BignumToFixed(
    double_format_context& dbl, Bignum& numerator, Bignum& denominator) {
    // Note that we have to look at more than just the requested_digits, since
    // a number could be rounded up. Example: v=0.5 with requested_digits=0.
    // Even though the power of v equals 0 we can't just stop here.
    if(-(dbl.decimal_point) > dbl.requested_digits) {
        // The number is definitively too small.
        // Ex: 0.001 with requested_digits == 1.
        // Set decimal-point to -requested_digits. This is what Gay does.
        // Note that it should not have any effect anyways since the string is
        // empty.
        dbl.decimal_point = -dbl.requested_digits;
        return;
    }
    else if(-(dbl.decimal_point) == dbl.requested_digits) {
        // We only need to verify if the number rounds down or up.
        // Ex: 0.04 and 0.06 with requested_digits == 1.
        assert(dbl.decimal_point == -dbl.requested_digits);
        // Initially the fraction lies in range (1, 10]. Multiply the
        // denominator by 10 so that we can compare more easily.
        denominator.Times10();
        if(Bignum::PlusCompare(numerator, numerator, denominator) >= 0) {
            // If the fraction is >= 0.5 then we have to include the rounded
            // digit.
            dbl.add_digit('1');
            ++dbl.decimal_point;
        }
        else {
            // Note that we caught most of similar cases earlier.
        }
        return;
    }
    else {
        // The requested digits correspond to the digits after the point.
        // The variable 'needed_digits' includes the digits before the point.
        int needed_digits = dbl.decimal_point + dbl.requested_digits;
        GenerateCountedDigits(dbl, needed_digits, numerator, denominator);
    }
}

// Returns an estimation of k such that 10^(k-1) <= v < 10^k where
// v = f * 2^exponent and 2^52 <= f < 2^53.
// v is hence a normalized double with the given exponent. The output is an
// approximation for the exponent of the decimal approimation .digits * 10^k.
//
// The result might undershoot by 1 in which case 10^k <= v < 10^k+1.
// Note: this property holds for v's upper boundary m+ too.
//    10^k <= m+ < 10^k+1.
//   (see explanation below).
//
// Examples:
//  EstimatePower(0)   => 16
//  EstimatePower(-52) => 0
//
// Note: e >= 0 => EstimatedPower(e) > 0. No similar claim can be made for e<0.
int EstimatePower(int exponent) {
    // This function estimates log10 of v where v = f*2^e (with e == exponent).
    // Note that 10^floor(log10(v)) <= v, but v <= 10^ceil(log10(v)).
    // Note that f is bounded by its container size. Let p = 53 (the double's
    // significand size). Then 2^(p-1) <= f < 2^p.
    //
    // Given that log10(v) == log2(v)/log2(10) and e+(len(f)-1) is quite close
    // to log2(v) the function is simplified to (e+(len(f)-1)/log2(10)).
    // The computed number undershoots by less than 0.631 (when we compute log3
    // and not log10).
    //
    // Optimization: since we only need an approximated result this computation
    // can be performed on 64 bit integers. On x86/x64 architecture the speedup
    // is not really measurable, though.
    //
    // Since we want to avoid overshooting we decrement by 1e10 so that
    // floating-point imprecisions don't affect us.
    //
    // Explanation for v's boundary m+: the computation takes advantage of
    // the fact that 2^(p-1) <= f < 2^p. Boundaries still satisfy this
    // requirement (even for denormals where the delta can be much more
    // important).

    const double k1Log10 = 0.30102999566398114; // 1/lg(10)

    // For doubles len(f) == 53 (don't forget the hidden bit).
    const int kSignificandSize = Double::kSignificandSize;
    double estimate = ceil((exponent + kSignificandSize - 1) * k1Log10 - 1e-10);
    return static_cast<int>(estimate);
}

// See comments for InitialScaledStartValues.
void InitialScaledStartValuesPositiveExponent(
    uint64_t significand, int exponent, int estimated_power,
    bool need_boundary_deltas, Bignum* numerator, Bignum* denominator,
    Bignum* delta_minus, Bignum* delta_plus) {
    // A positive exponent implies a positive power.
    assert(estimated_power >= 0);
    // Since the estimated_power is positive we simply multiply the denominator
    // by 10^estimated_power.

    // numerator = v.
    numerator->AssignUInt64(significand);
    numerator->ShiftLeft(exponent);
    // denominator = 10^estimated_power.
    denominator->AssignPowerUInt16(10, estimated_power);

    if(need_boundary_deltas) {
        // Introduce a common denominator so that the deltas to the boundaries
        // are integers.
        denominator->ShiftLeft(1);
        numerator->ShiftLeft(1);
        // Let v = f * 2^e, then m+ - v = 1/2 * 2^e; With the common
        // denominator (of 2) delta_plus equals 2^e.
        delta_plus->AssignUInt16(1);
        delta_plus->ShiftLeft(exponent);
        // Same for delta_minus. The adjustments if f == 2^p-1 are done later.
        delta_minus->AssignUInt16(1);
        delta_minus->ShiftLeft(exponent);
    }
}

// See comments for InitialScaledStartValues
static void InitialScaledStartValuesNegativeExponentPositivePower(
    uint64_t significand, int exponent, int estimated_power,
    bool need_boundary_deltas, Bignum* numerator, Bignum* denominator,
    Bignum* delta_minus, Bignum* delta_plus) {
    // v = f * 2^e with e < 0, and with estimated_power >= 0.
    // This means that e is close to 0 (have a look at how estimated_power is
    // computed).

    // numerator = significand
    //  since v = significand * 2^exponent this is equivalent to
    //  numerator = v * / 2^-exponent
    numerator->AssignUInt64(significand);
    // denominator = 10^estimated_power * 2^-exponent (with exponent < 0)
    denominator->AssignPowerUInt16(10, estimated_power);
    denominator->ShiftLeft(-exponent);

    if(need_boundary_deltas) {
        // Introduce a common denominator so that the deltas to the boundaries
        // are integers.
        denominator->ShiftLeft(1);
        numerator->ShiftLeft(1);
        // Let v = f * 2^e, then m+ - v = 1/2 * 2^e; With the common
        // denominator (of 2) delta_plus equals 2^e.
        // Given that the denominator already includes v's exponent the distance
        // to the boundaries is simply 1.
        delta_plus->AssignUInt16(1);
        // Same for delta_minus. The adjustments if f == 2^p-1 are done later.
        delta_minus->AssignUInt16(1);
    }
}

// See comments for InitialScaledStartValues
static void InitialScaledStartValuesNegativeExponentNegativePower(
    uint64_t significand, int exponent, int estimated_power,
    bool need_boundary_deltas, Bignum* numerator, Bignum* denominator,
    Bignum* delta_minus, Bignum* delta_plus) {
    // Instead of multiplying the denominator with 10^estimated_power we
    // multiply all values (numerator and deltas) by 10^-estimated_power.

    // Use numerator as temporary container for power_ten.
    Bignum* power_ten = numerator;
    power_ten->AssignPowerUInt16(10, -estimated_power);

    if(need_boundary_deltas) {
        // Since power_ten == numerator we must make a copy of
        // 10^estimated_power before we complete the computation of the
        // numerator. delta_plus = delta_minus = 10^estimated_power
        delta_plus->AssignBignum(*power_ten);
        delta_minus->AssignBignum(*power_ten);
    }

    // numerator = significand * 2 * 10^-estimated_power
    //  since v = significand * 2^exponent this is equivalent to
    // numerator = v * 10^-estimated_power * 2 * 2^-exponent.
    // Remember: numerator has been abused as power_ten. So no need to assign it
    //  to itself.
    assert(numerator == power_ten);
    numerator->MultiplyByUInt64(significand);

    // denominator = 2 * 2^-exponent with exponent < 0.
    denominator->AssignUInt16(1);
    denominator->ShiftLeft(-exponent);

    if(need_boundary_deltas) {
        // Introduce a common denominator so that the deltas to the boundaries
        // are integers.
        numerator->ShiftLeft(1);
        denominator->ShiftLeft(1);
        // With this shift the boundaries have their correct value, since
        // delta_plus = 10^-estimated_power, and
        // delta_minus = 10^-estimated_power.
        // These assignments have been done earlier.
        // The adjustments if f == 2^p-1 (lower boundary is closer) are done
        // later.
    }
}

// Let v = significand * 2^exponent.
// Computes v / 10^estimated_power exactly, as a ratio of two bignums, numerator
// and denominator. The functions GenerateShortestDigits and
// GenerateCountedDigits will then convert this ratio to its decimal
// representation d, with the required accuracy.
// Then d * 10^estimated_power is the representation of v.
// (Note: the fraction and the estimated_power might get adjusted before
// generating the decimal representation.)
//
// The initial start values consist of:
//  - a scaled numerator: s.t. numerator/denominator == v / 10^estimated_power.
//  - a scaled (common) denominator.
//  optionally (used by GenerateShortestDigits to decide if it has the shortest
//  decimal converting back to v):
//  - v - m-: the distance to the lower boundary.
//  - m+ - v: the distance to the upper boundary.
//
// v, m+, m-, and therefore v - m- and m+ - v all share the same denominator.
//
// Let ep == estimated_power, then the returned values will satisfy:
//  v / 10^ep = numerator / denominator.
//  v's boundarys m- and m+:
//    m- / 10^ep == v / 10^ep - delta_minus / denominator
//    m+ / 10^ep == v / 10^ep + delta_plus / denominator
//  Or in other words:
//    m- == v - delta_minus * 10^ep / denominator;
//    m+ == v + delta_plus * 10^ep / denominator;
//
// Since 10^(k-1) <= v < 10^k    (with k == estimated_power)
//  or       10^k <= v < 10^(k+1)
//  we then have 0.1 <= numerator/denominator < 1
//           or    1 <= numerator/denominator < 10
//
// It is then easy to kickstart the digit-generation routine.
//
// The boundary-deltas are only filled if the mode equals BIGNUM_DTOA_SHORTEST
// or BIGNUM_DTOA_SHORTEST_SINGLE.

static void InitialScaledStartValues(
    uint64_t significand, int exponent, bool lower_boundary_is_closer,
    int estimated_power, bool need_boundary_deltas, Bignum* numerator,
    Bignum* denominator, Bignum* delta_minus, Bignum* delta_plus) {
    if(exponent >= 0) {
        InitialScaledStartValuesPositiveExponent(
            significand, exponent, estimated_power, need_boundary_deltas,
            numerator, denominator, delta_minus, delta_plus);
    }
    else if(estimated_power >= 0) {
        InitialScaledStartValuesNegativeExponentPositivePower(
            significand, exponent, estimated_power, need_boundary_deltas,
            numerator, denominator, delta_minus, delta_plus);
    }
    else {
        InitialScaledStartValuesNegativeExponentNegativePower(
            significand, exponent, estimated_power, need_boundary_deltas,
            numerator, denominator, delta_minus, delta_plus);
    }

    if(need_boundary_deltas && lower_boundary_is_closer) {
        // The lower boundary is closer at half the distance of "normal"
        // numbers. Increase the common denominator and adapt all but the
        // delta_minus.
        denominator->ShiftLeft(1); // *2
        numerator->ShiftLeft(1);   // *2
        delta_plus->ShiftLeft(1);  // *2
    }
}

// This routine multiplies numerator/denominator so that its values lies in the
// range 1-10. That is after a call to this function we have:
//    1 <= (numerator + delta_plus) /denominator < 10.
// Let numerator the input before modification and numerator' the argument
// after modification, then the output-parameter decimal_point is such that
//  numerator / denominator * 10^estimated_power ==
//    numerator' / denominator' * 10^(decimal_point - 1)
// In some cases estimated_power was too low, and this is already the case. We
// then simply adjust the power so that 10^(k-1) <= v < 10^k (with k ==
// estimated_power) but do not touch the numerator or denominator.
// Otherwise the routine multiplies the numerator and the deltas by 10.
void FixupMultiply10(
    int estimated_power, bool is_even, int* decimal_point, Bignum* numerator,
    Bignum* denominator, Bignum* delta_minus, Bignum* delta_plus) {
    bool in_range;
    if(is_even) {
        // For IEEE doubles half-way cases (in decimal system numbers ending
        // with 5) are rounded to the closest floating-point number with even
        // significand.
        in_range =
            Bignum::PlusCompare(*numerator, *delta_plus, *denominator) >= 0;
    }
    else {
        in_range =
            Bignum::PlusCompare(*numerator, *delta_plus, *denominator) > 0;
    }
    if(in_range) {
        // Since numerator + delta_plus >= denominator we already have
        // 1 <= numerator/denominator < 10. Simply update the estimated_power.
        *decimal_point = estimated_power + 1;
    }
    else {
        *decimal_point = estimated_power;
        numerator->Times10();
        if(Bignum::Equal(*delta_minus, *delta_plus)) {
            delta_minus->Times10();
            delta_plus->AssignBignum(*delta_minus);
        }
        else {
            delta_minus->Times10();
            delta_plus->Times10();
        }
    }
}

} // namespace

void bignum_dtoa(double_format_context& dbl, dtoa_mode mode) {
    uint64_t significand = dbl.significand;
    int exponent = dbl.exponent;
    bool lower_boundary_is_closer = dbl.lower_boundary_is_closer();

    bool need_boundary_deltas = mode == dtoa_mode::SHORTEST;

    bool is_even = (significand & 1) == 0;
    int normalized_exponent = NormalizedExponent(significand, exponent);
    // estimated_power might be too low by 1.
    int estimated_power = EstimatePower(normalized_exponent);

    // Shortcut for Fixed.
    // The requested digits correspond to the digits after the point. If the
    // number is much too small, then there is no need in trying to get any
    // digits.
    if(mode == dtoa_mode::FIXED
       && -estimated_power - 1 > dbl.requested_digits) {
        // Set decimal-point to -requested_digits. This is what Gay does.
        // Note that it should not have any effect anyways since the string is
        // empty.
        dbl.decimal_point = -dbl.requested_digits;
        return;
    }

    Bignum numerator;
    Bignum denominator;
    Bignum delta_minus;
    Bignum delta_plus;
    // Make sure the bignum can grow large enough. The smallest double equals
    // 4e-324. In this case the denominator needs fewer than 324*4 binary
    // digits. The maximum double is 1.7976931348623157e308 which needs fewer
    // than 308*4 binary digits.
    assert(Bignum::kMaxSignificantBits >= 324 * 4);
    InitialScaledStartValues(
        significand, exponent, lower_boundary_is_closer, estimated_power,
        need_boundary_deltas, &numerator, &denominator, &delta_minus,
        &delta_plus);
    // We now have v = (numerator / denominator) * 10^estimated_power.
    FixupMultiply10(
        estimated_power, is_even, &dbl.decimal_point, &numerator, &denominator,
        &delta_minus, &delta_plus);
    // We now have v = (numerator / denominator) * 10^(decimal_point-1), and
    //  1 <= (numerator + delta_plus) / denominator < 10
    switch(mode) {
    case dtoa_mode::SHORTEST:
        GenerateShortestDigits(
            dbl, numerator, denominator, &delta_minus, &delta_plus, is_even);
        break;
    case dtoa_mode::FIXED:
        BignumToFixed(dbl, numerator, denominator);
        break;
    case dtoa_mode::PRECISION:
        GenerateCountedDigits(
            dbl, dbl.requested_digits, numerator, denominator);
        break;
    default:
        break;
    }
}

} // namespace detail
} // namespace fmt
} // namespace univang
