#pragma once

#include <algorithm>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/pop_back.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <boost/preprocessor/variadic/size.hpp>
#include <concepts>
#include <coroutine>
#include <experimental/memory>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <numeric>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <ranges>
#include <source_location>
#include <span>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <tls.h>
#include <unistd.h>

#include "../jutil.h"
#include "../vocabserv.h"

#include "../lmacro_begin.h"

namespace pnen::detail
{
using namespace jutil;

#define PNEN_push_diag(x)     _Pragma("GCC diagnostic push") x
#define PNEN_pop_diag()       _Pragma("GCC diagnostic pop")

#define PNEN_wno_unused_value _Pragma("GCC diagnostic ignored \"-Wunused-value\"")

#if !defined(NDEBUG) && !defined(__INTELLISENSE__)
#define PNEN_assume(E) CHECK(E)
#define PNEN_dbg(T, F) T
#else
#define PNEN_assume(E)                                                                             \
    do {                                                                                           \
        if (!(E)) __builtin_unreachable();                                                         \
    } while (0)
#define PNEN_dbg(T, F) F
#endif

enum class loop_state {
    suspend,   // spurious wake -> resuspend
    error,     // got error -> go to else-block
    has_next,  // next() available -> enter loop
    exhausted, // exhausted data -> exit loop
};

// clang-format off
template <class T>
concept has_next = requires (T x) {
    x.next();
};
template <class T>
concept has_first_state = requires (T x) {
    x.first_state();
};
// clang-format on

struct empty {};
JUTIL_CI empty next(auto &&) noexcept { return {}; }
JUTIL_CI auto next(has_next auto &&x) noexcept -> decltype(x.next()) { return x.next(); }
struct always_suspend : std::suspend_never {
    JUTIL_CI loop_state await_resume() const noexcept { return loop_state::suspend; }
};
JUTIL_CI always_suspend first_state(auto &&) noexcept { return {}; }
JUTIL_CI auto first_state(has_first_state auto &&x) noexcept(noexcept(x.first_state()))
{
    return x.first_state();
}

#define FOR_CO_AWAIT_bind(X) FOR_CO_AWAIT_bind_exp X
#define FOR_CO_AWAIT_bind_exp(x, ...) __VA_OPT__([) x __VA_OPT__(,) __VA_ARGS__ __VA_OPT__(])
#define FOR_CO_AWAIT_impl(x, y, ...)                                                               \
    if (auto BOOST_PP_CAT(fca_it, __LINE__) =                                                      \
            BOOST_PP_VARIADIC_ELEM(BOOST_PP_VARIADIC_SIZE(__VA_ARGS__), y, __VA_ARGS__);           \
        0) {                                                                                       \
    } else if (auto BOOST_PP_CAT(fca_s, __LINE__) =                                                \
                   co_await ::pnen::detail::first_state(BOOST_PP_CAT(fca_it, __LINE__));           \
               0) {                                                                                \
    } else                                                                                         \
        for (; BOOST_PP_CAT(fca_s, __LINE__) != ::pnen::detail::loop_state::exhausted;             \
             BOOST_PP_CAT(fca_s, __LINE__) =                                                       \
                 (BOOST_PP_CAT(fca_s, __LINE__) == ::pnen::detail::loop_state::error)              \
                     ? ::pnen::detail::loop_state::exhausted                                       \
                     : co_await BOOST_PP_CAT(fca_it, __LINE__).state())                            \
            if (BOOST_PP_CAT(fca_s, __LINE__) == ::pnen::detail::loop_state::suspend) {            \
                continue;                                                                          \
            } else                                                                                 \
                JUTIL_PUSH_DIAG(JUTIL_WNO_DANGLING_ELSE)                                           \
    if (BOOST_PP_CAT(fca_s, __LINE__) == ::pnen::detail::loop_state::has_next)                     \
        if (auto &&FOR_CO_AWAIT_bind(BOOST_PP_TUPLE_POP_BACK((x, y __VA_OPT__(, __VA_ARGS__)))) =  \
                ::pnen::detail::next(BOOST_PP_CAT(fca_it, __LINE__));                              \
            0) {                                                                                   \
        } else                                                                                     \
            JUTIL_POP_DIAG()
#define FOR_CO_AWAIT_dummy(...)                                                                    \
    FOR_CO_AWAIT_impl(BOOST_PP_CAT(fca_dummy, __LINE__), __VA_ARGS__)                              \
        PNEN_push_diag(PNEN_wno_unused_value) if ((BOOST_PP_CAT(fca_dummy, __LINE__), 0))          \
            PNEN_pop_diag();                                                                       \
    else for (bool BOOST_PP_CAT(fca_once, __LINE__) = true; BOOST_PP_CAT(fca_once, __LINE__);      \
              BOOST_PP_CAT(fca_once, __LINE__)      = false)
#define FOR_CO_AWAIT(x, ...)                                                                       \
    BOOST_PP_IF(BOOST_PP_CHECK_EMPTY(__VA_ARGS__), FOR_CO_AWAIT_dummy, FOR_CO_AWAIT_impl)          \
    (x, __VA_ARGS__)

struct read_state {
    tls *tc;
    char *buf;
    char *bufspn;
    size_t nbufspn;
};

struct write_state {
    tls *tc;
    const char *buf;
    size_t nbuf;
};

void log_ssl_error(tls *tc, const char *fname, int err,
                   std::source_location sl = std::source_location::current());

struct task {
    JUTIL_PUSH_DIAG(JUTIL_WNO_SUBOBJ_LINKAGE)
    struct promise_type {
        tls *tc;
        int sfd, tfd;
        promise_type()                                = default;
        promise_type(const promise_type &)            = delete;
        promise_type(promise_type &&)                 = delete;
        promise_type &operator=(const promise_type &) = delete;
        promise_type &operator=(promise_type &&)      = delete;
        ~promise_type()
        {
            CHECK(tls_close(tc), != -1);
            CHECK(close(sfd), != -1);
            CHECK(close(tfd), != -1);
            tls_free(tc);
        }
        constexpr JUTIL_INLINE task get_return_object() & { return {*this}; }
        constexpr JUTIL_INLINE std::suspend_always initial_suspend() { return {}; }
        constexpr JUTIL_INLINE std::suspend_never final_suspend() noexcept { return {}; }
        constexpr JUTIL_INLINE void return_void() {}
        constexpr JUTIL_INLINE void unhandled_exception() {}
    };
    JUTIL_POP_DIAG()
    promise_type &p;
};

struct socket {
    tls *tc;

