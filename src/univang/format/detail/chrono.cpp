#include <univang/format/buffer.hpp>
#include <univang/format/chrono.hpp>

namespace univang {
namespace fmt {
namespace detail {

using nanoseconds = std::chrono::nanoseconds;
using seconds = std::chrono::seconds;
using minutes = std::chrono::minutes;
using hours = std::chrono::hours;
using days = std::chrono::duration<int, std::ratio<3600 * 24>>;

// calendar date
struct calendar_date_t {
    uint16_t y;
    uint8_t m;
    uint8_t d;
};

constexpr calendar_date_t make_calendar_date(days days_since_epoch) noexcept {
    const int epoch_gregorian_day = 719162;
    auto R = epoch_gregorian_day + days_since_epoch.count() + 307;
    auto H = 100 * R - 25;
    auto A = H / 3652425;
    A -= (A >> 2);
    uint16_t year = (100 * A + H) / 36525;
    auto C = A + R - 365 * year - (year >> 2);
    uint8_t month = ((535 * C + 48950) >> 14);
    uint8_t day = C - ((979 * month - 2918) >> 5);
    if(month > 12) {
        ++year;
        month -= 12;
    }
    return {year, month, day};
}

constexpr calendar_date_t date_from_epoch(seconds epoch_s) noexcept {
    return detail::make_calendar_date(
        std::chrono::duration_cast<days>(epoch_s));
}

// time of a day
struct time_of_day_t {
    uint8_t h;
    uint8_t m;
    uint8_t s;
};

constexpr time_of_day_t day_time_from_epoch(seconds epoch_s) noexcept {
    auto h = static_cast<uint8_t>(
        std::chrono::duration_cast<hours>(epoch_s % days(1)).count());
    auto m = static_cast<uint8_t>(
        std::chrono::duration_cast<minutes>(epoch_s % hours(1)).count());
    auto s = static_cast<uint8_t>((epoch_s % minutes(1)).count());
    return {h, m, s};
}

void append_time_point(format_context& out, seconds sec, nanoseconds ns) {
    auto d = date_from_epoch(sec);
    auto t = day_time_from_epoch(sec);
    char tmp[30] = {char('0' + (d.y / 1000) % 10),
                    char('0' + (d.y / 100) % 10),
                    char('0' + (d.y / 10) % 10),
                    char('0' + (d.y % 10)),
                    '-',
                    char('0' + (d.m / 10) % 10),
                    char('0' + (d.m % 10)),
                    '-',
                    char('0' + (d.d / 10) % 10),
                    char('0' + (d.d % 10)),
                    'T',
                    char('0' + (t.h / 10) % 10),
                    char('0' + (t.h % 10)),
                    ':',
                    char('0' + (t.m / 10) % 10),
                    char('0' + (t.m % 10)),
                    ':',
                    char('0' + (t.s / 10) % 10),
                    char('0' + (t.s % 10))};
    size_t len = 19;
    if(ns.count() != 0) {
        tmp[len++] = '.';
        uint32_t u = static_cast<uint32_t>(ns.count());
        uint32_t f = 1000000000;
        do {
            auto digit = (u / f) % 10;
            tmp[len++] = char('0' + digit);
            u %= f;
            f /= 10;
        } while(u != 0);
    }
    out.write(tmp, len);
}

void append_duration(format_context& out, seconds sec, nanoseconds ns) {
    if(sec.count() == 0 && ns.count() == 0) {
        append(out, '0');
        return;
    }
    fs_buffer<256> tmp;

    if(sec.count() < 0) {
        append(tmp, '-');
        sec = -sec;
        ns = -ns;
    }
    bool first = true;
    if(sec >= days(1)) {
        append_inline(tmp, std::chrono::duration_cast<days>(sec).count(), 'd');
        sec %= days(1);
        first = false;
    }
    if(sec >= hours(1)) {
        if(!first)
            append(tmp, ' ');
        append_inline(tmp, std::chrono::duration_cast<hours>(sec).count(), 'h');
        sec %= hours(1);
        first = false;
    }
    if(sec >= minutes(1)) {
        if(!first)
            append(tmp, ' ');
        append_inline(
            tmp, std::chrono::duration_cast<minutes>(sec).count(), 'm');
        sec %= minutes(1);
        first = false;
    }
    if(sec.count() != 0 || ns.count() != 0) {
        if(!first)
            append(tmp, ' ');
        append(tmp, sec.count());
        if(ns.count() != 0) {
            append(tmp, '.');
            uint32_t u = static_cast<uint32_t>(ns.count());
            uint32_t f = std::nano::den;
            do {
                auto digit = (u / f) % 10;
                append(tmp, char('0' + digit));
                u %= f;
                f /= 10;
            } while(u != 0);
        }
        append(tmp, 's');
    }
    out.write(tmp.data(), tmp.size());
}

} // namespace detail
} // namespace fmt
} // namespace univang