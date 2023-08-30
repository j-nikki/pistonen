#include "log.h"

#include <time.h>

void logger::init(FILE *const f, const level lvl_)
{
    start = clock::now();
    file  = f;
    lvl   = lvl_;
}

bool logger::init(const std::string_view dir, const level lvl_)
{
    const auto t = time(nullptr);
    const auto z = gmtime(&t);
    char path[512];
    const auto ndir = dir.copy(path, 512);
    if (!strftime(path + ndir, 512 - ndir, "log-%Y%m%dT%H%M%SZ", z)) return false;
    return init(fopen(path, "w"), lvl_), file;
}

#ifndef LOG_NO_GLOG
logger g_log;
#endif
