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
#include <netinet/in.h>
#include <numeric>
#include <ranges>
#include <span>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "../jutil.h"

#include "../lmacro_begin.h"

namespace pnen::detail
{
using namespace jutil;

#define PNEN_push_diag(x)     _Pragma("GCC diagnostic push") x
#define PNEN_pop_diag()       _Pragma("GCC diagnostic pop")

#define PNEN_wno_unused_value _Pragma("GCC diagnostic ignored \"-Wunused-value\"")

//[[noreturn]] void error(const char *msg)
//{
//    perror(msg);
//    exit(EXIT_FAILURE);
//}

#if !defined(NDEBUG) && !defined(__INTELLISENSE__)
#define PNEN_assume(E) CHECK(E)
#define PNEN_dbg(T, F) T
#else
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
#define FOR_CO_AWAIT_bind_exp(x, ...) __VA_OPT__([) x __VA_OPT__(,) __VA_ARGS__ __VA_OPT__(])
#define FOR_CO_AWAIT_impl(x, y, ...)                                                               \
    if (auto BOOST_PP_CAT(fca_it, __LINE__) =                                                      \
            BOOST_PP_VARIADIC_ELEM(BOOST_PP_VARIADIC_SIZE(__VA_ARGS__), y, __VA_ARGS__);           \
        0) {                                                                                       \
    } else if (bool BOOST_PP_CAT(fca_ok, __LINE__) =                                               \
                   co_await ::pnen::detail::has_first(BOOST_PP_CAT(fca_it, __LINE__));             \
               !BOOST_PP_CAT(fca_ok, __LINE__)) {                                                  \
    } else                                                                                         \
        for (auto &&BOOST_PP_CAT(fca_var, __LINE__) =                                              \
                 ::pnen::detail::first(BOOST_PP_CAT(fca_it, __LINE__));                            \
             BOOST_PP_CAT(fca_ok, __LINE__);                                                       \
             (BOOST_PP_CAT(fca_ok, __LINE__) =                                                     \
                  co_await BOOST_PP_CAT(fca_it, __LINE__).has_next()) &&                           \
             (BOOST_PP_CAT(fca_var, __LINE__) = BOOST_PP_CAT(fca_it, __LINE__).next(), 0))         \
            if (auto &&FOR_CO_AWAIT_bind(                                                          \
                    BOOST_PP_TUPLE_POP_BACK((x, y __VA_OPT__(, __VA_ARGS__)))) =                   \
                    static_cast<decltype(BOOST_PP_CAT(fca_var, __LINE__)) &&>(                     \
                        BOOST_PP_CAT(fca_var, __LINE__));                                          \
                0) {                                                                               \
            } else
#define FOR_CO_AWAIT_dummy(...)                                                                    \
    FOR_CO_AWAIT_impl(BOOST_PP_CAT(fca_dummy, __LINE__), __VA_ARGS__)                              \
        PNEN_push_diag(PNEN_wno_unused_value) if ((BOOST_PP_CAT(fca_dummy, __LINE__), 0))          \
            PNEN_pop_diag();                                                                       \
    else
#define FOR_CO_AWAIT(x, ...)                                                                       \
    BOOST_PP_IF(BOOST_PP_CHECK_EMPTY(__VA_ARGS__), FOR_CO_AWAIT_dummy, FOR_CO_AWAIT_impl)          \
    (x, __VA_ARGS__)

struct read_state {
    char *buf;
    char *bufspn;
    size_t nbufspn;
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
        constexpr JUTIL_INLINE task get_return_object() & { return {*this}; }
        constexpr JUTIL_INLINE std::suspend_always initial_suspend() { return {}; }
        constexpr JUTIL_INLINE std::suspend_never final_suspend() noexcept { return {}; }
        constexpr JUTIL_INLINE void return_void() {}
        constexpr JUTIL_INLINE void unhandled_exception() {}
    };
    promise_type &p;
};

struct socket {
    const int fd;

    //
    // read
    //

  private:
    struct read_res_base : read_state {
        [[nodiscard]] JUTIL_INLINE bool has_next() noexcept { return true; }
    };
    struct has_next_awaitable {
        read_state &rs;
        JUTIL_INLINE bool await_ready() noexcept { return false; }
        JUTIL_INLINE void await_suspend(std::coroutine_handle<task::promise_type> h) noexcept
        {
            h.promise().rs = &rs;
        }
        JUTIL_INLINE bool await_resume() noexcept { return true; }
    };
    struct read_res : read_state {
        JUTIL_INLINE has_next_awaitable has_next() noexcept { return {*this}; }
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
    JUTIL_INLINE read_res read(char *const buf, const size_t nbuf) const noexcept
    {
        return {{.buf = buf, .bufspn = buf, .nbufspn = nbuf}};
    }

    //
    // write
    //

    JUTIL_INLINE ssize_t write(const void *const buf, size_t nbuf) const noexcept
    {
        return CHECK(::write(fd, buf, nbuf), != -1);
    }
    JUTIL_INLINE ssize_t write(const std::string_view buf) const noexcept
    {
        return write(buf.data(), buf.size());
    }
};

struct run_server_options {
    uint16_t hostport;
    timespec timeout;
};

template <class F, class... Args>
using promisety = typename std::coroutine_traits<call_result<F, Args...>, Args...>::promise_type;
template <class F, class... Args>
using crhdlty = typename std::coroutine_handle<typename promisety<F, Args...>::promise_type>;

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
                auto &p    = on_accept(socket{fd}).p;
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
                auto h             = crhdl::from_address(es[i].data.ptr);
                auto &[rs, sfd, _] = h.promise();
                if (const auto nread = CHECK(read(sfd, rs->bufspn, rs->nbufspn), != -1)) {
                    rs->bufspn += nread;
                    rs->nbufspn -= nread;
                    h.resume();
                } else
                    h.destroy();
            }
        } while (i--);
    }
}
}

#include "../lmacro_end.h"