#pragma once
#include <chrono>
#include "format.hpp"

namespace univang {
namespace fmt {

namespace detail {

using seconds = std::chrono::seconds;
using nanoseconds = std::chrono::nanoseconds;

void append_time_point(format_context& out, seconds sec, nanoseconds ns);

template<class Clock, class Duration>
inline void append(
    format_context& out, const std::chrono::time_point<Clock, Duration>& tp) {
    detail::append_time_point(
        out, seconds(Clock::to_time_t(tp)),
        std::chrono::duration_cast<nanoseconds>(
            tp - std::chrono::floor<seconds>(tp)));
}

void append_duration(format_context& out, seconds sec, nanoseconds ns);
template<class Rep, class Period>
void append(
    format_context& out, const std::chrono::duration<Rep, Period>& dur) {
    using in_duration = std::chrono::duration<Rep, Period>;

    if(dur == in_duration::max())
        return detail::append_duration(out, seconds::max(), nanoseconds(0));
    if(dur == in_duration::min())
        return detail::append_duration(out, seconds::min(), nanoseconds(0));
    detail::append_duration(
        out, std::chrono::duration_cast<seconds>(dur),
        std::chrono::duration_cast<nanoseconds>(dur % seconds(1)));
}

} // namespace detail

template<class Clock, class Duration>
struct formatter<std::chrono::time_point<Clock, Duration>> {
    using time_point = std::chrono::time_point<Clock, Duration>;
    void format(format_context& out, const time_point& tp) {
        detail::append(out, tp);
    }
};

template<class Rep, class Period>
struct formatter<std::chrono::duration<Rep, Period>> {
    using duration = std::chrono::duration<Rep, Period>;
    void format(format_context& out, const duration& dur) {
        detail::append(out, dur);
    }
};

} // namespace fmt
} // namespace univang
