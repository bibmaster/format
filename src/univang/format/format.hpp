#pragma once
#include <array>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <string_view>
#include <type_traits>
#include <variant>

#include "format_context.hpp"
#include "parse_context.hpp"

namespace univang {
namespace fmt {

// format_spec ::=
//  [[fill]align][sign]["#"]["0"][width]["." precision][type]
struct format_spec {
    unsigned width = 0;
    unsigned precision = 0;
    bool has_precision = false;
    char fill = 0;
    char align = 0;
    char sign = 0;
    char alt = 0;
    char type = 0;
};

template<class T, class Enable = void>
struct formatter {
    formatter() = delete;
};

inline void append(format_context& out, char v) {
    out.write(v);
}
void append(format_context& out, int v);
void append(format_context& out, unsigned int v);
void append(format_context& out, long v);
void append(format_context& out, unsigned long v);
void append(format_context& out, long long v);
void append(format_context& out, unsigned long long v);
void append(format_context& out, double v);
void append(format_context& out, const void* v);
inline void append(format_context& out, std::string_view v) {
    out.write(v);
}
inline void append(format_context& out, const char* v) {
    append(out, std::string_view(v));
}
inline void append(format_context& out, bool v) {
    append(out, v ? "true" : "false");
}

template<class T>
using has_formatter = std::is_constructible<formatter<T>>;

template<class, class = std::void_t<>>
struct has_parsing_formatter : std::false_type {};
template<class T>
struct has_parsing_formatter<
    T,
    std::void_t<decltype(std::declval<formatter<T>>().format(
        std::declval<parse_context&>(), std::declval<format_context&>(),
        std::declval<T>()))>> : std::true_type {};

template<class, class = std::void_t<>>
struct has_default_formatter : std::false_type {};
template<class T>
struct has_default_formatter<
    T,
    std::void_t<decltype(std::declval<formatter<T>>().format(
        std::declval<format_context&>(), std::declval<T>()))>>
    : std::true_type {};

template<class, class = std::void_t<>>
struct has_format_member : std::false_type {};
template<class T>
struct has_format_member<
    T,
    std::void_t<decltype(std::declval<T>().format(
        std::declval<format_context&>()))>> : std::true_type {};
template<class, class = std::void_t<>>
struct has_parsing_format_member : std::false_type {};
template<class T>
struct has_parsing_format_member<
    T,
    std::void_t<decltype(std::declval<T>().format(
        std::declval<parse_context&>(), std::declval<format_context&>()))>>
    : std::true_type {};

template<class, class = std::void_t<>>
struct has_adl_format : std::false_type {};
template<class T>
struct has_adl_format<
    T,
    std::void_t<decltype(
        format(std::declval<format_context&>(), std::declval<T>()))>>
    : std::true_type {};

template<class, class = std::void_t<>>
struct has_parsing_adl_format : std::false_type {};
template<class T>
struct has_parsing_adl_format<
    T,
    std::void_t<decltype(format(
        std::declval<parse_context&>(), std::declval<format_context&>(),
        std::declval<T>()))>> : std::true_type {};

template<class T, class R = void>
using enable_if_formattable = std::enable_if_t<
    has_formatter<T>::value || has_format_member<T>::value
        || has_parsing_format_member<T>::value || has_adl_format<T>::value
        || has_parsing_adl_format<T>::value,
    R>;

template<class T>
auto append(format_context& out, const T& v) -> enable_if_formattable<T> {
    if constexpr(has_default_formatter<T>::value) {
        formatter<T> formatter;
        formatter.format(out, v);
    }
    else if constexpr(has_parsing_formatter<T>::value) {
        formatter<T> formatter;
        parse_context no_fmt;
        formatter.format(no_fmt, out, v);
    }
    else if constexpr(has_format_member<T>::value) {
        v.format(out);
    }
    else if constexpr(has_parsing_format_member<T>::value) {
        parse_context no_fmt;
        v.format(no_fmt, out);
    }
    else if constexpr(has_parsing_adl_format<T>::value) {
        using namespace fmt;
        parse_context no_fmt;
        format(no_fmt, out, v);
    }
    else if constexpr(has_adl_format<T>::value) {
        using namespace fmt;
        format(out, v);
    }
}

struct format_arg {
    struct handle {
        template<class T>
        static void do_format(
            format_context& out, parse_context& fmt, const void* p) {
            const auto& v = *static_cast<const T*>(p);
            if constexpr(has_parsing_formatter<T>::value) {
                formatter<T> formatter;
                formatter.format(fmt, out, v);
            }
            else if constexpr(has_formatter<T>::value) {
                formatter<T> formatter;
                formatter.format(out, v);
            }
            else if constexpr(has_parsing_format_member<T>::value) {
                v.format(fmt, out);
            }
            else if constexpr(has_format_member<T>::value) {
                v.format(out);
            }
            else if constexpr(has_parsing_adl_format<T>::value) {
                using namespace fmt;
                format(fmt, out, v);
            }
            else if constexpr(has_adl_format<T>::value) {
                using namespace fmt;
                format(out, v);
            }
        }
        const void* ptr;
        void (*format_fn)(format_context&, parse_context&, const void*);

