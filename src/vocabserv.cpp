#include "vocabserv.h"

#include <grp.h>
#include <jutil/argparse.h>
#include <jutil/b64.h>
#include <jutil/macro.h>
#include <pwd.h>
#include <sqlite3.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "buffer.h"
#include "format.h"
#include "server.h"
#include "string.h"

#include <jutil/lmacro.inl>

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

const char *get_pass(char *const buf, const std::size_t nbuf, const char *prompt) noexcept
{
    if (prompt) printf("%s", prompt);
    return echo_off([=] {
        const auto bufl = buf + nbuf - 1;
        auto dit        = buf;
        for (char c; dit != bufl && (c = getc(stdin)) != EOF && c != '\n';)
            *dit++ = c;
        putc('\n', stdout);
        *dit = '\0';
        return buf;
    });
}

template <std::size_t N>
JUTIL_INLINE const char *get_pass(char (&buf)[N], const char *prompt) noexcept
{
    return get_pass(buf, N, prompt);
}

namespace ptf
{
void test();
}

static constexpr auto user  = "nobody";
static constexpr auto group = "nogroup";

struct args_t : pnen::run_server_options {
    const char *user  = nullptr;
    const char *group = nullptr;
};

sqlite3 *g_pdb = nullptr;

int entry(args_t args, std::span<const char *const> posargs)
{
    if (args.user) {
        const auto uid = CHECK(getpwnam(user), != nullptr)->pw_uid;
        const auto gid = CHECK(getgrnam(group), != nullptr)->gr_gid;
        CHECK(setgroups(0, nullptr), == 0);
        CHECK(setgid(gid), == 0);
        CHECK(setuid(uid), == 0);
    }
    if (CHECK(access("..", R_OK | W_OK | X_OK), != -1 || errno == EACCES) == 0) {
        g_log.warn(".. is accessible");
    }
    if (CHECK(access(".", R_OK | W_OK | X_OK), != -1 || errno == EACCES) == -1) {
        g_log.info(". is not accessible");
    }
    const char *db = CHECK(posargs, .size() == 1)[0];
    g_log.info("opening database ", db);
    CHECK(sqlite3_open_v2(db, &g_pdb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, nullptr),
          == SQLITE_OK);
    DEFER[=] { sqlite3_close_v2(g_pdb); };
    CHECK(sqlite3_create_function(
              g_pdb, "b64en", 1, SQLITE_UTF8, nullptr,
              +[](sqlite3_context *ctx, int, sqlite3_value **args_) {
                  const auto nblob = sqlite3_value_bytes(args_[0]);
                  const auto blob  = reinterpret_cast<const char *>(sqlite3_value_blob(args_[0]));
                  const auto nbuf  = (nblob + 5) / 6 * 8;
                  const auto buf   = reinterpret_cast<char *>(sqlite3_malloc(nbuf));
                  auto bit =
                      &*jutil::b64_encode_pad(std::span{blob, static_cast<std::size_t>(nblob)},
                                              std::span{buf, static_cast<std::size_t>(nbuf)});
                  sqlite3_result_text(ctx, buf, bit - buf, sqlite3_free);
              },
              nullptr, nullptr),
          == SQLITE_OK);
    g_log.info("starting server at https://", args.host, ":", args.port);
    pnen::run_server(args);
    return 0;
}

int main(int argc, char *argv[])
{
    using namespace jutil;
    g_log.init(stdout, logger::level::debug);
    auto ap = argparse<"jutil", help_cmd,                                                         //
                       command<"H", "host", 1, "address to host on, default 0.0.0.0", L(x.host)>, //
                       command<"p", "port", 1, "port to host on, default 8080", L(x.port)>,       //
                       command<"u", "user", 1, "user to switch to", L(x.user)>,                   //
                       command<"g", "group", 1, "group to switch to", L(x.group)>                 //
                       >;
    return ap(
        argc, argv, [] { return args_t{}; }, entry);
}

bool detail::vocab::init(const char *path)
{
    const auto file = fopen(path, "rb");
    if (!file) return false;
    DEFER[=] { fclose(file); };
    fseek(file, 0, SEEK_END);
    const auto sz = static_cast<std::size_t>(ftell(file));
    fseek(file, 0, SEEK_SET);
    buf  = std::make_unique_for_overwrite<char[]>(sz);
    nbuf = fread(buf.get(), sizeof(char), sz, file);
    return true;
}

#include <jutil/lmacro.inl>
