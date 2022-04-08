#include "format.h"

namespace format::detail
{
char *format_impl::itoa(const uint32_t x, char *d_f) noexcept
{
    // algorithm from here: https://jk-jeon.github.io/posts/2022/02/jeaiii-algorithm/
    uint64_t prod;

#define GET_NEXT_TWO_DIGITS()                                                                      \
    static_cast<int>((prod = static_cast<uint32_t>(prod) * uint64_t{100}) >> 32)
#define PRINT_1(Digit) *d_f++ = static_cast<char>((Digit) + '0')
#define PRINT_2(TwoDigits)                                                                         \
    do {                                                                                           \
        memcpy(d_f, radix_100_table + (TwoDigits)*2, 2);                                           \
        d_f += 2;                                                                                  \
    } while (0)
#define PRINT(MagicNumber, ExtraShift, RemainingCount)                                             \
    do {                                                                                           \
        prod                  = (x * (MagicNumber)) >> (ExtraShift);                               \
        const auto two_digits = static_cast<int>(prod >> 32);                                      \
        if (two_digits < 10) {                                                                     \
            PRINT_1(two_digits);                                                                   \
            for (int i = 0; i < (RemainingCount); ++i)                                             \
                PRINT_2(GET_NEXT_TWO_DIGITS());                                                    \
        } else {                                                                                   \
            PRINT_2(two_digits);                                                                   \
            for (int i = 0; i < (RemainingCount); ++i)                                             \
                PRINT_2(GET_NEXT_TWO_DIGITS());                                                    \
        }                                                                                          \
    } while (0)

    if (x < 100) {
        if (x < 10)
            PRINT_1(x);
        else
            PRINT_2(x);
    } else if (x < 100'0000) {
        if (x < 1'0000) {
            // 42949673 = ceil(2^32 / 10^2)
            PRINT(uint64_t{42949673}, 0, 1);
        } else {
            // 429497 = ceil(2^32 / 10^4)
            PRINT(uint64_t{429497}, 0, 2);
        }
    } else if (x < 1'0000'0000) {
        // 281474978 = ceil(2^48 / 10^6) + 1
        PRINT(uint64_t{281474978}, 16, 3);
    } else if (x < 10'0000'0000) {
        // 1441151882 = ceil(2^57 / 10^8) + 1
        prod = x * uint64_t{1441151882};
        prod >>= 25;
        PRINT_1(static_cast<int>(prod >> 32));
        PRINT_2(GET_NEXT_TWO_DIGITS());
        PRINT_2(GET_NEXT_TWO_DIGITS());
        PRINT_2(GET_NEXT_TWO_DIGITS());
        PRINT_2(GET_NEXT_TWO_DIGITS());
    } else {
        // 1441151881 = ceil(2^57 / 10^8)
        prod = x * uint64_t{1441151881};
        prod >>= 25;
        PRINT_2(static_cast<int>(prod >> 32));
        PRINT_2(GET_NEXT_TWO_DIGITS());
        PRINT_2(GET_NEXT_TWO_DIGITS());
        PRINT_2(GET_NEXT_TWO_DIGITS());
        PRINT_2(GET_NEXT_TWO_DIGITS());
    }

    return d_f;
}

char *format_impl::format_timestamp(char *const d_f) noexcept
{
    const auto now   = sc::time_point_cast<sc::seconds>(sc::system_clock::now());
    const auto today = sc::time_point_cast<sc::days>(now);
    const sc::year_month_day ymd{today};

    auto s       = now - today;
    const auto h = sc::duration_cast<sc::hours>(s);
    s -= h;
    const auto m = sc::duration_cast<sc::minutes>(s);
    s -= m;

    return format_impl::format(
        d_f, FMT_TIMESTAMP(sc::weekday{ymd}.c_encoding(), static_cast<unsigned>(ymd.day()),
                           static_cast<unsigned>(ymd.month()) - 1, static_cast<int>(ymd.year()),
                           h.count(), m.count(), s.count()));
}

const char format_impl::radix_100_table[]{
    '0', '0', '0', '1', '0', '2', '0', '3', '0', '4', '0', '5', '0', '6', '0', '7', '0', '8', '0',
    '9', '1', '0', '1', '1', '1', '2', '1', '3', '1', '4', '1', '5', '1', '6', '1', '7', '1', '8',
    '1', '9', '2', '0', '2', '1', '2', '2', '2', '3', '2', '4', '2', '5', '2', '6', '2', '7', '2',
    '8', '2', '9', '3', '0', '3', '1', '3', '2', '3', '3', '3', '4', '3', '5', '3', '6', '3', '7',
    '3', '8', '3', '9', '4', '0', '4', '1', '4', '2', '4', '3', '4', '4', '4', '5', '4', '6', '4',
    '7', '4', '8', '4', '9', '5', '0', '5', '1', '5', '2', '5', '3', '5', '4', '5', '5', '5', '6',
    '5', '7', '5', '8', '5', '9', '6', '0', '6', '1', '6', '2', '6', '3', '6', '4', '6', '5', '6',
    '6', '6', '7', '6', '8', '6', '9', '7', '0', '7', '1', '7', '2', '7', '3', '7', '4', '7', '5',
    '7', '6', '7', '7', '7', '8', '7', '9', '8', '0', '8', '1', '8', '2', '8', '3', '8', '4', '8',
    '5', '8', '6', '8', '7', '8', '8', '8', '9', '9', '0', '9', '1', '9', '2', '9', '3', '9', '4',
    '9', '5', '9', '6', '9', '7', '9', '8', '9', '9'};
} // namespace format::detail
