#include "vocabserv.h"

#include <filesystem>
#include <stdio.h>

#include "format.h"
#include "server.h"

namespace sc = std::chrono;
namespace sf = std::filesystem;

detail::log g_log;
detail::vocab g_vocab;

int main(int argc, char **argv)
{
    try {
        if (argc < 2) {
            fprintf(stderr, "usage: %s <vocab-path> [<port-num>] [<log-dir>]\n", argv[0]);
            return 1;
        }

        if (!g_vocab.init(argv[1])) {
            fprintf(stderr, "couldn't open vocab file \"%s\"\n", argv[1]);
            return 1;
        }

        unsigned short port = 80;
        if (argc >= 3 && sscanf(argv[2], "%hu", &port) != 1) {
            fprintf(stderr, "couldn't read port as int (\"%s\")", argv[2]);
            return 1;
        }

        if (argc < 4)
            g_log.init();
        else if (!g_log.init(argv[3])) {
            fprintf(stderr, "couldn't open log file \"%s\"\n", argv[3]);
            return 1;
        }

        DBGEXPR(printf("server will run on 0.0.0.0:%hu...\n", port));
        run_server(port);

        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "%s: exception occurred: %s\n", argv[0], e.what());
        return 1;
    }
}

bool detail::vocab::init(const char *path)
{
    const auto file = fopen(path, "rb");
    if (!file)
        return false;
    DEFER[=] { fclose(file); };
    fseek(file, 0, SEEK_END);
    const auto sz = static_cast<std::size_t>(ftell(file));
    fseek(file, 0, SEEK_SET);
    buf  = std::make_unique_for_overwrite<char[]>(sz);
    nbuf = fread(buf.get(), sizeof(char), sz, file);
    return true;
}

bool detail::log::init(const char *dir)
{
    const auto id = static_cast<std::size_t>(
        std::distance(sf::directory_iterator{dir}, sf::directory_iterator{}));
    char path[64];
    format::format(path, std::string_view{dir}, "/", id, ".log\0");
    return (file = fopen(path, "w"));
}
