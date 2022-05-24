#include "vocabserv.h"

#include <filesystem>
#include <stdio.h>

#include "format.h"
#include "options.h"
#include "server.h"

namespace sc = std::chrono;
namespace sf = std::filesystem;

detail::log g_log;
detail::vocab g_vocab;
const char *g_wwwroot = ".";

int main(int argc, char **argv)
{
    using options::strs;
    try {
        pnen::run_server_options opts{};
        const char *logdir = nullptr;

        const auto ov      = options::make_visitor([](const std::string_view sv) {
            fprintf(stderr, "unknown argument '%.*s'\n", static_cast<int>(sv.size()), sv.data());
            return 1;
        }) //
            (strs("-help", "h"),
             [&] {
                 fprintf(stderr,
                         "%s --port-num <port> --vocab-path <path> --www-root <path> --log-dir "
                         "<path> --cert <path> --pkey <path>",
                         argv[0]);
                 return 1;
             }) //
            ("-port-num",
             [&](const std::string_view sv) {
                 if (sscanf(sv.data(), "%hu", &opts.hostport) != 1) {
                     fprintf(stderr, "couldn't read port as int (\"%s\")", sv.data());
                     return 1;
                 }
                 return 0;
             }) //
            ("-vocab-path",
             [](const std::string_view sv) {
                 if (!g_vocab.init(sv.data())) {
                     fprintf(stderr, "couldn't open vocab file \"%s\"\n", sv.data());
                     return 1;
                 }
                 return 0;
             })                                                                          //
            ("-www-root", [](const std::string_view sv) { g_wwwroot = sv.data(); })      //
            ("-log-dir", [&](const std::string_view sv) { logdir = sv.data(); })         //
            ("-cert", [&](const std::string_view cert) { opts.ssl_cert = cert.data(); }) //
            ("-pkey", [&](const std::string_view pkey) { opts.ssl_pkey = pkey.data(); }) //
            ();

        if (const auto res = options::visit(argc, argv, ov, options::default_visitor))
            return res;

        if (!logdir) {
            g_log.init();
        } else if (!g_log.init(logdir)) {
            fprintf(stderr, "couldn't access log dir \"%s\"\n", logdir);
            return 1;
        }

        DBGEXPR(printf("server will run on https://localhost:%hu...\n", opts.hostport));
        pnen::run_server(opts, handle_connection);

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
    // TODO: replace id with timestamp
    const auto id = static_cast<std::size_t>(
        std::distance(sf::directory_iterator{dir}, sf::directory_iterator{}));
    char path[64];
    format::format(path, std::string_view{dir}, "/", id, ".log\0");
    return (file = fopen(path, "w"));
}