        template<class T>
        explicit handle(const T& val) noexcept
            : ptr(&val), format_fn(&do_format<T>) {
        }
    };
    struct map {
        template<typename T>
        std::enable_if_t<
            std::is_integral_v<
                T> && std::is_signed_v<T> && sizeof(T) <= sizeof(int),
            int>
        operator()(const T& v) const {
            return v;
        }
        template<typename T>
        std::enable_if_t<
            std::is_integral_v<
                T> && std::is_signed_v<T> && sizeof(int) < sizeof(T)
                && sizeof(long long) <= sizeof(T),
            long long>
        operator()(const T& v) const {
            return v;
        }
        template<typename T>
        std::enable_if_t<
            std::is_integral_v<
                T> && std::is_unsigned_v<T> && sizeof(T) <= sizeof(unsigned),
            unsigned>
        operator()(const T& v) const {
            return v;
        }
        template<typename T>
        std::enable_if_t<
            std::is_integral_v<
                T> && std::is_unsigned_v<T> && sizeof(unsigned) < sizeof(T)
                && sizeof(unsigned long long) <= sizeof(T),
            unsigned long long>
        operator()(const T& v) const {
            return v;
        }
        template<typename T>
        enable_if_formattable<T, handle> operator()(const T& v) const {
            return handle(v);
        }
    };
    using value_type = std::variant<
        bool, char, int, unsigned, long long, unsigned long long, double,
        const char*, std::string_view, const void*, handle>;
    value_type value;

    template<typename T>
    explicit format_arg(const T& v) noexcept : value(map()(v)) {
    }
    explicit format_arg(bool b) noexcept : value(b) {
    }
    explicit format_arg(char b) noexcept : value(b) {
    }
    explicit format_arg(float n) noexcept : value(n) {
    }
    explicit format_arg(double n) noexcept : value(n) {
    }
    explicit format_arg(const char* s) : value(s) {
    }
    template<class Traits>
    explicit format_arg(std::basic_string_view<char, Traits> s) noexcept
        : value(std::string_view(s.data(), s.size())) {
    }

    template<class Traits, class Allocator>
    explicit format_arg(
        const std::basic_string<char, Traits, Allocator>& s) noexcept
        : value(std::string_view(s.data(), s.size())) {
    }

    explicit format_arg(std::nullptr_t) noexcept
        : value(static_cast<const void*>(nullptr)) {
    }

    template<class T, class = std::enable_if_t<std::is_void_v<T>>>
    explicit format_arg(const T* p) noexcept
        : value(static_cast<const void*>(p)) {
    }
};

template<class... Args>
using format_arg_store = std::array<format_arg, sizeof...(Args)>;

struct format_arg_span {
    template<size_t ArgCount>
    constexpr format_arg_span(const std::array<format_arg, ArgCount>& store)
        : data(store.data()), count(ArgCount) {
    }
    constexpr const format_arg* begin() const {
        return data;
    }
    constexpr const format_arg* end() const {
        return data + count;
    }
    const format_arg* data;
    unsigned count;
};

template<class... Args>
constexpr format_arg_store<Args...> pack_args(const Args&... args) {
    return {format_arg(args)...};
}

enum class delim_t : char {};

inline delim_t delim(char c) {
    return delim_t(c);
}

void vappend_to(format_context& out, format_arg_span args);
void vappend_to(format_context& out, delim_t delim, format_arg_span args);
void vformat_to(
    format_context& out, std::string_view format_str, format_arg_span args);

void vappend_to(std::string& str, format_arg_span args);
void vappend_to(std::string& str, delim_t delim, format_arg_span args);
void vformat_to(
    std::string& str, std::string_view format_str, format_arg_span args);

template<class... Args>
inline void append_inline(format_context& out, const Args&... args) {
    (append(out, args), ...);
}

template<class... Args>
inline void append(format_context& out, const Args&... args) {
    return vappend_to(out, pack_args(args...));
}
template<class... Args>
inline void append(format_context&& out, const Args&... args) {
    return vappend_to(out, pack_args(args...));
}

template<class... Args>
inline void append(format_context& out, delim_t delim, const Args&... args) {
    return vappend_to(out, delim, pack_args(args...));
}
template<class... Args>
inline void append(format_context&& out, delim_t delim, const Args&... args) {
    return vappend_to(out, delim, pack_args(args...));
}

template<class... Args>
inline void format_to(
    format_context& out, std::string_view format_str, const Args&... args) {
    return vformat_to(out, format_str, pack_args(args...));
}
template<class... Args>
inline void format_to(
    format_context&& out, std::string_view format_str, const Args&... args) {
    return vformat_to(out, format_str, pack_args(args...));
}

template<class T>
std::string to_string(const T& arg) {
    std::string str;
    string_format_context out(str);
    append(out, arg);
    out.finalize();
    return str;
}

template<class... Args>
void append(std::string& str, const Args&... args) {
    vappend_to(str, pack_args(args...));
}

template<class... Args>
void assign_concat(std::string& str, const Args&... args) {
    str.clear();
    vappend_to(str, pack_args(args...));
}

template<class... Args>
std::string concat(const Args&... args) {
    std::string str;
    vappend_to(str, pack_args(args...));
    return str;
}

template<class... Args>
std::string concat(delim_t delim, const Args&... args) {
    std::string str;
    vappend_to(str, delim, pack_args(args...));
    return str;
}

template<class... Args>
void format_to(
    std::string& str, std::string_view format_str, const Args&... args) {
    vformat_to(str, format_str, pack_args(args...));
}

template<size_t Size, class... Args>
size_t format_to(
    char (&arr)[Size], std::string_view format_str, const Args&... args) {
    format_context out(arr, Size);
    vformat_to(out, format_str, pack_args(args...));
    return out.size();
}

template<class... Args>
void assign_format(
    std::string& str, std::string_view format_str, const Args&... args) {
    str.clear();
    vformat_to(str, format_str, pack_args(args...));
}

template<class... Args>
std::string format(std::string_view format_str, const Args&... args) {
    std::string str;
    vformat_to(str, format_str, pack_args(args...));
    return str;
}

} // namespace fmt
} // namespace univang
