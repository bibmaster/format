// Copyright 2012 the V8 project authors. All rights reserved.
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

#include "format_double.hpp"
#include "format_integer.hpp"

namespace univang {
namespace fmt {
namespace detail {

namespace {

// Represents a 128bit type. This class should be replaced by a native type on
// platforms that support 128bit integers.
struct uint128_t {
    uint64_t hi, lo;
};

template<class T>
void write_digits(double_format_context& dbl, T number) {
    char tmp[sizeof(T) * 3];
    auto pos = write_dec(tmp, sizeof(tmp), number);
    auto size = sizeof(tmp) - pos;
    std::memcpy(dbl.digits + dbl.digit_count, tmp + pos, size);
    dbl.digit_count += size;
}

void write_17_digits(double_format_context& dbl, uint64_t number) {
    char tmp[20];
    auto pos = write_dec(tmp, sizeof(tmp), number);
    auto size = sizeof(tmp) - pos;
    assert(size <= 17);
    auto* digits = dbl.digits + dbl.digit_count;
    auto padding = 17 - size;
    std::memset(digits, '0', padding);
    std::memcpy(digits + padding, tmp + pos, size);
    dbl.digit_count += 17;
}

void round_up(double_format_context& dbl) {
    // An empty buffer represents 0.
    if(dbl.digit_count == 0) {
        dbl.add_digit('1');
        dbl.decimal_point = 1;
        return;
    }
    if(dbl.round_up())
        ++dbl.decimal_point;
}

// The given fractionals number represents a fixed-point number with binary
// point at bit (-exponent).
// Preconditions:
//   -128 <= exponent <= 0.
//   0 <= fractionals * 2^exponent < 1
//   The buffer holds the result.
// The function will round its result. During the rounding-process digits not
// generated by this function might be updated, and the decimal-point variable
// might be updated. If this function generates the digits 99 and the buffer
// already contained "199" (thus yielding a buffer of "19999") then a
// rounding-up will change the contents of the buffer to "20000".
void write_fraction_digits(double_format_context& dbl, uint64_t fractionals) {
    int exponent = dbl.exponent;
    int fractional_count = dbl.requested_digits;
    assert(-128 <= exponent && exponent <= 0);
    // 'fractionals' is a fixed-point number, with binary point at bit
    // (-exponent). Inside the function the non-converted remainder of
    // fractionals is a fixed-point number, with binary point at bit 'point'.
    if(-exponent <= 64) {
        // One 64 bit number is sufficient.
        assert(fractionals >> 56 == 0);
        int point = -exponent;
        for(int i = 0; i < fractional_count; ++i) {
            if(fractionals == 0)
                break;
            // Instead of multiplying by 10 we multiply by 5 and adjust the
            // point location. This way the fractionals variable will not
            // overflow. Invariant at the beginning of the loop: fractionals <
            // 2^point. Initially we have: point <= 64 and fractionals < 2^56
            // After each iteration the point is decremented by one.
            // Note that 5^3 = 125 < 128 = 2^7.
            // Therefore three iterations of this loop will not overflow
            // fractionals (even without the subtraction at the end of the loop
            // body). At this time point will satisfy point <= 61 and therefore
            // fractionals < 2^point and any further multiplication of
            // fractionals by 5 will not overflow.
            fractionals *= 5;
            point--;
            uint8_t digit = static_cast<uint8_t>(fractionals >> point);
            assert(digit <= 9);
            dbl.add_num_digit(digit);
            fractionals -= static_cast<uint64_t>(digit) << point;
        }
        // If the first bit after the point is set we have to round up.
        assert(fractionals == 0 || point - 1 >= 0);
        if((fractionals != 0) && ((fractionals >> (point - 1)) & 1) == 1)
            round_up(dbl);
        return;
    }
    // We need 128 bits.
    assert(64 < -exponent && -exponent <= 128);
    uint128_t f128{fractionals >> (-exponent - 64),
                   fractionals << (128 + exponent)};
    int point = 128;
    for(int i = 0; i < fractional_count; ++i) {
        if(f128.hi == 0 && f128.lo == 0)
            break;
        // x *= 5.
        const uint32_t mask = 0xffffffff;
        uint64_t accumulator = (f128.lo & mask) * 5;
        uint32_t part = static_cast<uint32_t>(5 & mask);
        accumulator >>= 32;
        accumulator += (f128.lo >> 32) * 5;
        f128.lo = (accumulator << 32) + part;
        accumulator >>= 32;
        accumulator += (f128.hi & mask) * 5;
        part = static_cast<uint32_t>(accumulator & mask);
        accumulator >>= 32;
        accumulator += (f128.hi >> 32) * 5;
        f128.hi = (accumulator << 32) + part;
        assert((accumulator >> 32) == 0);
        point--;

        // digit = x / 2^pow, x = x % 2^pow
        uint8_t digit;
        if(point >= 64) {
            digit = static_cast<uint8_t>(f128.hi >> (point - 64));
            f128.hi -= static_cast<uint64_t>(digit) << (point - 64);
        }
        else {
            uint64_t part_low = f128.lo >> point;
            uint64_t part_high = f128.hi << (64 - point);
            digit = static_cast<uint8_t>(part_low + part_high);
            f128.hi = 0;
            f128.lo -= part_low << point;
        }
        assert(digit <= 9);
        dbl.add_num_digit(digit);
    }
    --point;
    auto part = f128.hi;
    if(point >= 64)
        point -= 64;
    else
        part = f128.lo;
    if((part >> point) & 1)
        round_up(dbl);
}

} // namespace

bool fast_fixed_dtoa(double_format_context& dbl) {
    const uint32_t kMaxUInt32 = 0xFFFFFFFF;
    // v = significand * 2^exponent (with significand a 53bit integer).
    // If the exponent is larger than 20 (i.e. we may have a 73bit number) then
    // we don't know how to compute the representation. 2^73 ~= 9.5*10^21. If
    // necessary this limit could probably be increased, but we don't need more.
    if(dbl.exponent > 20)
        return false;
    if(dbl.requested_digits > 20)
        return false;
    // At most kDoubleSignificandSize bits of the significand are non-zero.
    // Given a 64 bit integer we have 11 0s followed by 53 potentially non-zero
    // bits:  0..11*..0xxx..53*..xx
    auto exponent = dbl.exponent;
    auto significand = dbl.significand;
    if(exponent + Double::kSignificandSize > 64) {
        // The exponent must be > 11.
        //
        // We know that v = significand * 2^exponent.
        // And the exponent > 11.
        // We simplify the task by dividing v by 10^17.
        // The quotient delivers the first digits, and the remainder fits into a
        // 64 bit number. Dividing by 10^17 is equivalent to dividing by
        // 5^17*2^17.
        // const uint64_t kFive17 = 0x000000B1A2BC2EC5llu; // 5^17
        uint64_t divisor = 0x000000B1A2BC2EC5llu; // 5^17
        int divisor_power = 17;
        uint64_t dividend = significand;
        uint32_t quotient;
        uint64_t remainder;
        // Let v = f * 2^e with f == significand and e == exponent.
        // Then need q (quotient) and r (remainder) as follows:
        //   v            = q * 10^17       + r
        //   f * 2^e      = q * 10^17       + r
        //   f * 2^e      = q * 5^17 * 2^17 + r
        // If e > 17 then
        //   f * 2^(e-17) = q * 5^17        + r/2^17
        // else
        //   f  = q * 5^17 * 2^(17-e) + r/2^e
        if(exponent > divisor_power) {
            // We only allow exponents of up to 20 and therefore (17 - e) <= 3
            dividend <<= exponent - divisor_power;
            quotient = static_cast<uint32_t>(dividend / divisor);
            remainder = (dividend % divisor) << divisor_power;
        }
        else {
            divisor <<= divisor_power - exponent;
            quotient = static_cast<uint32_t>(dividend / divisor);
            remainder = (dividend % divisor) << exponent;
        }
        write_digits(dbl, quotient);
        write_17_digits(dbl, remainder);
        dbl.decimal_point = static_cast<int>(dbl.digit_count);
    }
    else if(exponent >= 0) {
        // 0 <= exponent <= 11
        significand <<= exponent;
        write_digits(dbl, significand);
        dbl.decimal_point = static_cast<int>(dbl.digit_count);
    }
    else if(exponent > -Double::kSignificandSize) {
        // We have to cut the number.
        uint64_t integrals = significand >> -exponent;
        uint64_t fractionals = significand - (integrals << -exponent);
        write_digits(dbl, integrals);
        dbl.decimal_point = static_cast<int>(dbl.digit_count);
        write_fraction_digits(dbl, fractionals);
    }
    else if(exponent < -128) {
        // This configuration (with at most 20 digits) means that all digits
        // must be 0.
        assert(dbl.requested_digits <= 20);
        dbl.digit_count = 0;
        dbl.decimal_point = -dbl.requested_digits;
    }
    else {
        dbl.decimal_point = 0;
        write_fraction_digits(dbl, significand);
    }

    // trim leading/trailing zeros
    while(dbl.digit_count != 0 && dbl.last_digit() == '0')
        --dbl.digit_count;
    if(dbl.digit_count == 0)
        // The string is empty and the decimal_point thus has no importance.
        // Mimick Gay's dtoa and and set it to -fractional_count.
        dbl.decimal_point = -dbl.requested_digits;
    else if(dbl.first_digit() == '0') {
        unsigned leading_zeros = 1;
        while(leading_zeros < dbl.digit_count
              && dbl.digit(leading_zeros) == '0')
            ++leading_zeros;
        dbl.digit_count -= leading_zeros;
        dbl.decimal_point -= leading_zeros;
        std::memmove(dbl.digits, dbl.digits + leading_zeros, dbl.digit_count);
    }

    return true;
}

} // namespace detail
} // namespace fmt
} // namespace univang
