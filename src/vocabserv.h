#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sqlite3.h>

#include "buffer.h"
#include "log.h"

namespace detail
{
struct vocab {
    bool init(const char *path);
    std::unique_ptr<char[]> buf;
    std::size_t nbuf;
};
} // namespace detail

extern sqlite3 *g_pdb;
extern detail::vocab g_vocab;
extern const char *g_wwwroot;