    //
    // read
    //

  private:
    struct read_state_awaitable {
        read_state &rs;
        JUTIL_INLINE bool await_ready() noexcept { return false; }
        JUTIL_INLINE void await_suspend(std::coroutine_handle<>) noexcept {}
        JUTIL_INLINE loop_state await_resume() noexcept
        {
            // TODO: investigate broken pipe: g_log.debug() every socket-related action
            const auto ret = tls_read(rs.tc, rs.bufspn, rs.nbufspn);
            if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) {
                return loop_state::suspend;
            } else if (ret == -1) {
                return loop_state::error;
            } else [[likely]] {
                rs.bufspn += ret;
                rs.nbufspn -= ret;
                return loop_state::has_next;
            }
        }
    };
    struct read_res : read_state {
        JUTIL_INLINE read_state_awaitable state() noexcept { return {*this}; }
        JUTIL_INLINE std::tuple<std::span<char>, read_state &> next() noexcept
        {
            return {{buf, bufspn}, {*this}};
        }
    };

  public:
    //! @brief Read indefinitely from socket
    //! @param buf The start of destination buffer
    //! @param nbuf The maximum amount of bytes to read
    //! @return read_res_iter Await-iterable yielding amount of bytes read
    [[nodiscard]] JUTIL_INLINE read_res read(char *const buf, const size_t nbuf) const noexcept
    {
        return {{.tc = tc, .buf = buf, .bufspn = buf, .nbufspn = nbuf}};
    }

    //
    // write
    //

