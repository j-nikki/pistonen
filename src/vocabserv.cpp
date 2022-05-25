#include "vocabserv.h"

#include <filesystem>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "format.h"
#include "options.h"
#include "server.h"

namespace sc = std::chrono;
namespace sf = std::filesystem;

detail::log g_log;
detail::vocab g_vocab;
const char *g_wwwroot = ".";

auto echo_off(auto &&f) -> decltype(f())
{
    struct termios tp, save;
    tcgetattr(STDIN_FILENO, &tp);
    save = tp;
    tp.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tp);
    decltype(f()) res = f();
    tcsetattr(STDIN_FILENO, TCSANOW, &save);
    return res;
}

std::string_view get_pass(char *const buf, const std::size_t nbuf, const char *prompt) noexcept
{
    if (prompt)
        printf("%s", prompt);
    return echo_off([=] {
        const auto bufl = buf + nbuf;
        auto dit        = buf;
        for (char c; dit != bufl && (c = getc(stdin)) != EOF && c != '\n';)
            *dit++ = c;
        putc('\n', stdout);
        return std::string_view{buf, dit};
    });
}

template <std::size_t N>
std::string_view get_pass(char (&buf)[N], const char *prompt) noexcept
{
    return get_pass(buf, N, prompt);
}

int main(int argc, char **argv)
{
    using options::help;
    using options::strs;
    try {
        pnen::run_server_options opts{};
        const char *logdir = nullptr;

        const auto ov      = options::make_visitor([](const std::string_view sv) {
            fprintf(stderr, "unknown argument '%.*s'\n", static_cast<int>(sv.size()), sv.data());
            return 1;
        }) //
            (strs("-port-num", "p")(help, "Set the port to listen on."),
             [&](const std::string_view sv) {
                 if (sscanf(sv.data(), "%hu", &opts.hostport) != 1) {
                     fprintf(stderr, "couldn't read port as int (\"%s\")", sv.data());
                     return 1;
                 }
                 return 0;
             }) //
            (strs("-vocab-path", "v")(help, "Set path to vocab.gz."),
             [](const std::string_view sv) {
                 if (!g_vocab.init(sv.data())) {
                     fprintf(stderr, "couldn't open vocab file \"%s\"\n", sv.data());
                     return 1;
                 }
                 return 0;
             }) //
            (strs("-www-root", "w")(help, "Set path from which static files can be served."),
             [](const std::string_view sv) { g_wwwroot = sv.data(); }) //
            (strs("-log-dir", "l")(help, "Set path to dir into which log files are put."),
             [&](const std::string_view sv) { logdir = sv.data(); }) //
            (strs("-cert", "c")(help, "Set path to certificate file."),
             [&](const std::string_view cert) { opts.ssl_cert = cert.data(); }) //
            (strs("-pkey", "k")(help, "Set path to private key file."),
             [&](const std::string_view pkey) { opts.ssl_pkey = pkey.data(); }) //
            (strs("-pkpass", "P")(help, "Give pkey password, or 'prompt' for interactive prompt."),
             [&](const std::string_view pass) { opts.pk_pass = pass; }) //
            ("vocabserv", "program for serving a static vocabulary listing");

        if (const auto res = options::visit(argc, argv, ov, options::default_visitor))
            return res;

        char pwbuf[128];
        if (opts.pk_pass == "prompt")
            if (opts.pk_pass = get_pass(pwbuf, "Enter PEM pass phrase:"); opts.pk_pass.empty())
                return 1;

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
