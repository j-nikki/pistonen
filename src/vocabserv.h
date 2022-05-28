#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>

#include "buffer.h"
#include "jutil.h"

namespace detail
{
struct vocab {
    bool init(const char *path);
    std::unique_ptr<char[]> buf;
    std::size_t nbuf;
};

struct log {
    bool init(const char *dir);
    template <class... Args>
    JUTIL_INLINE void print(Args &&...args)
    {
        std::scoped_lock lk{mtx};
        const auto len = buf.put(static_cast<Args &&>(args)..., "\n");
        fwrite(buf.data(), sizeof(char), len, file);
    }
    template <class... Args>
    JUTIL_INLINE void debug(Args &&...args)
    {
        return print(static_cast<Args &&>(args)...);
    }
    template <class... Args>
    JUTIL_INLINE void error(Args &&...args)
    {
        return print(static_cast<Args &&>(args)...);
    }
    template <class... Args>
    JUTIL_INLINE void info(Args &&...args)
    {
        return print(static_cast<Args &&>(args)...);
    }
    template <class... Args>
    JUTIL_INLINE void warn(Args &&...args)
    {
        return print(static_cast<Args &&>(args)...);
    }
    ~log()
    {
        if (file != stdout) fclose(file);
    }
    buffer buf;
    std::mutex mtx;
    FILE *file = stdout;
};
} // namespace detail

extern detail::log g_log;
extern detail::vocab g_vocab;
extern const char *g_wwwroot;