  private:
    template <bool F>
    struct write_state_awaitable {
        write_state &ws;
        JUTIL_INLINE bool await_ready() noexcept { return false; }
        JUTIL_INLINE void await_suspend(std::coroutine_handle<>) noexcept {}
        JUTIL_INLINE loop_state await_resume() noexcept
        {
            // if constexpr (F) return loop_state::exhausted;
            const auto ret = tls_write(ws.tc, ws.buf, ws.nbuf);
            if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT) {
                return loop_state::suspend;
            } else if (ret == -1) {
                return loop_state::error;
            } else [[likely]] {
                ws.buf += ret;
                ws.nbuf -= ret;
                return (ws.nbuf > 0) ? loop_state::suspend : loop_state::exhausted;
            }
        }
    };
    struct write_res : write_state {
        JUTIL_INLINE write_state_awaitable<true> first_state() noexcept { return {*this}; }
        JUTIL_INLINE write_state_awaitable<false> state() noexcept { return {*this}; }
    };

  public:
    JUTIL_INLINE write_res write(const char *const buf, size_t nbuf) const noexcept
    {
        return {{.tc = tc, .buf = buf, .nbuf = nbuf}};
    }
    [[nodiscard]] JUTIL_INLINE write_res write(const std::string_view buf) const noexcept
    {
        return write(buf.data(), buf.size());
    }
};

struct run_server_options {
    uint16_t hostport    = 3000;
    timespec timeout     = {.tv_sec = 5};
    const char *ssl_cert = nullptr;
    const char *ssl_pkey = nullptr;
    const char *pk_pass  = {};
};

template <class F, class... Args>
using promisety = typename std::coroutine_traits<call_result<F, Args...>, Args...>::promise_type;
template <class F, class... Args>
using crhdlty = typename std::coroutine_handle<typename promisety<F, Args...>::promise_type>;

enum class severity { info, error };

template <callable_r<task, socket &&> Task>
JUTIL_INLINE void run_server(const run_server_options o, Task on_accept)
{
    using crhdl     = crhdlty<Task, socket &&>;

    const auto acfd = CHECK(::socket(AF_INET, SOCK_STREAM, 0), != -1);
    const int flag  = 1;
    CHECK(setsockopt(acfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)), != -1);
    sockaddr_in sin{.sin_family = AF_INET, .sin_port = htons(o.hostport), .sin_addr{INADDR_ANY}};
    CHECK(bind(acfd, reinterpret_cast<const sockaddr *>(&sin), sizeof(sin)), != -1);
    CHECK(listen(acfd, 5), != -1);

    const auto tcnf = tls_config_new();
    if (!tcnf) return g_log.error("tls_config_new() failed");
    DEFER[=] { tls_config_free(tcnf); };

    tls_config_set_cert_file(tcnf, o.ssl_cert);
    size_t nkey;
    const auto keyf = tls_load_file(o.ssl_pkey, &nkey, const_cast<char *>(o.pk_pass));
    if (!keyf) return g_log.error("tls_load_file() failed");
    DEFER[=] { tls_unload_file(keyf, nkey); };
    CHECK(tls_config_set_key_mem(tcnf, keyf, nkey), != -1);

    const auto ts = tls_server();
    if (!ts) return g_log.error("tls_server() failed");
    DEFER[=] { tls_free(ts); };
    CHECK(tls_configure(ts, tcnf), != -1);

    const auto epfd = CHECK(epoll_create1(0), != -1);
    epoll_event e{.events = EPOLLIN | EPOLLOUT}, es[16];
    const itimerspec its{.it_value = o.timeout};
    CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, acfd, &e), != -1);
    for (;;) {
        int i = call_while(L0(epoll_wait(epfd, es, 16, -1), &), L(PNEN_dbg(x == -1, 0))) - 1;
        do {
            if (!es[i].data.ptr) {
                socklen_t nsin = sizeof(sin);
                const auto fd  = CHECK(
                     accept4(acfd, reinterpret_cast<sockaddr *>(&sin), &nsin, SOCK_NONBLOCK), != -1);

                tls *tc;
                CHECK(tls_accept_socket(ts, &tc, fd), != -1);

                auto &p    = on_accept(socket{tc}).p;
                p.tc       = tc;
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
                crhdl::from_address(es[i].data.ptr).resume();
            }
        } while (i--);
    }
}
}

#include "../lmacro_end.h"
