#include <algorithm>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/pop_back.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <boost/preprocessor/variadic/size.hpp>
#include <concepts>
#include <coroutine>
#include <fcntl.h>
#include <netinet/in.h>
#include <numeric>
#include <ranges>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "jutil.h"

#include "lmacro_begin.h"

namespace pnen
{
namespace detail
{
using namespace jutil;

#define PNEN_inline inline __attribute__((always_inline))

[[noreturn]] void error(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

#if !defined(NDEBUG) && !defined(__INTELLISENSE__)
#define CHECK(E, ...)                                                                              \
    []<class BOOST_PP_CAT(T, __LINE__)>(BOOST_PP_CAT(T, __LINE__) &&                               \
                                        BOOST_PP_CAT(x, __LINE__)) -> BOOST_PP_CAT(T, __LINE__) {  \
        if (!(BOOST_PP_CAT(x, __LINE__) __VA_ARGS__)) [[unlikely]]                                 \
            ::pnen::detail::error(__FILE__ ":" BOOST_PP_STRINGIZE(__LINE__) ": check failed: " #E " " BOOST_PP_STRINGIZE(__VA_ARGS__));              \
        return static_cast<BOOST_PP_CAT(T, __LINE__) &&>(BOOST_PP_CAT(x, __LINE__));               \
    }(E)
#define PNEN_assume(E) CHECK(E)
#define PNEN_dbg(T, F) T
#else
#define CHECK(E, ...) (E)
#define PNEN_assume(E)                                                                             \
    do {                                                                                           \
        if (!(E))                                                                                  \
            __builtin_unreachable();                                                               \
    } while (0)
#define PNEN_dbg(T, F) F
#endif

// clang-format off
template <class T>
concept t_has_first = requires(T x) {
    x.first();
    { x.has_first() } -> std::same_as<bool>;
};
// clang-format on
constexpr inline auto has_first = []<class T>(T &x) {
    if constexpr (t_has_first<T>)
        return x.has_first();
    else
        return x.has_next();
};
constexpr inline auto first = []<class T>(T &x) {
    if constexpr (t_has_first<T>)
        return x.first();
    else
        return x.next();
};

#define FOR_CO_AWAIT_bind(X) FOR_CO_AWAIT_bind_exp X
#define FOR_CO_AWAIT_bind_exp(x, ...) __VA_OPT__([) \
            x __VA_OPT__(,) __VA_ARGS__ __VA_OPT__(])
#define FOR_CO_AWAIT(x, y, ...)                                                                    \
    if (auto BOOST_PP_CAT(fca_it, __LINE__) =                                                      \
            BOOST_PP_VARIADIC_ELEM(BOOST_PP_VARIADIC_SIZE(__VA_ARGS__), y, __VA_ARGS__);           \
        0) {                                                                                       \
    } else if (bool BOOST_PP_CAT(fca_ok, __LINE__) =                                               \
                   ::pnen::detail::has_first(BOOST_PP_CAT(fca_it, __LINE__));                      \
               !BOOST_PP_CAT(fca_ok, __LINE__)) {                                                  \
    } else                                                                                         \
        for (auto &&BOOST_PP_CAT(fca_var, __LINE__) =                                              \
                 co_await ::pnen::detail::first(BOOST_PP_CAT(fca_it, __LINE__));                   \
             BOOST_PP_CAT(fca_ok, __LINE__);                                                       \
             (BOOST_PP_CAT(fca_ok, __LINE__) = BOOST_PP_CAT(fca_it, __LINE__).has_next()) &&       \
             (BOOST_PP_CAT(fca_var, __LINE__) = co_await BOOST_PP_CAT(fca_it, __LINE__).next(),    \
                                    0))                                                            \
            if (auto &&FOR_CO_AWAIT_bind(BOOST_PP_TUPLE_POP_BACK((x, y, __VA_ARGS__))) =           \
                    static_cast<decltype(BOOST_PP_CAT(fca_var, __LINE__)) &&>(                     \
                        BOOST_PP_CAT(fca_var, __LINE__));                                          \
                0) {                                                                               \
            } else

struct read_state {
    void *buf;
    size_t nbuf;
    ssize_t nread;
};

struct task {
    struct promise_type {
        read_state *rs;
        int sfd, tfd;
        promise_type()                     = default;
        promise_type(const promise_type &) = delete;
        promise_type(promise_type &&)      = delete;
        promise_type &operator=(const promise_type &) = delete;
        promise_type &operator=(promise_type &&) = delete;
        ~promise_type()
        {
            CHECK(close(sfd), != -1);
            CHECK(close(tfd), != -1);
        }
        constexpr PNEN_inline task get_return_object() & { return {*this}; }
        constexpr PNEN_inline std::suspend_always initial_suspend() { return {}; }
        constexpr PNEN_inline std::suspend_never final_suspend() noexcept { return {}; }
        constexpr PNEN_inline void return_void() {}
        constexpr PNEN_inline void unhandled_exception() {}
    };
    promise_type &p;
};

struct socket {
  private:
    struct read_res_base : read_state {
        [[nodiscard]] PNEN_inline bool has_next() noexcept { return true; }
    };
    struct read_res {
        read_res_base &b;
        PNEN_inline bool await_ready() noexcept { return false; }
        PNEN_inline void await_suspend(std::coroutine_handle<task::promise_type> h) noexcept
        {
            h.promise().rs = &b;
        }
        PNEN_inline std::tuple<ssize_t, void *&, size_t &> await_resume() noexcept
        {
            return {b.nread, b.buf, b.nbuf};
        }
    };
    struct read_res_iter : read_res_base {
        [[nodiscard]] PNEN_inline read_res next() noexcept { return {*this}; }
    };

  public:
    //! @brief Read indefinitely from socket
    //! @param buf The start of destination buffer
    //! @param nbuf The maximum amount of bytes to read
    //! @return read_res_iter Await-iterable yielding amount of bytes read
    read_res_iter read(void *const buf, const size_t nbuf) const noexcept { return {buf, nbuf}; }
};

using detail::socket;

struct run_server_options {
    uint16_t hostport;
    timespec timeout;
};

template <class F, class... Args>
using promisety = std::coroutine_traits<std::invoke_result_t<F, Args...>, Args...>::promise_type;
template <class F, class... Args>
using crhdlty = std::coroutine_handle<typename promisety<F, Args...>::promise_type>;

template <std::invocable<socket &&> Task>
PNEN_inline void run_server(const run_server_options o, Task on_accept)
{
    using crhdl     = crhdlty<Task, socket &&>;

    const auto acfd = CHECK(::socket(AF_INET, SOCK_STREAM, 0), != -1);
    const int flag  = 1;
    CHECK(setsockopt(acfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)), != -1);
    sockaddr_in sin{.sin_family = AF_INET, .sin_port = htons(o.hostport), .sin_addr{INADDR_ANY}};
    CHECK(bind(acfd, reinterpret_cast<const sockaddr *>(&sin), sizeof(sin)), != -1);
    CHECK(listen(acfd, 5), != -1);

    const auto epfd = CHECK(epoll_create1(0), != -1);
    epoll_event e{.events = EPOLLIN}, es[16];
    const itimerspec its{.it_value = o.timeout};
    CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, acfd, &e), != -1);
    for (;;) {
        int i = call_while(L0(epoll_wait(epfd, es, 16, -1), &), L(PNEN_dbg(x == -1, 0))) - 1;
        do {
            if (!es[i].data.ptr) {
                socklen_t nsin = sizeof(sin);
                const auto fd  = CHECK(
                     accept4(acfd, reinterpret_cast<sockaddr *>(&sin), &nsin, SOCK_NONBLOCK), != -1);
                auto &p    = on_accept(socket{}).p;
                p.sfd      = fd;
                auto h     = crhdl::from_promise(p);
                e.data.ptr = h.address();
                CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &e), != -1);

                p.tfd = CHECK(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC), -1);
                CHECK(timerfd_settime(p.tfd, 0, &its, nullptr), -1);
                e.data.ptr = to_ptr(to_uint(e.data.ptr) ^ 1);
                CHECK(reinterpret_cast<uintptr_t>(e.data.ptr) & 1);
                CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, p.tfd, &e) != -1);

                h.resume();
            } else if (const auto iptr = to_uint(es[i].data.ptr); iptr & 1) {
                crhdl::from_address(reinterpret_cast<void *>(iptr ^ 1)).destroy();
            } else [[likely]] {
                auto h                   = crhdl::from_address(es[i].data.ptr);
                auto &[rs, sfd, _]       = h.promise();
                auto &[buf, nbuf, nread] = *rs;
                if ((nread = CHECK(read(sfd, buf, nbuf), != -1)) != 0) [[likely]]
                    h.resume();
                else
                    h.destroy();
            }
        } while (i--);
    }
}

} // namespace detail
using detail::run_server;
using detail::run_server_options;
using detail::socket;
using detail::task;
} // namespace pnen

namespace sr = std::ranges;

int main(const int argc, char *const argv[])
{
    using SV = std::string_view;
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port-number>\n", argv[0]);
    }
    const pnen::run_server_options o{.hostport = static_cast<uint16_t>(atoi(argv[1])),
                                     .timeout  = {.tv_sec = 3}};
    pnen::run_server(o, [](pnen::socket s) -> pnen::task {
        static constexpr size_t nbuffer = 8 * 1024 * 1024;
        char buffer[nbuffer];
        FOR_CO_AWAIT (n, buf, nbuf, s.read(buffer, nbuffer)) {
            const SV svbuf{buffer, static_cast<size_t>(n)};
            const auto [it, _] = sr::search(svbuf, SV{"\r\n\r\n"});
            if (it == svbuf.end()) { // partial header only
                reinterpret_cast<char *&>(buf) += n;
                nbuf -= n;
                if (nbuf == 0)
                    co_return; // TODO: return "431 Request Header Fields Too Large"
            } else {           // end of header (CRLFCRLF)
                printf("received message:\n");
                printf("vvv\n%.*s\n^^^\n", static_cast<int>(n), buffer);
                buf  = buffer;
                nbuf = nbuffer;
                // TODO: read header and respond to it
            }
        }
    });
}