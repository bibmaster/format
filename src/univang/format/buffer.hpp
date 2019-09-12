#pragma once
#include <memory>

#include "format_context.hpp"

namespace univang {
namespace fmt {

template<size_t Size>
class fs_buffer : public format_context {
public:
    fs_buffer() : format_context(store_, Size) {
    }
    virtual void grow(size_t /*new_capacity*/) {
        throw std::length_error("fs_buffer limit overflow");
    }

private:
    byte store_[Size];
};

template<size_t Size, class Allocator = std::allocator<format_context::byte>>
class buffer
    : public format_context
    , private Allocator {
public:
    explicit buffer(const Allocator& alloc = Allocator())
        : format_context(store_, Size), Allocator(alloc) {
    }
    buffer(buffer&& rhs) {
        move_from_(rhs);
    }
    buffer& operator=(buffer&& rhs) {
        assert(this != &rhs);
        deallocate_();
        move_from_(rhs);
        return *this;
    }
    ~buffer() {
        deallocate_();
    }

private:
    void grow(std::size_t size) override {
        auto old_capacity = this->capacity();
        auto new_capacity = old_capacity + old_capacity / 2;
        if(size > new_capacity)
            new_capacity = size;
        auto* old_data = this->data();
        auto* new_data = Allocator::allocate(new_capacity);
        std::memcpy(new_data, old_data, this->size());
        assign(new_data, new_capacity);
        if(old_data != store_)
            Allocator::deallocate(old_data, old_capacity);
    }

    void deallocate_() {
        if(this->data() != store_)
            Allocator::deallocate(this->data(), this->capacity());
    }

    void move_from_(buffer& rhs) {
        static_cast<Allocator&>(*this) =
            std::move(static_cast<Allocator&>(rhs));
        if(rhs.data() == rhs.store_)
            std::memcpy(store_, rhs.store_, rhs.size());
        else
            assign(rhs.data(), rhs.capacity());
        advance(rhs.size());
        rhs.assign(rhs.store_, 0);
    }

private:
    byte store_[Size];
};

template<class Allocator>
class buffer<0, Allocator>
    : public format_context
    , private Allocator {
public:
    explicit buffer(const Allocator& alloc = Allocator()) : Allocator(alloc) {
    }
    buffer(buffer&& rhs)
        : format_context(rhs.data(), rhs.capacity(), rhs.size())
        , Allocator(std::move(rhs)) {
        rhs.assign(nullptr, 0);
    }
    buffer& operator=(buffer&& rhs) {
        assert(this != &rhs);
        Allocator::deallocate(this->data(), this->capacity());
        static_cast<Allocator&>(*this) =
            std::move(static_cast<Allocator&>(rhs));
        assign(rhs.data(), rhs.capacity());
        advance(rhs.size());
        rhs.assign(nullptr, 0);
        return *this;
    }
    ~buffer() {
        Allocator::deallocate(this->data(), this->capacity());
    }

private:
    void grow(std::size_t size) override {
        auto old_capacity = this->capacity();
        auto new_capacity = old_capacity + old_capacity / 2;
        if(size > new_capacity)
            new_capacity = size;
        auto* old_data = this->data();
        auto* new_data = Allocator::allocate(new_capacity);
        std::memcpy(new_data, old_data, this->size());
        assign(new_data, new_capacity);
        Allocator::deallocate(old_data, old_capacity);
    }
};

using dynamic_buffer = buffer<0, std::allocator<format_context::byte>>;

} // namespace fmt
} // namespace univang
