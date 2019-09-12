#include <gtest/gtest.h>
#include <univang/format/format.hpp>

namespace fmt = univang::fmt;

TEST(FormatTest, Escaping) {
    EXPECT_EQ("8-{", fmt::format("{0}-{{", 8));
}

TEST(FormatTest, Indexing) {
    EXPECT_EQ("a to b", fmt::format("{} to {}", "a", "b"));
    EXPECT_EQ("b to a", fmt::format("{1} to {0}", "a", "b"));
    EXPECT_EQ("a to a", fmt::format("{0} to {}", "a", "b"));
    EXPECT_EQ("a to b", fmt::format("{} to {1}", "a", "b"));
}

TEST(FormatTest, Alignment) {
    EXPECT_EQ("    42", fmt::format("{:6}", 42));
    EXPECT_EQ("x     ", fmt::format("{:6}", 'x'));
    EXPECT_EQ("x*****", fmt::format("{:*<6}", 'x'));
    EXPECT_EQ("*****x", fmt::format("{:*>6}", 'x'));
    EXPECT_EQ("**x***", fmt::format("{:*^6}", 'x'));
    EXPECT_EQ("   120", fmt::format("{:6d}", char(120)));
    EXPECT_EQ("true  ", fmt::format("{:6}", true));
}

TEST(FormatTest, Int) {
    EXPECT_EQ("42", fmt::format("{}", 42));
    EXPECT_EQ("101010 42 52 2a", fmt::format("{0:b} {0:d} {0:o} {0:x}", 42));
    EXPECT_EQ("0x2a 0X2A", fmt::format("{0:#x} {0:#X}", 42));
    EXPECT_EQ("1,234", fmt::format("{:n}", 1234));
    EXPECT_EQ("1,234,567,890", fmt::format("{:n}", 1234567890));
    EXPECT_EQ("1 +1 1  1", fmt::format("{0:} {0:+} {0:-} {0: }", 1));
    EXPECT_EQ("-1 -1 -1 -1", fmt::format("{0:} {0:+} {0:-} {0: }", -1));
}

TEST(DoubleTest, Special) {
    auto nan = std::numeric_limits<double>::quiet_NaN();
    auto inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ("inf +inf inf  inf", fmt::format("{0:} {0:+} {0:-} {0: }", inf));
    EXPECT_EQ(
        "-inf -inf -inf -inf", fmt::format("{0:} {0:+} {0:-} {0: }", -inf));
    EXPECT_EQ("nan nan nan nan", fmt::format("{0:} {0:+} {0:-} {0: }", nan));
    EXPECT_EQ("nan nan nan nan", fmt::format("{0:} {0:+} {0:-} {0: }", -nan));
}

TEST(DoubleTest, Zero) {
    EXPECT_EQ("0", fmt::format("{}", 0.0));
}

TEST(DoubleTest, Round) {
    EXPECT_EQ(
        "1.9156918820264798e-56", fmt::format("{}", 1.9156918820264798e-56));
    EXPECT_EQ("0.0000", fmt::format("{:.4f}", 7.2809479766055470e-15));
}

TEST(DoubleTest, DoublePrettify) {
    EXPECT_EQ("0.0001", fmt::format("{}", 1e-4));
    EXPECT_EQ("0.000001", fmt::format("{}", 1e-6));
    EXPECT_EQ("1e-7", fmt::format("{}", 1e-7));
    EXPECT_EQ("0.00009999", fmt::format("{}", 9.999e-5));
    EXPECT_EQ("10000000000", fmt::format("{}", 1e10));
    EXPECT_EQ("100000000000", fmt::format("{}", 1e11));
    EXPECT_EQ("12340000000", fmt::format("{}", 1234e7));
    EXPECT_EQ("12.34", fmt::format("{}", 1234e-2));
    EXPECT_EQ("0.001234", fmt::format("{}", 1234e-6));
}

TEST(DoubleTest, ZeroPrecision) {
    EXPECT_EQ("1", fmt::format("{:.0}", 1.0));
}

constexpr std::string_view color_names[] = {"red", "green", "blue"};

enum color { red, green, blue };

void format(fmt::format_context& out, color c) {
    out.write(color_names[c]);
};

TEST(FormatTest, Adl) {
    EXPECT_EQ("red", fmt::format("{}", red));
}

enum class color2 { red, green, blue };

template<>
struct fmt::formatter<color2> {
    void format(fmt::format_context& out, color2 c) {
        out.write(color_names[static_cast<int>(c)]);
    };
};

TEST(FormatTest, Formatter) {
    EXPECT_EQ("red", fmt::format("{}", color2::red));
}

struct color3 {
    uint8_t r, g, b;
    void format(fmt::format_context& out) const {
        fmt::format_to(
            out, "#{:06X}", unsigned(r) << 16 | unsigned(g) << 8 | unsigned(b));
    };
};

TEST(FormatTest, MemberFormat) {
    EXPECT_EQ("#00BFFF", fmt::format("{}", color3{0, 191, 255}));
}

struct foo {
    int x;
    int y;
};

void format_foo(
    fmt::parse_context& fmt, fmt::format_context& out, const foo& s,
    std::string_view prefix) {
    char type = 0;
    if(!fmt.eof())
        type = fmt.consume_char();
    if(type == 'x')
        fmt::append(out, prefix, '{', s.x, '}');
    else if(type == 'y')
        fmt::append(out, prefix, '{', s.y, '}');
    else
        fmt::append(out, prefix, '{', s.x, ',', s.y, '}');
};

struct with_adl_format : foo {};

void format(
    fmt::parse_context& fmt, fmt::format_context& out,
    const with_adl_format& s) {
    format_foo(fmt, out, s, "adl");
};

TEST(FormatTest, AdlParse) {
    with_adl_format s{123, 456};
    EXPECT_EQ("adl{123,456}", fmt::format("{}", s));
    EXPECT_EQ("adl{123}", fmt::format("{:x}", s));
    EXPECT_EQ("adl{456}", fmt::format("{:y}", s));
}

struct with_formatter : foo {};

template<>
struct fmt::formatter<with_formatter> {
    void format(
        fmt::parse_context& fmt, fmt::format_context& out,
        const with_formatter& s) const {
        format_foo(fmt, out, s, "fmt");
    }
};

TEST(FormatTest, FormatterParse) {
    with_formatter s{123, 456};
    EXPECT_EQ("fmt{123,456}", fmt::format("{}", s));
    EXPECT_EQ("fmt{123}", fmt::format("{:x}", s));
    EXPECT_EQ("fmt{456}", fmt::format("{:y}", s));
}

struct with_member_format : foo {
    void format(fmt::parse_context& fmt, fmt::format_context& out) const {
        format_foo(fmt, out, *this, "mem");
    }
};

TEST(FormatTest, MemberFormatParse) {
    with_member_format s{123, 456};
    EXPECT_EQ("mem{123,456}", fmt::format("{}", s));
    EXPECT_EQ("mem{123}", fmt::format("{:x}", s));
    EXPECT_EQ("mem{456}", fmt::format("{:y}", s));
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
