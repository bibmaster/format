#pragma once
namespace univang {
namespace fmt {
namespace detail {

using out_byte_t = format_context::byte;

static constexpr char base_100_digits[201] =
    "0001020304050607080910111213141516171819"
    "2021222324252627282930313233343536373839"
    "4041424344454647484950515253545556575859"
    "6061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

template<class Char, class T>
unsigned write_dec(Char* out, unsigned len, T i) noexcept {
    unsigned pos = len - 1;
    while(i >= 100) {
        auto const num = (i % 100) * 2;
        i /= 100;
        out[pos] = Char(base_100_digits[num + 1]);
        out[pos - 1] = Char(base_100_digits[num]);
        pos -= 2;
    }
    if(i >= 10) {
        auto const num = i * 2;
        out[pos] = Char(base_100_digits[num + 1]);
        --pos;
        out[pos] = Char(base_100_digits[num]);
    }
    else
        out[pos] = Char('0' + i);
    return pos;
}

template<class Char, class T>
unsigned write_dec_with_sep(Char* out, unsigned len, T i, char sep) noexcept {
    unsigned pos = len;
    size_t n = 0;
    auto write_sep = [&n, &pos, out, sep] {
        ++n;
        if(n == 3) {
            out[--pos] = Char(sep);
            n = 0;
        }
    };
    while(i >= 100) {
        auto const num = (i % 100) * 2;
        i /= 100;
        out[--pos] = Char(base_100_digits[num + 1]);
        write_sep();
        out[--pos] = Char(base_100_digits[num]);
        write_sep();
    }
    if(i >= 10) {
        auto const num = i * 2;
        out[--pos] = Char(base_100_digits[num + 1]);
        write_sep();
        out[--pos] = Char(base_100_digits[num]);
    }
    else {
        write_sep();
        out[--pos] = Char('0' + i);
    }
    return pos;
}

template<class T>
unsigned write_int(
    out_byte_t* out, unsigned len, T i, unsigned base,
    bool uppercase = false) noexcept {
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned pos = len - 1;
    while(i >= base) {
        auto const num = i % base;
        i /= base;
        out[pos--] = out_byte_t(digits[num]);
    }
    out[pos] = out_byte_t(digits[i]);
    return pos;
}

template<class T>
std::enable_if_t<std::is_unsigned_v<T>> append_dec(format_context& out, T i) {
    out_byte_t tmp[sizeof(T) * 3];
    auto pos = write_dec(tmp, sizeof(tmp), i);
    out.write(tmp + pos, sizeof(tmp) - pos);
}

template<class T>
std::enable_if_t<std::is_signed_v<T>> append_dec(format_context& out, T i) {
    format_context::byte tmp[sizeof(T) * 3 + 1];
    const bool neg = i < 0;
    using U = std::make_unsigned_t<T>;
    const U u = neg ? (U)~i + 1u : i;
    auto pos = write_dec(tmp, sizeof(tmp), u);
    if(neg)
        tmp[--pos] = out_byte_t('-');
    out.write(tmp + pos, sizeof(tmp) - pos);
}

template<class T>
bool format_num(
    format_context& out, const format_spec& spec, T arg,
    bool negative = false) {
    char sign = negative ? '-' : (spec.sign == '-') ? 0 : spec.sign;
    out_byte_t tmp[sizeof(T) * 8];
    size_t pos;
    auto alt = spec.alt;
    switch(spec.type) {
    case 0:
    case 'd':
        alt = false;
        pos = write_dec(tmp, sizeof(tmp), arg);
        break;
    case 'n':
        alt = false;
        pos = write_dec_with_sep(tmp, sizeof(tmp), arg, ',');
        break;
    case 'b':
    case 'B':
        pos = write_int(tmp, sizeof(tmp), arg, 2);
        break;
    case 'o':
        pos = write_int(tmp, sizeof(tmp), arg, 8);
        break;
    case 'x':
    case 'X':
        pos = write_int(tmp, sizeof(tmp), arg, 16, spec.type == 'X');
        break;
    default:
        return false;
    }
    size_t size = sizeof(tmp) - pos;
    std::string_view num_str{(char*)tmp + pos, size};
    size += (sign != 0);
    if(alt)
        size += spec.type == 'o' ? 1 : 2;
    auto padding = (spec.width <= size) ? 0u : spec.width - unsigned(size);
    out.ensure(size + padding);
    auto fill = spec.fill ? spec.fill : ' ';
    if(spec.align == '>' || !spec.align)
        out.add_padding(fill, padding);
    else if(spec.align == '^')
        out.add_padding(fill, padding / 2);
    if(sign)
        out.add(sign);
    if(alt) {
        out.add('0');
        if(spec.type != 'o')
            out.add(spec.type);
    }
    if(spec.align == '=')
        out.add_padding(fill, padding);
    out.add(num_str);
    if(spec.align == '<')
        out.add_padding(fill, padding);
    else if(spec.align == '^')
        out.add_padding(fill, padding - padding / 2);
    return true;
}

template<class T>
std::enable_if_t<std::is_unsigned_v<T>, bool> do_format_int(
    format_context& out, const format_spec& spec, T arg) {
    return format_num(out, spec, arg, false);
}

template<class T>
std::enable_if_t<std::is_signed_v<T>, bool> do_format_int(
    format_context& out, const format_spec& spec, T arg) {
    const bool negative = arg < 0;
    using U = std::make_unsigned_t<T>;
    const U u = negative ? (U)~arg + 1u : arg;
    return format_num(out, spec, u, negative);
}

} // namespace detail
} // namespace fmt
} // namespace univang
