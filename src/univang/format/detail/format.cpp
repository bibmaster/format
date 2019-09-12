#include "univang/format/format.hpp"
#include "univang/format/buffer.hpp"

#include "format_utils.hpp"

#include "format_double.hpp"
#include "format_integer.hpp"
#include "format_parsing.hpp"

namespace univang {
namespace fmt {
namespace detail {
namespace {

struct append_handler {
    append_handler(format_context& out) : out(out) {
    }
    void operator()(const format_arg::handle& v) {
        v.format_fn(out, dummy_fmt, v.ptr);
    }
    template<class T>
    void operator()(const T& v) {
        ::univang::fmt::append(out, v);
    }
    format_context& out;
    parse_context dummy_fmt;
};

struct format_handler {
    format_handler(format_context& out, format_parse_context& fmt)
        : out(out), fmt(fmt) {
    }
    template<class T>
    void format_int(T arg) {
        if(!do_format_int(out, spec, arg))
            fmt.on_error("invalid numeric type");
    }
    void format_str(std::string_view v) {
        // TODO: utf-8 specialization
        if(!spec.align)
            spec.align = '<';
        detail::write_padded(out, spec, padded_string(v));
    }
    void operator()(bool arg) {
        format_str(arg ? "true" : "false");
    }
    void operator()(char arg) {
        if(spec.type && spec.type != 's' && spec.type != 'c')
            format_int((unsigned)arg);
        else {
            if(!spec.align)
                spec.align = '<';
            detail::write_padded(out, spec, padded_char(arg));
        }
    }
    void operator()(int arg) {
        format_int(arg);
    }
    void operator()(unsigned int arg) {
        format_int(arg);
    }
    void operator()(long arg) {
        format_int(arg);
    }
    void operator()(unsigned long arg) {
        format_int(arg);
    }
    void operator()(long long arg) {
        format_int(arg);
    }
    void operator()(unsigned long long arg) {
        format_int(arg);
    }
    void operator()(double arg) {
        if(!validate_float_spec(spec))
            fmt.on_error("invalid floating type");
        else {
            // do_format_double(out, spec, arg);
            detail::do_format_double(out, spec, arg);
        }
    }
    void operator()(const char* arg) {
        if(spec.type == 'p')
            return operator()((const void*)arg);
        format_str(arg);
    }
    void operator()(std::string_view arg) {
        format_str(arg);
    }
    void operator()(const void* arg) {
        append(out, arg);
    }
    void operator()(const format_arg::handle& /*arg*/) {
    }
    format_context& out;
    format_parse_context& fmt;
    format_spec spec;
};

} // namespace
} // namespace detail

// Append integers.
void append(format_context& out, int arg) {
    detail::append_dec(out, arg);
}

void append(format_context& out, unsigned int arg) {
    detail::append_dec(out, arg);
}

void append(format_context& out, long arg) {
    detail::append_dec(out, arg);
}

void append(format_context& out, unsigned long arg) {
    detail::append_dec(out, arg);
}

void append(format_context& out, long long arg) {
    detail::append_dec(out, arg);
}

void append(format_context& out, unsigned long long arg) {
    detail::append_dec(out, arg);
}

// Append float.
void append(format_context& out, double arg) {
    format_spec empty_spec;
    // do_format_double(out, empty_spec, arg);
    detail::do_format_double(out, empty_spec, arg);
}

// Append pointer.
void append(format_context& out, const void* v) {
    auto u = reinterpret_cast<uintptr_t>(v);
    constexpr unsigned width = sizeof(u) * 2;
    format_context::byte tmp[width];
    auto pos = detail::write_int(tmp, sizeof(tmp), u, 16);
    out.ensure(width + 2);
    out.add('0');
    out.add('x');
    out.add_padding('0', pos);
    out.add(tmp + pos, width - pos);
}

void vappend_to(format_context& out, format_arg_span args) {
    for(const auto& arg : args)
        std::visit(detail::append_handler(out), arg.value);
}

void vappend_to(format_context& out, delim_t delim, format_arg_span args) {
    if(args.count == 0)
        return;
    std::visit(detail::append_handler(out), args.data[0].value);
    for(size_t i = 1; i < args.count; ++i) {
        append(out, char(delim));
        std::visit(detail::append_handler(out), args.data[i].value);
    }
}

void vappend_to(std::string& str, format_arg_span args) {
    string_format_context out(str);
    vappend_to(out, args);
}

void vappend_to(std::string& str, delim_t delim, format_arg_span args) {
    string_format_context out(str);
    vappend_to(out, delim, args);
}

void vformat_to(
    format_context& out, std::string_view format_str, format_arg_span args) {
    detail::format_parse_context fmt{format_str, args};
    while(!fmt.eof()) {
        const auto* p = fmt.find('{');
        if(p == nullptr)
            return out.write(fmt.begin(), fmt.size());
        out.write(fmt.begin(), p - fmt.begin());
        fmt.advance_to(p + 1);
        if(fmt.eof()) {
            fmt.on_error("invalid format string");
            break;
        }
        if(fmt.consume('{')) {
            out.write('{');
            continue;
        }
        unsigned arg_pos = parse_arg_ref(fmt);
        if(fmt.fail())
            break;
        if(!fmt.is_char('}') && (!fmt.consume(':') || fmt.eof())) {
            fmt.on_error("invalid format string");
            break;
        }
        const auto& arg = fmt.get_arg(arg_pos);
        if(fmt.consume('}')) {
            std::visit(detail::append_handler(out), arg);
        }
        else if(!std::holds_alternative<format_arg::handle>(arg)) {
            detail::format_handler handler{out, fmt};
            if(!parse_format_spec(fmt, handler.spec))
                break;
            std::visit(handler, arg);
        }
        else {
            p = fmt.find('}');
            if(p == nullptr) {
                fmt.on_error("invalid format string");
                break;
            }
            auto& handle = std::get<format_arg::handle>(arg);
            parse_context arg_fmt{fmt.pos(), size_t(p - fmt.pos())};
            fmt.advance_to(p + 1);
            handle.format_fn(out, arg_fmt, handle.ptr);
        }
    }
    if(fmt.fail())
        out.write(std::string_view(fmt.error()));
}

void vformat_to(
    std::string& str, std::string_view format_str, format_arg_span args) {
    string_format_context out(str);
    vformat_to(out, format_str, args);
}

} // namespace fmt
} // namespace univang
