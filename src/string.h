#pragma once

#include <jutil/core.h>

namespace pnen
{
struct string {
    char *p;
    std::size_t n;
    JUTIL_CI char *begin() noexcept { return p; }
    JUTIL_CI char *end() noexcept { return p + n; }
    JUTIL_CI const char *begin() const noexcept { return p; }
    JUTIL_CI const char *end() const noexcept { return p + n; }
    JUTIL_CI char *data() noexcept { return p; }
    JUTIL_CI const char *data() const noexcept { return p; }
    JUTIL_CI std::size_t size() const noexcept { return n; }
    JUTIL_CI char &operator[](std::size_t i) noexcept { return p[i]; }
    JUTIL_CI const char &operator[](std::size_t i) const noexcept { return p[i]; }
    JUTIL_CI std::strong_ordering operator<=>(const std::string_view rhs) const noexcept
    {
        return std::string_view{*this} <=> rhs;
    }
    JUTIL_CI bool operator==(const std::string_view rhs) const noexcept
    {
        return std::is_eq(operator<=>(rhs));
    }
    JUTIL_CI std::strong_ordering operator<=>(const string rhs) const noexcept
    {
        return std::string_view{*this} <=> rhs;
    }
    JUTIL_CI bool operator==(const string rhs) const noexcept
    {
        return std::is_eq(operator<=>(rhs));
    }
    JUTIL_CI string substr(std::size_t m) const noexcept { return {p + m, n - m}; }
    JUTIL_CI operator std::string_view() const noexcept { return {p, n}; }
    JUTIL_CI std::string_view sv() const noexcept { return {p, n}; }
};
} // namespace pnen
