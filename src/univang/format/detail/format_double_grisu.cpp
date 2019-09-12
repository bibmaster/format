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

namespace univang {
namespace fmt {
namespace detail {

namespace {

using byte = double_format_context::byte;

// This "Do It Yourself Floating Point" class implements a floating-point number
// with a uint64 significand and an int exponent. Normalized DiyFp numbers will
// have the most significant bit of the significand set.
// Multiplication and Subtraction do not normalize their results.
// DiyFp store only non-negative numbers and are not designed to contain special
// doubles (NaN and Infinity).
class DiyFp {
public:
    constexpr static const int kSignificandSize = 64;

    constexpr DiyFp() noexcept : f_(0), e_(0) {
    }
    constexpr DiyFp(const uint64_t significand, const int32_t exponent) noexcept
        : f_(significand), e_(exponent) {
    }

    // The value encoded by this Double must be strictly greater than 0.
    static DiyFp MakeNormalized(const double_format_context& dbl) noexcept {
        auto f = dbl.significand;
        auto e = dbl.exponent;
        while((f & Double::kHiddenBit) == 0) {
            f <<= 1;
            --e;
        }
        // Do the final shifts in one go.
        f <<= kSignificandSize - Double::kSignificandSize;
        e -= kSignificandSize - Double::kSignificandSize;
        return DiyFp(f, e);
    }

    // Computes the two boundaries of this.
    // The bigger boundary (m_plus) is normalized. The lower boundary has the
    // same exponent as m_plus. Precondition: the value encoded by this Double
    // must be greater than 0.
    static void NormalizedBoundaries(
        const double_format_context& dbl, DiyFp* out_m_minus,
        DiyFp* out_m_plus) noexcept {
        // The boundary is closer if the significand is of the form f == 2^p-1
        // then the lower boundary is closer. Think of v = 1000e10 and v- =
        // 9999e9. Then the boundary (== (v - v-)/2) is not just at a distance
        // of 1e9 but at a distance of 1e8. The only exception is for the
        // smallest normal: the largest denormal is at the same distance as its
        // successor. Note: denormals have the same exponent as the smallest
        // normals.
        bool lower_boundary_is_closer = dbl.lower_boundary_is_closer();
        DiyFp v = DiyFp(dbl.significand, dbl.exponent);
        DiyFp m_plus = DiyFp::Normalize(DiyFp((v.f() << 1) + 1, v.e() - 1));
        DiyFp m_minus;
        if(lower_boundary_is_closer)
            m_minus = DiyFp((v.f() << 2) - 1, v.e() - 2);
        else
            m_minus = DiyFp((v.f() << 1) - 1, v.e() - 1);
        m_minus.set_f(m_minus.f() << (m_minus.e() - m_plus.e()));
        m_minus.set_e(m_plus.e());
        *out_m_plus = m_plus;
        *out_m_minus = m_minus;
    }

    // this -= other.
    // The exponents of both numbers must be the same and the significand of
    // this must be greater or equal than the significand of other. The result
    // will not be normalized.
    void Subtract(const DiyFp& other) noexcept {
        assert(e_ == other.e_);
        assert(f_ >= other.f_);
        f_ -= other.f_;
    }

    // Returns a - b.
    // The exponents of both numbers must be the same and a must be greater
    // or equal than b. The result will not be normalized.
    static DiyFp Minus(const DiyFp& a, const DiyFp& b) noexcept {
        DiyFp result = a;
        result.Subtract(b);
        return result;
    }

    // this *= other.
    void Multiply(const DiyFp& other) noexcept {
        // Simply "emulates" a 128 bit multiplication.
        // However: the resulting number only contains 64 bits. The least
        // significant 64 bits are only used for rounding the most significant
        // 64 bits.
        const uint64_t kM32 = 0xFFFFFFFFU;
        const uint64_t a = f_ >> 32;
        const uint64_t b = f_ & kM32;
        const uint64_t c = other.f_ >> 32;
        const uint64_t d = other.f_ & kM32;
        const uint64_t ac = a * c;
        const uint64_t bc = b * c;
        const uint64_t ad = a * d;
        const uint64_t bd = b * d;
        // By adding 1U << 31 to tmp we round the final result.
        // Halfway cases will be rounded up.
        const uint64_t tmp =
            (bd >> 32) + (ad & kM32) + (bc & kM32) + (1U << 31);
        e_ += other.e_ + 64;
        f_ = ac + (ad >> 32) + (bc >> 32) + (tmp >> 32);
    }

    // returns a * b;
    static DiyFp Times(const DiyFp& a, const DiyFp& b) noexcept {
        DiyFp result = a;
        result.Multiply(b);
        return result;
    }

    void Normalize() noexcept {
        assert(f_ != 0);
        uint64_t significand = f_;
        int32_t exponent = e_;

        // This method is mainly called for normalizing boundaries. In general,
        // boundaries need to be shifted by 10 bits, and we optimize for this
        // case.
        const uint64_t k10MSBits = 0xFFC0000000000000llu;
        while((significand & k10MSBits) == 0) {
            significand <<= 10;
            exponent -= 10;
        }
        while((significand & kUint64MSB) == 0) {
            significand <<= 1;
            exponent--;
        }
        f_ = significand;
        e_ = exponent;
    }

