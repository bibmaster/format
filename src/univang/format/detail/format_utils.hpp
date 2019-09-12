#pragma once
#include "univang/format/format.hpp"

namespace univang {
namespace fmt {
namespace detail {

template<typename Dest, typename Source>
inline Dest bit_cast(const Source& source) {
    static_assert(sizeof(Dest) == sizeof(Source), "size mismatch");
    Dest dest;
    std::memcpy(&dest, &source, sizeof(dest));
    return dest;
}

template<class Padded>
void write_padded(
    format_context& out, const format_spec& spec, Padded&& value) {
    auto size = value.size();
    if(spec.width <= size)
        return value.write(out);
    out.ensure(spec.width);
    auto padding = spec.width - unsigned(size);
    auto left_padding =
        spec.align == '<' ? 0 : spec.align == '^' ? padding / 2 : padding;
    auto right_padding =
        spec.align == '<' ? padding : spec.align == '^' ? padding - left_padding : 0;
    auto fill = spec.fill ? spec.fill : ' ';
    if(left_padding != 0)
        out.add_padding(fill, left_padding);
    value.write(out);
    if(right_padding != 0)
        out.add_padding(fill, right_padding);
}

struct padded_string {
    std::string_view str;
    padded_string(std::string_view str) : str(str) {
    }
    size_t size() const noexcept {
        return str.size();
    }
    void write(format_context& out) {
        out.write(str.data(), str.size());
    }
};

struct padded_char {
    char c;
    padded_char(char c) : c(c) {
    }
    size_t size() const noexcept {
        return 1;
    }
    void write(format_context& out) {
        out.write(c);
    }
};

} // namespace detail
} // namespace fmt
} // namespace univang
