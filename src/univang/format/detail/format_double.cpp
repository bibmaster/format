#include "format_double.hpp"

#include <algorithm>

#include <univang/format/buffer.hpp>

namespace univang {
namespace fmt {
namespace detail {
namespace {

struct double_format_options {
    bool write_exponent_plus;
    // 0 => "42", 1 = "42.", 2 = "42.0"
    unsigned zero_decimal_fraction;
    unsigned min_exponent_width;
    // shortest - as decimal if exponent in range
    int decimal_exponent_min;
    int decimal_exponent_max;
    int max_precision_leading_zeros;
    int max_precision_trailing_zeros;
};

constexpr double_format_options format_options{
    true, // write_exponent_plus
    0,    // zero_decimal_fraction
    0,    // min_exponent_width
    -6,   // decimal_exponent_min
    21,   // decimal_exponent_max
    6,    // max_precision_leading_zeros
    0     // max_precision_trailing_zeros
};

unsigned exponent_format_size(const double_format_context& dbl) {
    unsigned result = dbl.digit_count + 1;
    if(dbl.digit_count > 1)
        ++result;
    int exponent = dbl.decimal_point - 1;
    if(exponent < 0) {
        ++result;
        exponent = -exponent;
    }
    else if(format_options.write_exponent_plus)
        ++result;
    auto exponent_len = 1;
    while(exponent > 0) {
        ++result;
        exponent /= 10;
    }
    return result;
}

void format_exponent(format_context& out, const double_format_context& dbl) {
    int exponent = dbl.decimal_point - 1;
    assert(dbl.digit_count != 0);
    out.add(format_context::byte(dbl.digits[0]));
    if(dbl.digit_count != 1) {
        out.add('.');
        out.add(dbl.digits + 1, dbl.digit_count - 1);
    }
    out.add(dbl.uppercase ? 'E' : 'e');
    if(exponent < 0) {
        out.add('-');
        exponent = -exponent;
    }
    else if(format_options.write_exponent_plus)
        out.add('+');
    if(exponent == 0) {
        out.add('0');
        return;
    }
    assert(exponent < 1e4);
    constexpr unsigned max_exp_length = 5;
    char buffer[max_exp_length];
    unsigned pos = max_exp_length;
    while(exponent > 0) {
        buffer[--pos] = '0' + (exponent % 10);
        exponent /= 10;
    }
    out.add(&buffer[pos], max_exp_length - pos);
}

unsigned decimal_format_size(const double_format_context& dbl) {
    unsigned result;
    // Create a representation that is padded with zeros if needed.
    if(dbl.decimal_point <= 0)
        result = dbl.digits_after_point == 0 ? 1 : 2 + dbl.digits_after_point;
    else if(dbl.decimal_point >= int(dbl.digit_count)) {
        result = dbl.decimal_point;
        if(dbl.digits_after_point != 0)
            result += 1 + dbl.digits_after_point;
    }
    else {
        result = dbl.digit_count + 1 + dbl.digits_after_point
            - (dbl.digit_count - dbl.decimal_point);
    }
    if(dbl.digits_after_point == 0)
        result += format_options.zero_decimal_fraction;
    return result;
}

void format_decimal(format_context& out, const double_format_context& dbl) {
    // Create a representation that is padded with zeros if needed.
    if(dbl.decimal_point <= 0) {
        // "0.00000decimal_rep" or "0.000decimal_rep00".
        out.add('0');
        if(dbl.digits_after_point != 0) {
            out.add('.');
            out.add_padding('0', -dbl.decimal_point);
            assert(
                dbl.digit_count
                <= dbl.digits_after_point - (-dbl.decimal_point));
            out.add(dbl.digits, dbl.digit_count);
            auto remaining_digits = dbl.digits_after_point
                - unsigned(-dbl.decimal_point) - dbl.digit_count;
            out.add_padding('0', remaining_digits);
        }
    }
    else if(dbl.decimal_point >= int(dbl.digit_count)) {
        // "decimal_rep0000.00000" or "decimal_rep.0000".
        out.add(dbl.digits, dbl.digit_count);
        out.add_padding('0', unsigned(dbl.decimal_point) - dbl.digit_count);
        if(dbl.digits_after_point > 0) {
            out.add('.');
            out.add_padding('0', dbl.digits_after_point);
        }
    }
    else {
        // "decima.l_rep000".
        assert(dbl.digits_after_point > 0);
        out.add(dbl.digits, dbl.decimal_point);
        out.add('.');
        assert(dbl.digit_count - dbl.decimal_point <= dbl.digits_after_point);
        out.add(
            dbl.digits + dbl.decimal_point,
            dbl.digit_count - dbl.decimal_point);
        auto remaining_digits =
            dbl.digits_after_point - (dbl.digit_count - dbl.decimal_point);
        out.add_padding('0', remaining_digits);
    }
    if(dbl.digits_after_point == 0
       && format_options.zero_decimal_fraction != 0) {
        out.add('.');
        if(format_options.zero_decimal_fraction > 1)
            out.add('0');
    }
}

inline unsigned format_size(const double_format_context& dbl) {
    return dbl.format_as_exponent ? exponent_format_size(dbl)
                                  : decimal_format_size(dbl);
}

inline void format(format_context& out, const double_format_context& dbl) {
    if(dbl.format_as_exponent)
        format_exponent(out, dbl);
    else
        format_decimal(out, dbl);
}

void generate_decimal_digits(double_format_context& dbl, dtoa_mode mode) {
    if(mode == dtoa_mode::PRECISION && dbl.requested_digits == 0)
        return;

    if(dbl.value == 0) {
        dbl.add_digit('0');
        dbl.decimal_point = 1;
        return;
    }

    bool fast_worked = false;
    switch(mode) {
    case dtoa_mode::SHORTEST:
        fast_worked = grisu3_dtoa(dbl);
        break;
    case dtoa_mode::FIXED:
        fast_worked = fast_fixed_dtoa(dbl);
        break;
    case dtoa_mode::PRECISION:
        fast_worked = grisu3_fixed_dtoa(dbl);
        break;
    }
    if(fast_worked)
        return;
    dbl.digit_count = 0;
    dbl.decimal_point = 0;

    // If the fast dtoa didn't succeed use the slower bignum version.
    bignum_dtoa(dbl, mode);
}

void generate_shortest(double_format_context& dbl) {
    generate_decimal_digits(dbl, dtoa_mode::SHORTEST);
    int exponent = dbl.decimal_point - 1;
    if(format_options.decimal_exponent_min <= exponent
       && exponent <= format_options.decimal_exponent_max) {
        dbl.digits_after_point =
            (std::max)(0, int(dbl.digit_count) - dbl.decimal_point);
    }
    else {
        dbl.format_as_exponent = true;
    }
}

bool generate_fixed(double_format_context& dbl) {
    const double max_fixed_value = 1e60;
    if(dbl.value >= max_fixed_value || dbl.value <= -max_fixed_value)
        return false;
    dbl.requested_digits = std::clamp(dbl.requested_digits, 0, 60);
    generate_decimal_digits(dbl, dtoa_mode::FIXED);
    dbl.digits_after_point = dbl.requested_digits;
    return true;
}

void generate_exponent(double_format_context& dbl) {
    dbl.requested_digits = std::clamp(dbl.requested_digits, 0, 120);
    if(!dbl.has_requested_digits)
        generate_decimal_digits(dbl, dtoa_mode::SHORTEST);
    else {
        ++dbl.requested_digits;
        generate_decimal_digits(dbl, dtoa_mode::PRECISION);
        while(dbl.digit_count < unsigned(dbl.requested_digits))
            dbl.add_digit('0');
    }
    dbl.format_as_exponent = true;
}

void generate_precision(double_format_context& dbl) {
    dbl.requested_digits = std::clamp(dbl.requested_digits, 1, 120);

    generate_decimal_digits(dbl, dtoa_mode::PRECISION);
    assert(dbl.digit_count <= unsigned(dbl.requested_digits));

    int exponent = dbl.decimal_point - 1;
    int extra_zero = format_options.zero_decimal_fraction > 1 ? 1 : 0;
    if((-dbl.decimal_point + 1 > format_options.max_precision_leading_zeros)
       || (dbl.decimal_point - dbl.requested_digits + extra_zero
           > format_options.max_precision_trailing_zeros)) {
        while(dbl.digit_count < unsigned(dbl.requested_digits))
            dbl.add_digit('0');
        dbl.format_as_exponent = true;
    }
    else {
        dbl.digits_after_point =
            (std::max)(0, dbl.requested_digits - dbl.decimal_point);
    }
}

void format_nan_inf(format_context& out, const format_spec& spec, bool inf) {
    char buf[5];
    size_t width = 0;
    if(spec.sign && inf)
        buf[width++] = spec.sign;
    bool upper = spec.type != 0 && spec.type < 'a';
    const char* str = inf ? (upper ? "INF" : "inf") : (upper ? "NAN" : "nan");
    memcpy(buf + width, str, 3);
    width += 3;
    write_padded(out, spec, padded_string({buf, width}));
}

} // namespace

// Floating point format.
void do_format_double(format_context& out, format_spec& spec, double value) {
    bool negative = std::signbit(value);
    if(negative)
        value = -value;
    spec.sign = negative ? '-' : (spec.sign == '-') ? 0 : spec.sign;

    if(!std::isfinite(value))
        return format_nan_inf(out, spec, std::isinf(value));

    if(spec.type == '%')
        value *= 100;

    double_format_context dbl{value};
    dbl.uppercase = spec.type != 0 && spec.type < 'a';
    dbl.has_requested_digits = spec.has_precision;
    dbl.requested_digits = spec.has_precision ? spec.precision : 6;

    switch(spec.type) {
    case 'E':
    case 'e':
        generate_exponent(dbl);
        break;
    case 'F':
    case 'f':
        if(!generate_fixed(dbl))
            generate_precision(dbl);
        break;
    case 0:
    case 'G':
    case 'g':
    case '%':
        if(spec.has_precision)
            generate_precision(dbl);
        else
            generate_shortest(dbl);
        break;
    }

    auto size = format_size(dbl);
    if(spec.sign)
        ++size;
    if(spec.type == '%')
        ++size;
    unsigned left_padding = 0, right_padding = 0;
    auto fill = spec.fill ? spec.fill : ' ';
    if(spec.width > size) {
        size = spec.width;
        auto padding = spec.width - unsigned(size);
        left_padding =
            spec.align == '<' ? 0 : spec.align == '^' ? padding / 2 : padding;
        auto right_padding = spec.align == '<'
            ? padding
            : spec.align == '^' ? padding - left_padding : 0;
    }
    out.ensure(size);
    if(left_padding != 0)
        out.add_padding(fill, left_padding);
    if(spec.sign)
        out.add(spec.sign);
    format(out, dbl);
    if(spec.type == '%')
        out.add('%');
    if(right_padding != 0)
        out.add_padding(fill, right_padding);
}

} // namespace detail
} // namespace fmt
} // namespace univang
