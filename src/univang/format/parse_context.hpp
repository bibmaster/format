#pragma once
#include <cassert>
#include <string_view>

namespace univang {
namespace fmt {

class parse_context {
public:
#if FMT_RESPECT_ALIASING
    using byte = std::byte;
#else
    enum class byte : char {};
#endif

    constexpr parse_context() noexcept = default;
    parse_context(const void* data, size_t size) noexcept
        : pos_(static_cast<const byte*>(data))
        , end_(pos_ + size)
        , eof_(size == 0) {
    }
    parse_context(std::string_view str) noexcept
        : parse_context(str.data(), str.size()) {
    }
    const byte* begin() const noexcept {
        return pos_;
    }
    const byte* end() const noexcept {
        return end_;
    }
    const byte* pos() const noexcept {
        return pos_;
    }
    const char* error() const noexcept {
        return err_;
    }
    std::size_t size() const noexcept {
        return end_ - pos_;
    }
    bool eof() const noexcept {
        return eof_;
    }
    bool fail() const noexcept {
        return err_ != nullptr;
    }
    const byte* find(char c) const noexcept {
        if(eof_)
            return nullptr;
        if(*pos_ == byte(c))
            return pos_;
        return static_cast<const byte*>(std::memchr(pos_, c, size()));
    }
    void advance_to(const byte* p) noexcept {
        pos_ = p;
        check_eof();
    }
    void advance(size_t count = 1) noexcept {
        pos_ += count;
        check_eof();
    }
    char front_char() const noexcept {
        assert(!eof_);
        return char(*pos_);
    }
    char consume_char() {
        assert(!eof_);
        auto res = char(*pos_++);
        check_eof();
        return res;
    }
    bool consume(char c) {
        if(eof_ || *pos_ != byte(c))
            return false;
        ++pos_;
        check_eof();
        return true;
    }
    void on_error(const char* err) noexcept {
        eof_ = true;
        err_ = err;
    }
    bool is_char(char c) const noexcept {
        return !eof_ && *pos_ == byte(c);
    }
    bool is_decimal_digit() const noexcept {
        return !eof_ && *pos_ >= byte('0') && *pos_ <= byte('9');
    }

private:
    void check_eof() noexcept {
        eof_ = pos_ == end_;
    }

private:
    const byte* pos_ = nullptr;
    const byte* end_ = nullptr;
    bool eof_ = true;
    const char* err_ = nullptr;
};

} // namespace fmt
} // namespace univang