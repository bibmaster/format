#pragma once
namespace univang {
namespace fmt {
namespace detail {

class format_parse_context : public parse_context {
public:
    format_parse_context(std::string_view str, format_arg_span args) noexcept
        : parse_context(str), args_(args) {
    }
    unsigned next_arg() {
        return last_arg_pos_++;
    }
    unsigned arg_count() const {
        return args_.count;
    }
    const format_arg::value_type& get_arg(unsigned pos) {
        return args_.data[pos].value;
    }

private:
    format_arg_span args_;
    unsigned last_arg_pos_ = 0;
};

unsigned parse_uint(format_parse_context& parser) {
    unsigned result = parser.consume_char() - '0';
    // TODO: check overflow
    while(parser.is_decimal_digit())
        result = result * 10 + (parser.consume_char() - '0');
    return result;
}

struct int_handler {
    template<class T>
    std::enable_if_t<std::is_convertible_v<T, int>, int> operator()(
        const T& v) const {
        return static_cast<int>(v);
    }
    template<class T>
    std::enable_if_t<!std::is_convertible_v<T, int>, int> operator()(
        const T& v) const {
        return -1;
    }
};

unsigned parse_arg_ref(format_parse_context& parser) {
    unsigned arg_pos =
        parser.is_decimal_digit() ? parse_uint(parser) : parser.next_arg();
    if(!parser.fail() && arg_pos >= parser.arg_count())
        parser.on_error("arg num out of range");
    return arg_pos;
}

bool parse_uint_spec_arg(format_parse_context& parser, unsigned& result) {
    if(!parser.consume('{'))
        result = parse_uint(parser);
    else {
        auto arg_pos = parse_arg_ref(parser);
        if(!parser.fail()) {
            if(!parser.consume('}'))
                parser.on_error("dynamic format: missing '}'");
            else {
                auto& arg = parser.get_arg(arg_pos);
                auto i = std::visit(int_handler(), arg);
                if(i < 0)
                    parser.on_error("not an integer arg");
                else
                    result = static_cast<unsigned>(i);
            }
        }
    }
    return !parser.fail();
}

bool parse_format_spec(format_parse_context& parser, format_spec& spec) {
    if(parser.eof()) {
        parser.on_error("invalid format spec");
        return false;
    }
    auto is_align = [](char c) { return (c >= '<' && c <= '>') || c == '^'; };

    auto c = parser.front_char();
    auto* next = parser.begin() + 1;
    if(next != parser.end() && is_align(char(*next))) {
        if(c == '{') {
            parser.on_error("invalid fill char");
            return false;
        }
        spec.fill = c;
        spec.align = char(*next);
        parser.advance_to(next + 1);
        c = parser.eof() ? 0 : parser.front_char();
    }
    else if(is_align(c)) {
        spec.align = c;
        parser.advance();
        c = parser.eof() ? 0 : parser.front_char();
    }
    if(c == '+' || c == '-' || c == ' ') {
        spec.sign = c;
        parser.advance();
        c = parser.eof() ? 0 : parser.front_char();
    }
    if(c == '#') {
        spec.alt = c;
        parser.advance();
        c = parser.eof() ? 0 : parser.front_char();
    }
    if(c == '0') {
        spec.fill = '0';
        spec.align = '=';
        parser.advance();
        c = parser.eof() ? 0 : parser.front_char();
    }
    if(c == '{' || (c >= '0' && c <= '9')) {
        if(!parse_uint_spec_arg(parser, spec.width))
            return false;
        c = parser.eof() ? 0 : parser.front_char();
    }
    if(c == '.') {
        parser.advance();
        spec.has_precision = true;
        c = parser.eof() ? 0 : parser.front_char();
        if(c == '{' || (c >= '0' && c <= '9')) {
            if(!parse_uint_spec_arg(parser, spec.precision))
                return false;
            c = parser.front_char();
        }
    }
    if(c != '}') {
        spec.type = c;
        parser.advance();
        c = parser.eof() ? 0 : parser.front_char();
    }
    if(c != '}') {
        parser.on_error("invalid format spec");
        return false;
    }
    parser.advance();
    return true;
}

} // namespace detail
} // namespace fmt
} // namespace univang
