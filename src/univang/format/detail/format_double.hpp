#pragma once
#include <cmath>

#include "format_utils.hpp"

namespace univang {
namespace fmt {
namespace detail {

constexpr bool validate_float_spec(const format_spec& spec) {
    switch(spec.type) {
    case 0:
    case 'E':
    case 'e':
    case 'F':
    case 'f':
    case 'G':
    case 'g':
    case '%':
        return true;
    default:
        return false;
    }
}

namespace Double {
constexpr const uint64_t kSignificandMask = 0x000fffffffffffffull;
constexpr const uint64_t kExponentMask = 0x7ff0000000000000ull;
constexpr const uint64_t kHiddenBit = 0x0010000000000000ull;
constexpr const int kSignificandSize = 53;
// Excludes the hidden bit.
constexpr const int kPhysicalSignificandSize = 52;
constexpr const int kExponentBias = 0x3FF + kPhysicalSignificandSize;
constexpr const int kDenormalExponent = -kExponentBias + 1;
} // namespace Double

struct double_format_context {
    enum class byte : char {}; // To avoid misaliasing.

    constexpr const static size_t max_decimal_digits = 128;

    double_format_context(double value) : value(value) {
        auto u = bit_cast<uint64_t>(value);
        uint64_t significand = u & Double::kSignificandMask;
        uint64_t exponent_bits = u & Double::kExponentMask;
        int exponent;
        if(exponent_bits != 0) {
            significand += Double::kHiddenBit;
            exponent = int(exponent_bits >> Double::kPhysicalSignificandSize)
                - Double::kExponentBias;
        }
        // Denormal
        else {
            exponent = Double::kDenormalExponent;
        }
        this->significand = significand;
        this->exponent = exponent;
    }

    // Formatted value.
    double value;
    uint64_t significand;
    int exponent;
    bool uppercase = false;
    bool has_requested_digits = false;
    bool format_as_exponent = false;
    unsigned digits_after_point = 0;
    int requested_digits = 0;
    int decimal_point = 0;
    unsigned digit_count = 0;

    // Decimal digits representation.
    byte digits[max_decimal_digits];

    bool lower_boundary_is_closer() const {
        return significand == Double::kHiddenBit;
    }
    char digit(unsigned pos) const noexcept {
        assert(pos < digit_count);
        return char(digits[pos]);
    }
    char last_digit() const {
        return digit(digit_count - 1);
    }
    char first_digit() const {
        return digit(0);
    }
    bool check_digit_overflow(unsigned pos) {
        assert(pos < digit_count);
        return digits[pos] == byte('0' + 10);
    }
    void set_digit(unsigned pos, char c) {
        assert(pos < digit_count);
        digits[pos] = byte(c);
    }
    void add_digit(char digit) {
        assert(digit_count < max_decimal_digits);
        digits[digit_count++] = byte(digit);
    }
    void add_num_digit(uint8_t num) {
        assert(digit_count < max_decimal_digits);
        digits[digit_count++] = byte(num + '0');
    }
    void add_num_digit(uint16_t num) {
        assert(digit_count < max_decimal_digits);
        digits[digit_count++] = byte(num + '0');
    }
    void round_down_digit(unsigned pos) {
        assert(pos < digit_count);
        auto u = uint8_t(digits[pos]);
        digits[pos] = byte(uint8_t(digits[pos]) - 1);
    }
    void round_down_last_digit() {
        assert(digit_count != 0);
        round_down_digit(digit_count - 1);
    }
    void round_up_digit(unsigned pos) {
        assert(pos < digit_count);
        auto u = uint8_t(digits[pos]);
        digits[pos] = byte(uint8_t(digits[pos]) + 1);
    }
    void round_up_last_digit() {
        assert(digit_count != 0);
        round_up_digit(digit_count - 1);
    }
    // Returns true in case of 99999 -> 1000.
    bool round_up() {
        // Round the last digit until we either have a digit that was not '9' or
        // until we reached the first digit.
        round_up_last_digit();
        for(auto i = digit_count - 1; i != 0; --i) {
            if(!check_digit_overflow(i))
                return false;
            set_digit(i, '0');
            round_up_digit(i - 1);
        }
        // If the first digit is now '0' + 10, we would need to set it to '0'
        // and add a '1' in front. However we reach the first digit only if all
        // following digits had been '9' before rounding up. Now all trailing
        // digits are '0' and we simply switch the first digit to '1' and update
        // the decimal-point (indicating that the point is now one digit to the
        // right).
        if(!check_digit_overflow(0))
            return false;
        set_digit(0, '1');
        return true;
    }
};

enum class dtoa_mode { SHORTEST, FIXED, PRECISION };

bool grisu3_dtoa(double_format_context& dbl);
bool grisu3_fixed_dtoa(double_format_context& dbl);
bool fast_fixed_dtoa(double_format_context& out);
void bignum_dtoa(double_format_context& dbl, dtoa_mode mode);

// Floating point format.
void do_format_double(format_context& out, format_spec& spec, double value);

} // namespace detail
} // namespace fmt
} // namespace univang
