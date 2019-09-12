#pragma once
#include <cassert>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace univang {
namespace fmt {

class format_context {
public:
#if FMT_RESPECT_ALIASING
    using byte = std::byte;
#else
    enum class byte : char {};
#endif
    format_context() = default;
    constexpr format_context(
        void* data, size_t capacity, size_t size = 0) noexcept
        : data_(static_cast<byte*>(data)), capacity_(capacity), size_(size) {
    }
    format_context(const format_context&) = delete;
    format_context& operator=(const format_context&) = delete;
    virtual void grow(size_t /*new_capacity*/) {
        throw std::length_error("format_context limit overflow");
    }
    byte& operator[](size_t index) {
        assert(index <= capacity_);
        return data_[index];
    }
    byte operator[](size_t index) const {
        assert(index <= capacity_);
        return data_[index];
    }
    void clear() {
        size_ = 0;
    }
    void assign(void* data, size_t capacity) noexcept {
        data_ = static_cast<byte*>(data);
        capacity_ = capacity;
    }
    void reserve(size_t sz) {
        if(capacity_ < sz)
            grow(sz);
    }
    void ensure(size_t add_size) {
        reserve(size_ + add_size);
    }
    byte back() const {
        assert(size_ != 0);
        return data_[size_ - 1];
    }
    byte* data() const noexcept {
        return data_;
    }
    size_t size() const noexcept {
        return size_;
    }
    bool empty() const noexcept {
        return size_ == 0;
    }
    size_t capacity() const noexcept {
        return capacity_;
    }
    std::string_view get_str() const noexcept {
        return {reinterpret_cast<const char*>(data_), size_};
    }
    void advance(size_t sz = 1) noexcept {
        assert(size_ + sz <= capacity_);
        size_ += sz;
    }
    void add(byte b) {
        assert(size_ < capacity_);
        data_[size_++] = b;
    }
    void add(char c) {
        assert(size_ < capacity_);
        data_[size_++] = byte(c);
    }
    void write(char c) {
        ensure(1);
        add(c);
    }
    void add_padding(char c, size_t count) {
        assert(size_ + count <= capacity_);
        memset(data_ + size_, int(c), count);
        size_ += count;
    }
    void write_padding(char c, size_t count) {
        ensure(count);
        add_padding(c, count);
    }
    void make_c_str() {
        ensure(1);
        data_[size_] = byte(0);
    }
    void add(const void* p, size_t sz) {
        assert(size_ + sz <= capacity_);
        memcpy(data_ + size_, p, sz);
        size_ += sz;
    }
    void write(const void* p, size_t sz) {
        ensure(sz);
        add(p, sz);
    }
    template<class Char, class Traits>
    auto add(std::basic_string_view<Char, Traits> str)
        -> std::enable_if_t<sizeof(Char) == 1> {
        add(str.data(), str.size());
    }
    template<class Char, class Traits>
    auto write(std::basic_string_view<Char, Traits> str)
        -> std::enable_if_t<sizeof(Char) == 1> {
        write(str.data(), str.size());
    }

private:
    byte* data_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
};

template<class String>
class basic_string_format_context : public format_context {
public:
    basic_string_format_context(String& str)
        : format_context(str.data(), str.size(), str.size()), str_(str) {
    }
    ~basic_string_format_context() {
        finalize();
    }
    void grow(size_t new_capacity) final {
        str_.reserve(new_capacity);
        str_.resize(str_.capacity());
        format_context::assign(str_.data(), str_.size());
    }
    size_t finalize() {
        str_.resize(format_context::size());
        return format_context::size();
    }

private:
    String& str_;
};
using string_format_context = basic_string_format_context<std::string>;

} // namespace fmt
} // namespace univang