    static DiyFp Normalize(const DiyFp& a) noexcept {
        DiyFp result = a;
        result.Normalize();
        return result;
    }

    uint64_t f() const {
        return f_;
    }
    int32_t e() const {
        return e_;
    }

    void set_f(uint64_t new_value) {
        f_ = new_value;
    }
    void set_e(int32_t new_value) {
        e_ = new_value;
    }

private:
    static constexpr const uint64_t kUint64MSB = 0x8000000000000000llu;

    uint64_t f_;
    int32_t e_;
};

namespace PowersOfTenCache {

struct CachedPower {
    uint64_t significand;
    int16_t binary_exponent;
    int16_t decimal_exponent;
};

constexpr CachedPower kCachedPowers[] = {
    {0xfa8fd5a0081c0288llu, -1220, -348}, {0xbaaee17fa23ebf76llu, -1193, -340},
    {0x8b16fb203055ac76llu, -1166, -332}, {0xcf42894a5dce35eallu, -1140, -324},
    {0x9a6bb0aa55653b2dllu, -1113, -316}, {0xe61acf033d1a45dfllu, -1087, -308},
    {0xab70fe17c79ac6callu, -1060, -300}, {0xff77b1fcbebcdc4fllu, -1034, -292},
    {0xbe5691ef416bd60cllu, -1007, -284}, {0x8dd01fad907ffc3cllu, -980, -276},
    {0xd3515c2831559a83llu, -954, -268},  {0x9d71ac8fada6c9b5llu, -927, -260},
    {0xea9c227723ee8bcbllu, -901, -252},  {0xaecc49914078536dllu, -874, -244},
    {0x823c12795db6ce57llu, -847, -236},  {0xc21094364dfb5637llu, -821, -228},
    {0x9096ea6f3848984fllu, -794, -220},  {0xd77485cb25823ac7llu, -768, -212},
    {0xa086cfcd97bf97f4llu, -741, -204},  {0xef340a98172aace5llu, -715, -196},
    {0xb23867fb2a35b28ellu, -688, -188},  {0x84c8d4dfd2c63f3bllu, -661, -180},
    {0xc5dd44271ad3cdballu, -635, -172},  {0x936b9fcebb25c996llu, -608, -164},
    {0xdbac6c247d62a584llu, -582, -156},  {0xa3ab66580d5fdaf6llu, -555, -148},
    {0xf3e2f893dec3f126llu, -529, -140},  {0xb5b5ada8aaff80b8llu, -502, -132},
    {0x87625f056c7c4a8bllu, -475, -124},  {0xc9bcff6034c13053llu, -449, -116},
    {0x964e858c91ba2655llu, -422, -108},  {0xdff9772470297ebdllu, -396, -100},
    {0xa6dfbd9fb8e5b88fllu, -369, -92},   {0xf8a95fcf88747d94llu, -343, -84},
    {0xb94470938fa89bcfllu, -316, -76},   {0x8a08f0f8bf0f156bllu, -289, -68},
    {0xcdb02555653131b6llu, -263, -60},   {0x993fe2c6d07b7facllu, -236, -52},
    {0xe45c10c42a2b3b06llu, -210, -44},   {0xaa242499697392d3llu, -183, -36},
    {0xfd87b5f28300ca0ellu, -157, -28},   {0xbce5086492111aebllu, -130, -20},
    {0x8cbccc096f5088ccllu, -103, -12},   {0xd1b71758e219652cllu, -77, -4},
    {0x9c40000000000000llu, -50, 4},      {0xe8d4a51000000000llu, -24, 12},
    {0xad78ebc5ac620000llu, 3, 20},       {0x813f3978f8940984llu, 30, 28},
    {0xc097ce7bc90715b3llu, 56, 36},      {0x8f7e32ce7bea5c70llu, 83, 44},
    {0xd5d238a4abe98068llu, 109, 52},     {0x9f4f2726179a2245llu, 136, 60},
    {0xed63a231d4c4fb27llu, 162, 68},     {0xb0de65388cc8ada8llu, 189, 76},
    {0x83c7088e1aab65dbllu, 216, 84},     {0xc45d1df942711d9allu, 242, 92},
    {0x924d692ca61be758llu, 269, 100},    {0xda01ee641a708deallu, 295, 108},
    {0xa26da3999aef774allu, 322, 116},    {0xf209787bb47d6b85llu, 348, 124},
    {0xb454e4a179dd1877llu, 375, 132},    {0x865b86925b9bc5c2llu, 402, 140},
    {0xc83553c5c8965d3dllu, 428, 148},    {0x952ab45cfa97a0b3llu, 455, 156},
    {0xde469fbd99a05fe3llu, 481, 164},    {0xa59bc234db398c25llu, 508, 172},
    {0xf6c69a72a3989f5cllu, 534, 180},    {0xb7dcbf5354e9becellu, 561, 188},
    {0x88fcf317f22241e2llu, 588, 196},    {0xcc20ce9bd35c78a5llu, 614, 204},
    {0x98165af37b2153dfllu, 641, 212},    {0xe2a0b5dc971f303allu, 667, 220},
    {0xa8d9d1535ce3b396llu, 694, 228},    {0xfb9b7cd9a4a7443cllu, 720, 236},
    {0xbb764c4ca7a44410llu, 747, 244},    {0x8bab8eefb6409c1allu, 774, 252},
    {0xd01fef10a657842cllu, 800, 260},    {0x9b10a4e5e9913129llu, 827, 268},
    {0xe7109bfba19c0c9dllu, 853, 276},    {0xac2820d9623bf429llu, 880, 284},
    {0x80444b5e7aa7cf85llu, 907, 292},    {0xbf21e44003acdd2dllu, 933, 300},
    {0x8e679c2f5e44ff8fllu, 960, 308},    {0xd433179d9c8cb841llu, 986, 316},
    {0x9e19db92b4e31ba9llu, 1013, 324},   {0xeb96bf6ebadf77d9llu, 1039, 332},
    {0xaf87023b9bf0ee6bllu, 1066, 340},
};

// -1 * the first decimal_exponent.
constexpr const int kCachedPowersOffset = 348;
//  1 / lg(10)
constexpr const double kD_1_LOG2_10 = 0.30102999566398114;

// Not all powers of ten are cached. The decimal exponent of two neighboring
// cached numbers will differ by kDecimalExponentDistance.
constexpr const int kDecimalExponentDistance = 8;

constexpr const int kMinDecimalExponent = -348;
constexpr const int kMaxDecimalExponent = 340;

// Returns a cached power-of-ten with a binary exponent in the range
// [min_exponent; max_exponent] (boundaries included).
void GetCachedPowerForBinaryExponentRange(
    int min_exponent, int max_exponent, DiyFp* power, int* decimal_exponent) {
    int kQ = DiyFp::kSignificandSize;
    double k = ceil((min_exponent + kQ - 1) * kD_1_LOG2_10);
    int foo = kCachedPowersOffset;
    int index = (foo + static_cast<int>(k) - 1) / kDecimalExponentDistance + 1;
    assert(0 <= index && index < static_cast<int>(std::size(kCachedPowers)));
    CachedPower cached_power = kCachedPowers[index];
    assert(min_exponent <= cached_power.binary_exponent);
    (void)max_exponent; // Mark variable as used.
    assert(cached_power.binary_exponent <= max_exponent);
    *decimal_exponent = cached_power.decimal_exponent;
    *power = DiyFp(cached_power.significand, cached_power.binary_exponent);
}

} // namespace PowersOfTenCache

// The minimal and maximal target exponent define the range of w's binary
// exponent, where 'w' is the result of multiplying the input by a cached power
// of ten.
//
// A different range might be chosen on a different platform, to optimize digit
// generation, but a smaller range requires more powers of ten to be cached.
constexpr const int kMinimalTargetExponent = -60;
constexpr const int kMaximalTargetExponent = -32;

// Adjusts the last digit of the generated number, and screens out generated
// solutions that may be inaccurate. A solution may be inaccurate if it is
// outside the safe interval, or if we cannot prove that it is closer to the
// input than a neighboring representation of the same length.
//
// Input: * buffer containing the digits of too_high / 10^kappa
//        * the buffer's length
//        * distance_too_high_w == (too_high - w).f() * unit
//        * unsafe_interval == (too_high - too_low).f() * unit
//        * rest = (too_high - buffer * 10^kappa).f() * unit
//        * ten_kappa = 10^kappa * unit
//        * unit = the common multiplier
// Output: returns true if the buffer is guaranteed to contain the closest
//    representable number to the input.
//  Modifies the generated digits in the buffer to approach (round towards) w.
bool RoundWeed(
    double_format_context& dbl, uint64_t distance_too_high_w,
    uint64_t unsafe_interval, uint64_t rest, uint64_t ten_kappa,
    uint64_t unit) {
    uint64_t small_distance = distance_too_high_w - unit;
    uint64_t big_distance = distance_too_high_w + unit;
    // Let w_low  = too_high - big_distance, and
    //     w_high = too_high - small_distance.
    // Note: w_low < w < w_high
    //
    // The real w (* unit) must lie somewhere inside the interval
    // ]w_low; w_high[ (often written as "(w_low; w_high)")

    // Basically the buffer currently contains a number in the unsafe interval
    // ]too_low; too_high[ with too_low < w < too_high
    //
    //  too_high - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    //  -
    //                     ^v 1 unit            ^      ^                 ^ ^
    //  boundary_high ---------------------     .      .                 . .
    //                     ^v 1 unit            .      .                 . .
    //   - - - - - - - - - - - - - - - - - - -  +  - - + - - - - - -     . .
    //                                          .      .         ^       . . .
    //                                          big_distance  .       .      .
    //                                          .      .         .       . rest
    //                              small_distance     .         .       . .
    //                                          v      .         .       . .
    //  w_high - - - - - - - - - - - - - - - - - -     .         .       . .
    //                     ^v 1 unit                   .         .       . .
    //  w ----------------------------------------     .         .       . .
    //                     ^v 1 unit                   v         .       . .
    //  w_low  - - - - - - - - - - - - - - - - - - - - -         .       . .
    //                                                           .       . v
    //  buffer
    //  --------------------------------------------------+-------+--------
    //                                                           .       .
    //                                                  safe_interval    .
    //                                                           v       .
    //   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -     .
    //                     ^v 1 unit                                     .
    //  boundary_low ------------------------- unsafe_interval
    //                     ^v 1 unit                                     v
    //  too_low  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    //  -
    //
    //
    // Note that the value of buffer could lie anywhere inside the range too_low
    // to too_high.
    //
    // boundary_low, boundary_high and w are approximations of the real
    // boundaries and v (the input number). They are guaranteed to be precise up
    // to one unit. In fact the error is guaranteed to be strictly less than one
    // unit.
    //
    // Anything that lies outside the unsafe interval is guaranteed not to round
    // to v when read again.
    // Anything that lies inside the safe interval is guaranteed to round to v
    // when read again.
    // If the number inside the buffer lies inside the unsafe interval but not
    // inside the safe interval then we simply do not know and bail out
    // (returning false).
    //
    // Similarly we have to take into account the imprecision of 'w' when
    // finding the closest representation of 'w'. If we have two potential
    // representations, and one is closer to both w_low and w_high, then we know
    // it is closer to the actual value v.
    //
    // By generating the digits of too_high we got the largest (closest to
    // too_high) buffer that is still in the unsafe interval. In the case where
    // w_high < buffer < too_high we try to decrement the buffer.
    // This way the buffer approaches (rounds towards) w.
    // There are 3 conditions that stop the decrementation process:
    //   1) the buffer is already below w_high
    //   2) decrementing the buffer would make it leave the unsafe interval
    //   3) decrementing the buffer would yield a number below w_high and
    //   farther
    //      away than the current number. In other words:
    //              (buffer{-1} < w_high) && w_high - buffer{-1} > buffer -
    //              w_high
    // Instead of using the buffer directly we use its distance to too_high.
    // Conceptually rest ~= too_high - buffer
    // We need to do the following tests in this order to avoid over- and
    // underflows.
    assert(rest <= unsafe_interval);
    while(rest < small_distance &&               // Negated condition 1
          unsafe_interval - rest >= ten_kappa && // Negated condition 2
          (rest + ten_kappa < small_distance ||  // buffer{-1} > w_high
           small_distance - rest >= rest + ten_kappa - small_distance)) {
        dbl.round_down_last_digit();
        rest += ten_kappa;
    }

    // We have approached w+ as much as possible. We now test if approaching w-
    // would require changing the buffer. If yes, then we have two possible
    // representations close to w, but we cannot decide which one is closer.
    if(rest < big_distance && unsafe_interval - rest >= ten_kappa
       && (rest + ten_kappa < big_distance
           || big_distance - rest > rest + ten_kappa - big_distance)) {
        return false;
    }

    // Weeding test.
    //   The safe interval is [too_low + 2 ulp; too_high - 2 ulp]
    //   Since too_low = too_high - unsafe_interval this is equivalent to
    //      [too_high - unsafe_interval + 4 ulp; too_high - 2 ulp]
    //   Conceptually we have: rest ~= too_high - buffer
    return (2 * unit <= rest) && (rest <= unsafe_interval - 4 * unit);
}

// Rounds the buffer upwards if the result is closer to v by possibly adding
// 1 to the buffer. If the precision of the calculation is not sufficient to
// round correctly, return false.
// The rounding might shift the whole buffer in which case the kappa is
// adjusted. For example "99", kappa = 3 might become "10", kappa = 4.
//
// If 2*rest > ten_kappa then the buffer needs to be round up.
// rest can have an error of +/- 1 unit. This function accounts for the
// imprecision and returns false, if the rounding direction cannot be
// unambiguously determined.
//
// Precondition: rest < ten_kappa.
bool RoundWeedCounted(
    double_format_context& dbl, uint64_t rest, uint64_t ten_kappa,
    uint64_t unit, int& kappa) {
    assert(rest < ten_kappa);
    // The following tests are done in a specific order to avoid overflows. They
    // will work correctly with any uint64 values of rest < ten_kappa and unit.
    //
    // If the unit is too big, then we don't know which way to round. For
    // example a unit of 50 means that the real number lies within rest +/- 50.
    // If 10^kappa == 40 then there is no way to tell which way to round.
    if(unit >= ten_kappa)
        return false;
    // Even if unit is just half the size of 10^kappa we are already completely
    // lost. (And after the previous test we know that the expression will not
    // over/underflow.)
    if(ten_kappa - unit <= unit)
        return false;

    // If 2 * (rest + unit) <= 10^kappa we can safely round down.
    if((ten_kappa - rest > rest) && (ten_kappa - 2 * rest >= 2 * unit))
        return true;

    // If 2 * (rest - unit) >= 10^kappa, then we can safely round up.
    if((rest > unit) && (ten_kappa - (rest - unit) <= (rest - unit))) {
        if(dbl.round_up())
            ++kappa;
        return true;
    }
    return false;
}

// Returns the biggest power of ten that is less than or equal to the given
// number. We furthermore receive the maximum number of bits 'number' has.
//
// Returns power == 10^(exponent_plus_one-1) such that
//    power <= number < power * 10.
// If number_bits == 0 then 0^(0-1) is returned.
// The number of bits must be <= 32.
// Precondition: number < (1 << (number_bits + 1)).

// Inspired by the method for finding an integer log base 10 from here:
// http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10
constexpr unsigned int const kSmallPowersOfTen[] = {
    0,      1,       10,       100,       1000,      10000,
    100000, 1000000, 10000000, 100000000, 1000000000};

unsigned greater_power_of_ten(uint32_t number, int number_bits) {
    assert(number < (1u << (number_bits + 1)));
    // 1233/4096 is approximately 1/lg(10).
    unsigned exponent_plus_one_guess = ((number_bits + 1) * 1233 >> 12);
    // We increment to skip over the first entry in the kPowersOf10 table.
    // Note: kPowersOf10[i] == 10^(i-1).
    exponent_plus_one_guess++;
    // We don't have any guarantees that 2^number_bits <= number.
    if(number < kSmallPowersOfTen[exponent_plus_one_guess])
        exponent_plus_one_guess--;
    return exponent_plus_one_guess;
}

void BiggestPowerTen(
    uint32_t number, int number_bits, uint32_t* power, int* exponent_plus_one) {
    assert(number < (1u << (number_bits + 1)));
    // 1233/4096 is approximately 1/lg(10).
    int exponent_plus_one_guess = ((number_bits + 1) * 1233 >> 12);
    // We increment to skip over the first entry in the kPowersOf10 table.
    // Note: kPowersOf10[i] == 10^(i-1).
    exponent_plus_one_guess++;
    // We don't have any guarantees that 2^number_bits <= number.
    if(number < kSmallPowersOfTen[exponent_plus_one_guess]) {
        exponent_plus_one_guess--;
    }
    *power = kSmallPowersOfTen[exponent_plus_one_guess];
    *exponent_plus_one = exponent_plus_one_guess;
}

// Generates the digits of input number w.
// w is a floating-point number (DiyFp), consisting of a significand and an
// exponent. Its exponent is bounded by kMinimalTargetExponent and
// kMaximalTargetExponent.
//       Hence -60 <= w.e() <= -32.
//
// Returns false if it fails, in which case the generated digits in the buffer
// should not be used.
// Preconditions:
//  * low, w and high are correct up to 1 ulp (unit in the last place). That
//    is, their error must be less than a unit of their last digits.
//  * low.e() == w.e() == high.e()
//  * low < w < high, and taking into account their error: low~ <= high~
//  * kMinimalTargetExponent <= w.e() <= kMaximalTargetExponent
// Postconditions: returns false if procedure fails.
//   otherwise:
//     * buffer is not null-terminated, but len contains the number of digits.
//     * buffer contains the shortest possible decimal digit-sequence
//       such that LOW < buffer * 10^kappa < HIGH, where LOW and HIGH are the
//       correct values of low and high (without their error).
//     * if more than one decimal representation gives the minimal number of
//       decimal digits then the one closest to W (where W is the correct value
//       of w) is chosen.
// Remark: this procedure takes into account the imprecision of its input
//   numbers. If the precision is not enough to guarantee all the postconditions
//   then false is returned. This usually happens rarely (~0.5%).
//
// Say, for the sake of example, that
//   w.e() == -48, and w.f() == 0x1234567890abcdef
// w's value can be computed by w.f() * 2^w.e()
// We can obtain w's integral digits by simply shifting w.f() by -w.e().
//  -> w's integral part is 0x1234
//  w's fractional part is therefore 0x567890abcdef.
// Printing w's integral part is easy (simply print 0x1234 in decimal).
// In order to print its fraction we repeatedly multiply the fraction by 10 and
// get each digit. Example the first digit after the point would be computed by
//   (0x567890abcdef * 10) >> 48. -> 3
// The whole thing becomes slightly more complicated because we want to stop
// once we have enough digits. That is, once the digits inside the buffer
// represent 'w' we can stop. Everything inside the interval low - high
// represents w. However we have to pay attention to low, high and w's
// imprecision.
bool DigitGen(
    double_format_context& dbl, int exponent, uint64_t significand,
    uint64_t low, uint64_t high) {
    assert(low + 1 <= high - 1);
    assert(
        exponent >= kMinimalTargetExponent
        && exponent <= kMaximalTargetExponent);
    // low, w and high are imprecise, but by less than one ulp (unit in the last
    // place).
    // If we remove (resp. add) 1 ulp from low (resp. high) we are certain that
    // the new numbers are outside of the interval we want the final
    // representation to lie in.
    // Inversely adding (resp. removing) 1 ulp from low (resp. high) would yield
    // numbers that are certain to lie in the interval. We will use this fact
    // later on.
    // We will now start by generating the digits within the uncertain
    // interval. Later we will weed out representations that lie outside the
    // safe interval and thus _might_ lie outside the correct interval.
    auto too_low = low - 1;
    auto too_high = high + 1;
    // too_low and too_high are guaranteed to lie outside the interval we want
    // the generated number in.
    auto unsafe_interval = too_high - too_low;
    // We now cut the input number into two parts: the integral digits and the
    // fractionals. We will not write any decimal separator though, but adapt
    // kappa instead.
    // Reminder: we are currently computing the digits (stored inside the
    // buffer) such that:   too_low < buffer * 10^kappa < too_high We use
    // too_high for the digit_generation and stop as soon as possible. If we
    // stop early we effectively round down.
    uint64_t one = static_cast<uint64_t>(1) << -exponent;
    // Division by one is a shift.
    uint32_t integrals = static_cast<uint32_t>(too_high >> -exponent);
    // Modulo by one is an and.
    uint64_t fractionals = too_high & (one - 1);
    auto pow10 =
        greater_power_of_ten(integrals, DiyFp::kSignificandSize - (-exponent));
    dbl.decimal_point = pow10;
    // Loop invariant: buffer = too_high / 10^kappa  (integer division)
    // The invariant holds for the first iteration: kappa has been initialized
    // with the divisor exponent + 1. And the dgreater_power_of_ten(ivisor is
    // the biggest power of ten that is smaller than integrals.
    while(dbl.decimal_point > 0) {
        uint32_t divisor = kSmallPowersOfTen[pow10];
        int digit = integrals / divisor;
        assert(digit <= 9);
        dbl.add_digit(static_cast<char>('0' + digit));
        integrals %= divisor;
        --dbl.decimal_point;
        // Note that kappa now equals the exponent of the divisor and that the
        // invariant thus holds again.
        uint64_t rest =
            (static_cast<uint64_t>(integrals) << -exponent) + fractionals;
        // Invariant: too_high = buffer * 10^kappa + DiyFp(rest, one.e())

        if(rest < unsafe_interval) {
            // Rounding down (by not emitting the remaining digits) yields a
            // number that lies within the unsafe interval.
            return RoundWeed(
                dbl, too_high - significand, unsafe_interval, rest,
                static_cast<uint64_t>(divisor) << -exponent, 1);
        }
        --pow10;
    }

    // The integrals have been generated. We are at the point of the decimal
    // separator. In the following loop we simply multiply the remaining digits
    // by 10 and divide by one. We just need to pay attention to multiply
    // associated data (like the interval or 'unit'), too. Note that the
    // multiplication by 10 does not overflow, because w.e >= -60 and thus one.e
    // >= -60.
    assert(exponent >= -60);
    assert(fractionals < one);
    assert(0xFFFFFFFFFFFFFFFFllu / 10 >= one);
    uint64_t unit = 1;
    for(;;) {
        fractionals *= 10;
        unit *= 10;
        unsafe_interval *= 10;
        // Integer division by one.
        int digit = static_cast<int>(fractionals >> -exponent);
        assert(digit <= 9);
        dbl.add_digit(static_cast<char>('0' + digit));
        fractionals &= one - 1; // Modulo by one.
        --dbl.decimal_point;
        if(fractionals < unsafe_interval) {
            return RoundWeed(
                dbl, (too_high - significand) * unit, unsafe_interval,
                fractionals, one, unit);
        }
    }
}

// Generates (at most) requested_digits digits of input number w.
// w is a floating-point number (DiyFp), consisting of a significand and an
// exponent. Its exponent is bounded by kMinimalTargetExponent and
// kMaximalTargetExponent.
//       Hence -60 <= w.e() <= -32.
//
// Returns false if it fails, in which case the generated digits in the buffer
// should not be used.
// Preconditions:
//  * w is correct up to 1 ulp (unit in the last place). That
//    is, its error must be strictly less than a unit of its last digit.
//  * kMinimalTargetExponent <= w.e() <= kMaximalTargetExponent
//
// Postconditions: returns false if procedure fails.
//   otherwise:
//     * buffer is not null-terminated, but length contains the number of
//       digits.
//     * the representation in buffer is the most precise representation of
//       requested_digits digits.
//     * buffer contains at most requested_digits digits of w. If there are less
//       than requested_digits digits then some trailing '0's have been removed.
//     * kappa is such that
//            w = buffer * 10^kappa + eps with |eps| < 10^kappa / 2.
//
// Remark: This procedure takes into account the imprecision of its input
//   numbers. If the precision is not enough to guarantee all the postconditions
//   then false is returned. This usually happens rarely, but the failure-rate
//   increases with higher requested_digits.
bool DigitGenCounted(double_format_context& dbl, DiyFp w, int& kappa) {
    assert(kMinimalTargetExponent <= w.e() && w.e() <= kMaximalTargetExponent);
    assert(kMinimalTargetExponent >= -60);
    assert(kMaximalTargetExponent <= -32);
    // w is assumed to have an error less than 1 unit. Whenever w is scaled we
    // also scale its error.
    uint64_t w_error = 1;
    // We cut the input number into two parts: the integral digits and the
    // fractional digits. We don't emit any decimal separator, but adapt kappa
    // instead. Example: instead of writing "1.2" we put "12" into the buffer
    // and increase kappa by 1.
    DiyFp one = DiyFp(static_cast<uint64_t>(1) << -w.e(), w.e());
    // Division by one is a shift.
    uint32_t integrals = static_cast<uint32_t>(w.f() >> -one.e());
    // Modulo by one is an and.
    uint64_t fractionals = w.f() & (one.f() - 1);
    uint32_t divisor;
    int divisor_exponent_plus_one;
    BiggestPowerTen(
        integrals, DiyFp::kSignificandSize - (-one.e()), &divisor,
        &divisor_exponent_plus_one);
    kappa = divisor_exponent_plus_one;

    // Loop invariant: buffer = w / 10^kappa  (integer division)
    // The invariant holds for the first iteration: kappa has been initialized
    // with the divisor exponent + 1. And the divisor is the biggest power of
    // ten that is smaller than 'integrals'.
    unsigned digits_last = dbl.requested_digits;
    while(kappa > 0) {
        int digit = integrals / divisor;
        assert(digit <= 9);
        dbl.add_digit(static_cast<char>('0' + digit));
        --digits_last;
        integrals %= divisor;
        --kappa;
        // Note that kappa now equals the exponent of the divisor and that the
        // invariant thus holds again.
        if(digits_last == 0)
            break;
        divisor /= 10;
    }

    if(digits_last == 0) {
        uint64_t rest =
            (static_cast<uint64_t>(integrals) << -one.e()) + fractionals;
        return RoundWeedCounted(
            dbl, rest, static_cast<uint64_t>(divisor) << -one.e(), w_error,
            kappa);
    }

    // The integrals have been generated. We are at the point of the decimal
    // separator. In the following loop we simply multiply the remaining digits
    // by 10 and divide by one. We just need to pay attention to multiply
    // associated data (the 'unit'), too. Note that the multiplication by 10
    // does not overflow, because w.e >= -60 and thus one.e >= -60.
    assert(one.e() >= -60);
    assert(fractionals < one.f());
    assert(0xFFFFFFFFFFFFFFFFllu / 10 >= one.f());
    while(digits_last != 0 && fractionals > w_error) {
        fractionals *= 10;
        w_error *= 10;
        // Integer division by one.
        int digit = static_cast<int>(fractionals >> -one.e());
        assert(digit <= 9);
        dbl.add_digit(static_cast<char>('0' + digit));
        --digits_last;
        fractionals &= one.f() - 1; // Modulo by one.
        --kappa;
    }
    if(digits_last != 0)
        return false;
    return RoundWeedCounted(dbl, fractionals, one.f(), w_error, kappa);
}

} // namespace

// Provides a decimal representation of v.
// Returns true if it succeeds, otherwise the result cannot be trusted.
// There will be *length digits inside the buffer (not null-terminated).
// If the function returns true then
//        v == (double) (buffer * 10^decimal_exponent).
// The digits in the buffer are the shortest representation possible: no
// 0.09999999999999999 instead of 0.1. The shorter representation will even be
// chosen even if the longer one would be closer to v.
// The last digit will be closest to the actual v. That is, even if several
// digits might correctly yield 'v' when read again, the closest will be
// computed.
bool grisu3_dtoa(double_format_context& dbl) {
    DiyFp w = DiyFp::MakeNormalized(dbl);
    // boundary_minus and boundary_plus are the boundaries between v and its
    // closest floating-point neighbors. Any number strictly between
    // boundary_minus and boundary_plus will round to v when convert to a
    // double. Grisu3 will never output representations that lie exactly on a
    // boundary.
    DiyFp boundary_minus, boundary_plus;
    DiyFp::NormalizedBoundaries(dbl.value, &boundary_minus, &boundary_plus);

    assert(boundary_plus.e() == w.e());
    DiyFp ten_mk; // Cached power of ten: 10^-k
    int mk;       // -k
    int ten_mk_minimal_binary_exponent =
        kMinimalTargetExponent - (w.e() + DiyFp::kSignificandSize);
    int ten_mk_maximal_binary_exponent =
        kMaximalTargetExponent - (w.e() + DiyFp::kSignificandSize);
    PowersOfTenCache::GetCachedPowerForBinaryExponentRange(
        ten_mk_minimal_binary_exponent, ten_mk_maximal_binary_exponent, &ten_mk,
        &mk);
    assert(
        (kMinimalTargetExponent <= w.e() + ten_mk.e() + DiyFp::kSignificandSize)
        && (kMaximalTargetExponent
            >= w.e() + ten_mk.e() + DiyFp::kSignificandSize));
    // Note that ten_mk is only an approximation of 10^-k. A DiyFp only contains
    // a 64 bit significand and ten_mk is thus only precise up to 64 bits.

    // The DiyFp::Times procedure rounds its result, and ten_mk is approximated
    // too. The variable scaled_w (as well as scaled_boundary_minus/plus) are
    // now off by a small amount. In fact: scaled_w - w*10^k < 1ulp (unit in the
    // last place) of scaled_w. In other words: let f = scaled_w.f() and e =
    // scaled_w.e(), then
    //           (f-1) * 2^e < w*10^k < (f+1) * 2^e
    DiyFp scaled_w = DiyFp::Times(w, ten_mk);
    assert(
        scaled_w.e()
        == boundary_plus.e() + ten_mk.e() + DiyFp::kSignificandSize);
    // In theory it would be possible to avoid some recomputations by computing
    // the difference between w and boundary_minus/plus (a power of 2) and to
    // compute scaled_boundary_minus/plus by subtracting/adding from
    // scaled_w. However the code becomes much less readable and the speed
    // enhancements are not terriffic.
    DiyFp scaled_boundary_minus = DiyFp::Times(boundary_minus, ten_mk);
    DiyFp scaled_boundary_plus = DiyFp::Times(boundary_plus, ten_mk);

    // DigitGen will generate the digits of scaled_w. Therefore we have
    // v == (double) (scaled_w * 10^-mk).
    // Set decimal_exponent == -mk and pass it to DigitGen. If scaled_w is not
    // an integer than it will be updated. For instance if scaled_w == 1.23 then
    // the buffer will be filled with "123" und the decimal_exponent will be
    // decreased by 2.
    if(!DigitGen(
           dbl, scaled_w.e(), scaled_w.f(), scaled_boundary_minus.f(),
           scaled_boundary_plus.f())) {
        dbl.digit_count = 0;
        return false;
    }
    int decimal_exponent = -mk + dbl.decimal_point;
    dbl.decimal_point = static_cast<int>(dbl.digit_count) + decimal_exponent;
    return true;
}

// The "counted" version of grisu3 (see above) only generates requested_digits
// number of digits. This version does not generate the shortest representation,
// and with enough requested digits 0.1 will at some point print as 0.9999999...
// Grisu3 is too imprecise for real halfway cases (1.5 will not work) and
// therefore the rounding strategy for halfway cases is irrelevant.
bool grisu3_fixed_dtoa(double_format_context& dbl) {
    DiyFp w = DiyFp::MakeNormalized(dbl);
    DiyFp ten_mk; // Cached power of ten: 10^-k
    int mk;       // -k
    int ten_mk_minimal_binary_exponent =
        kMinimalTargetExponent - (w.e() + DiyFp::kSignificandSize);
    int ten_mk_maximal_binary_exponent =
        kMaximalTargetExponent - (w.e() + DiyFp::kSignificandSize);
    PowersOfTenCache::GetCachedPowerForBinaryExponentRange(
        ten_mk_minimal_binary_exponent, ten_mk_maximal_binary_exponent, &ten_mk,
        &mk);
    assert(
        (kMinimalTargetExponent <= w.e() + ten_mk.e() + DiyFp::kSignificandSize)
        && (kMaximalTargetExponent
            >= w.e() + ten_mk.e() + DiyFp::kSignificandSize));
    // Note that ten_mk is only an approximation of 10^-k. A DiyFp only contains
    // a 64 bit significand and ten_mk is thus only precise up to 64 bits.

    // The DiyFp::Times procedure rounds its result, and ten_mk is approximated
    // too. The variable scaled_w (as well as scaled_boundary_minus/plus) are
    // now off by a small amount. In fact: scaled_w - w*10^k < 1ulp (unit in the
    // last place) of scaled_w. In other words: let f = scaled_w.f() and e =
    // scaled_w.e(), then
    //           (f-1) * 2^e < w*10^k < (f+1) * 2^e
    DiyFp scaled_w = DiyFp::Times(w, ten_mk);

    // We now have (double) (scaled_w * 10^-mk).
    // DigitGen will generate the first requested_digits digits of scaled_w and
    // return together with a kappa such that scaled_w ~= buffer * 10^kappa. (It
    // will not always be exactly the same since DigitGenCounted only produces a
    // limited number of digits.)
    int kappa;
    if(!DigitGenCounted(dbl, scaled_w, kappa)) {
        dbl.digit_count = 0;
        return false;
    }
    int decimal_exponent = -mk + kappa;
    dbl.decimal_point = static_cast<int>(dbl.digit_count) + decimal_exponent;
    return true;
}

} // namespace detail
} // namespace fmt
} // namespace univang
