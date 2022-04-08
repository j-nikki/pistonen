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
    JUTIL_INLINE bool init() const noexcept { return true; }
    bool init(const char *dir);
    template <class... Args>
    JUTIL_INLINE void print(Args &&...args) noexcept
    {
        std::scoped_lock lk{mtx};
        const auto len = buf.put(static_cast<Args &&>(args)..., "\n");
        fwrite(buf.data(), sizeof(char), len, file);
    }
    ~log()
    {
        if (file != stdout)
            fclose(file);
    }
    buffer buf;
    std::mutex mtx;
    FILE *file = stdout;
};
} // namespace detail

extern detail::log g_log;
extern detail::vocab g_vocab;
