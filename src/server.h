#pragma once

#include <algorithm>
#include <concepts>
#include <coroutine>
#include <experimental/memory>
#include <fcntl.h>
#include <jutil/core.h>
#include <jutil/data.h>
#include <jutil/macro.h>
#include <magic_enum.hpp>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <numeric>
#include <ranges>
#include <signal.h>
#include <source_location>
#include <span>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#if PNEN_SSL
#include <linux/tls.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#endif

#include <cert.inl>

#include "format"
#include "pistonen.h"
#include "vocabserv.h"

namespace pnen
{
enum class loop_state {
    suspend,   // spurious wake -> resuspend
    error,     // got error -> go to else-block
    has_next,  // next() available -> enter loop
    exhausted, // exhausted data -> exit loop
};
template <class T>
concept loop_state_descriptor = requires(T &x) { x == loop_state{}; };
template <class T>
concept for_co_await_iterable = requires(T &x) {
    {
        x.state()
    } -> loop_state_descriptor;
    x.suspend(x.state());
};
namespace detail
{
// constexpr std::tuple ls_fx{
//     format::fg("suspend", format::bright_yellow), format::fg("error", format::bright_red),
//     format::fg("has_next", format::bright_green), format::fg("exhausted", format::bright_cyan)};
using namespace jutil::literals;
constexpr std::array ls_fx{format::string<format::fg("suspend  "_s, format::bright_yellow)>,
                           format::string<format::fg("error    "_s, format::bright_magenta)>,
                           format::string<format::fg("has_next "_s, format::bright_green)>,
                           format::string<format::fg("exhausted"_s, format::bright_cyan)>};
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
// struct always_suspend : std::suspend_never {
//     JUTIL_CI loop_state await_resume() const noexcept { return loop_state::suspend; }
// };
JUTIL_CI auto first_state(auto &&x) noexcept(noexcept(x.state())) -> decltype(x.state())
{
    return x.state();
}
JUTIL_CI auto first_state(has_first_state auto &&x) noexcept(noexcept(x.first_state()))
    -> decltype(x.state())
{
    return x.first_state();
}

#define FOR_CO_AWAIT_bind(...)     FOR_CO_AWAIT_bind_exp##__VA_OPT__(1)(__VA_ARGS__)
#define FOR_CO_AWAIT_bind_exp(...) [[maybe_unused]] auto &&CAT(fca_dummy, __LINE__)
#define FOR_CO_AWAIT_bind_exp1(x, ...) auto &&__VA_OPT__([) x __VA_OPT__(,) __VA_ARGS__ __VA_OPT__(])

#define FOR_CO_AWAIT_impl(xs, ...)                                                                 \
    if (::pnen::for_co_await_iterable auto CAT(fca_it, __LINE__) = __VA_ARGS__; 0) {               \
    } else if (::pnen::loop_state_descriptor auto CAT(fca_s, __LINE__) =                           \
                   ::pnen::detail::first_state(CAT(fca_it, __LINE__));                             \
               0) {                                                                                \
    } else                                                                                         \
        for (bool CAT(fca_err, __LINE__) = false;                                                  \
                                                                                                   \
             !CAT(fca_err, __LINE__) && CAT(fca_s, __LINE__) != ::pnen::loop_state::exhausted;     \
             !(CAT(fca_err, __LINE__) = CAT(fca_s, __LINE__) == ::pnen::loop_state::error) &&      \
             (CAT(fca_s, __LINE__) = CAT(fca_it, __LINE__).state(), 0))                            \
            if (g_log.debug(__FILE__                                                               \
                            ":" STR(__LINE__) ": " /* jutil::string{STR(__FILE__)}, ":",           \
                                                             jutil::string{STR(__LINE__)}, ": ",*/ \
                            STR(FST(__VA_ARGS__)) " -> ",                                          \
                            ::pnen::detail::ls_fx[std::to_underlying(                              \
                                static_cast<::pnen::loop_state>(CAT(fca_s, __LINE__)))]),          \
                CAT(fca_s, __LINE__) == ::pnen::loop_state::suspend) {                             \
                co_await decltype(CAT(fca_it, __LINE__))::suspend(CAT(fca_s, __LINE__));           \
            } else                                                                                 \
                JUTIL_PUSH_DIAG(JUTIL_WNO_DANGLING_ELSE)                                           \
    if (CAT(fca_s, __LINE__) == ::pnen::loop_state::has_next)                                      \
        if (FOR_CO_AWAIT_bind xs = ::pnen::detail::next(CAT(fca_it, __LINE__)); 0) {               \
        } else                                                                                     \
            JUTIL_POP_DIAG()
#define FOR_CO_AWAIT_(...) FOR_CO_AWAIT_impl((), __VA_ARGS__)
} // namespace detail
#define FOR_CO_AWAIT(x, ...) FOR_CO_AWAIT_##__VA_OPT__(impl)(x __VA_OPT__(, __VA_ARGS__))

#define CO_AWAIT_INIT_impl(xs, ...)                                                                \
    using CAT(caiT, __LINE__) = decltype((__VA_ARGS__).next());                                    \
    std::aligned_storage_t<sizeof(CAT(caiT, __LINE__)), alignof(CAT(caiT, __LINE__))> CAT(         \
        cai_buf, __LINE__);                                                                        \
    FOR_CO_AWAIT ((CAT(cai_tmp, __LINE__)), __VA_ARGS__) {                                         \
        new (&CAT(cai_buf, __LINE__)) CAT(caiT, __LINE__){std::move(CAT(cai_tmp, __LINE__))};      \
        break;                                                                                     \
    } else                                                                                         \
        co_return;                                                                                 \
    FOR_CO_AWAIT_bind xs = *reinterpret_cast<CAT(caiT, __LINE__) *>(&CAT(cai_buf, __LINE__));      \
    DEFER[p = reinterpret_cast<CAT(caiT, __LINE__) *>(&CAT(cai_buf, __LINE__))]                    \
    {                                                                                              \
        std::destroy_at(p);                                                                        \
    };
#define CO_AWAIT_INIT(...) CO_AWAIT_INIT_impl(__VA_ARGS__)

template <class... Ts>
struct task {
    JUTIL_PUSH_DIAG(JUTIL_WNO_SUBOBJ_LINKAGE)
    struct promise_type {
        static constexpr auto is_void = sizeof...(Ts) == 0;
        using val_t                   = jutil::tuple<Ts...>;
        val_t val;
        int efd, sfd, tfd;
        promise_type()                                = default;
        promise_type(const promise_type &)            = delete;
        promise_type(promise_type &&)                 = delete;
        promise_type &operator=(const promise_type &) = delete;
        promise_type &operator=(promise_type &&)      = delete;
        ~promise_type()
        {
            CHECKU(epoll_ctl(efd, EPOLL_CTL_DEL, sfd, nullptr));
            CHECKU(epoll_ctl(efd, EPOLL_CTL_DEL, tfd, nullptr));
            CHECKU(close(sfd));
            CHECKU(close(tfd));
        }
        constexpr JUTIL_INLINE task get_return_object() & { return {*this}; }
        constexpr JUTIL_INLINE std::suspend_always initial_suspend() { return {}; }
        constexpr JUTIL_INLINE std::suspend_never final_suspend() noexcept { return {}; }
        constexpr JUTIL_INLINE void return_void()
            requires(is_void)
        {
        }
        // constexpr JUTIL_INLINE void return_value(val_t &&v)
        //     requires(!is_void)
        // {
        //     val = static_cast<val_t &&>(v);
        // }
        // constexpr JUTIL_INLINE void return_value(const val_t &v)
        //     requires(!is_void)
        // {
        //     val = v;
        // }
        constexpr JUTIL_INLINE void unhandled_exception() {}
    };
    JUTIL_POP_DIAG()
    promise_type &p;
};

using handler = task<>;

// template <>
// struct task<void> {
//     JUTIL_PUSH_DIAG(JUTIL_WNO_SUBOBJ_LINKAGE)
//     struct promise_type {
//         int efd, sfd, tfd;
//         promise_type()                                = default;
//         promise_type(const promise_type &)            = delete;
//         promise_type(promise_type &&)                 = delete;
//         promise_type &operator=(const promise_type &) = delete;
//         promise_type &operator=(promise_type &&)      = delete;
//         ~promise_type()
//         {
//             CHECKU(epoll_ctl(efd, EPOLL_CTL_DEL, sfd, nullptr));
//             CHECKU(epoll_ctl(efd, EPOLL_CTL_DEL, tfd, nullptr));
//             CHECKU(close(sfd));
//             CHECKU(close(tfd));
//         }
//         constexpr JUTIL_INLINE task get_return_object() & { return {*this}; }
//         constexpr JUTIL_INLINE std::suspend_always initial_suspend() { return {}; }
//         constexpr JUTIL_INLINE std::suspend_never final_suspend() noexcept { return {}; }
//         constexpr JUTIL_INLINE void return_void() {}
//         constexpr JUTIL_INLINE void unhandled_exception() {}
//     };
//     JUTIL_POP_DIAG()
//     promise_type &p;
// };

// template <class F, class... Args>
// using promisety =
//     typename std::coroutine_traits<jutil::call_result<F, Args...>, Args...>::promise_type;
// template <class F, class... Args>
// using crhdlty = typename std::coroutine_handle<typename promisety<F, Args...>::promise_type>;
// using crhdl = crhdlty<task (*)(socket), socket>;
using crhdl   = std::coroutine_handle<handler::promise_type>;

namespace detail
{
JUTIL_INLINE void set_events(crhdl h, uint32_t events) noexcept
{
    epoll_event e{.events = events | EPOLLET | EPOLLONESHOT, .data = {.ptr = h.address()}};
    CHECKU(epoll_ctl(h.promise().efd, EPOLL_CTL_MOD, h.promise().sfd, &e));
}
struct suspend_t {
    uint32_t events;
    crhdl h_;
    [[nodiscard]] JUTIL_INLINE bool await_ready() const noexcept { return false; }
    void await_suspend(crhdl h) noexcept
    {
        // if (events != EPOLLIN) set_events(h_ = h, events);
        set_events(h_ = h, events);
    }
    void await_resume() noexcept
    {
        // if (events != EPOLLIN) set_events(h_, EPOLLIN);
    }
    ~suspend_t() noexcept
    {
        // if (events != EPOLLIN) set_events(h_, EPOLLIN);
        // set_events(h_, EPOLLIN);
    }
};
} // namespace detail

struct socket {
#if PNEN_SSL
    WOLFSSL_CTX *wc;
#endif
    int fd;

    //
    // handshake
    //

  private:
#if PNEN_SSL
    JUTIL_PUSH_DIAG(JUTIL_WNO_SUBOBJ_LINKAGE)
    struct handshake_res {
        int fd;
        std::unique_ptr<WOLFSSL, decltype([](auto p) { wolfSSL_free(p); })> ws;
        struct state_ty {
            loop_state ls;
            uint32_t e;
            JUTIL_CI operator loop_state() const noexcept { return ls; }
        };
        static JUTIL_INLINE detail::suspend_t suspend(state_ty s) noexcept { return {s.e}; }
        JUTIL_INLINE state_ty state() noexcept
        {
            const auto ret = wolfSSL_accept(ws.get());
            if (ret == WOLFSSL_SUCCESS) {
                const auto nkey = wolfSSL_GetKeySize(ws.get());
                const auto err  = CHECK(setsockopt(fd, SOL_TCP, TCP_ULP, "tls", sizeof("tls")),
                                        != -1 || errno == ENOTCONN);
                if (err) return {.ls = loop_state::error};

                tls12_crypto_info_aes_gcm_128 ci;
                ci.info.version     = TLS_1_3_VERSION;
                ci.info.cipher_type = TLS_CIPHER_AES_GCM_128;

                uint64_t seq;
                wolfSSL_GetSequenceNumber(ws.get(), &seq);
                seq      = htobe64(seq);

                auto key = (wolfSSL_GetSide(ws.get()) == WOLFSSL_CLIENT_END)
                               ? wolfSSL_GetClientWriteKey(ws.get())
                               : wolfSSL_GetServerWriteKey(ws.get());
                auto iv  = (wolfSSL_GetSide(ws.get()) == WOLFSSL_CLIENT_END)
                               ? wolfSSL_GetClientWriteIV(ws.get())
                               : wolfSSL_GetServerWriteIV(ws.get());

                memcpy(ci.key, key, nkey);
                memcpy(ci.salt, iv, 4);
                memcpy(ci.iv, (iv + 4), 8);
                memcpy(ci.rec_seq, &seq, sizeof(seq));
                CHECKU(setsockopt(fd, SOL_TLS, TLS_TX, &ci, sizeof(ci)));

                key = (wolfSSL_GetSide(ws.get()) == WOLFSSL_CLIENT_END)
                          ? wolfSSL_GetServerWriteKey(ws.get())
                          : wolfSSL_GetClientWriteKey(ws.get());
                iv  = (wolfSSL_GetSide(ws.get()) == WOLFSSL_CLIENT_END)
                          ? wolfSSL_GetServerWriteIV(ws.get())
                          : wolfSSL_GetClientWriteIV(ws.get());
                memcpy(ci.key, key, nkey);
                memcpy(ci.salt, iv, 4);
                memcpy(ci.iv, (iv + 4), 8);
                memcpy(ci.rec_seq, &seq, sizeof(seq));
                CHECKU(setsockopt(fd, SOL_TLS, TLS_RX, &ci, sizeof(ci)));

                return {.ls = loop_state::has_next};
            }
            switch (const auto err = wolfSSL_get_error(ws.get(), ret); err) {
            case SSL_ERROR_WANT_READ: return {.ls = loop_state::suspend, .e = EPOLLIN};
            case SSL_ERROR_WANT_WRITE: return {.ls = loop_state::suspend, .e = EPOLLOUT};
            default: return {.ls = loop_state::error};
            }
        }
        JUTIL_INLINE auto next() noexcept { return std::move(ws); }
    };
    JUTIL_POP_DIAG()

  public:
    [[nodiscard]] JUTIL_INLINE handshake_res handshake() noexcept
    {
        const auto ws = CHECK(wolfSSL_new(wc));
        CHECK(wolfSSL_set_cipher_list(ws, "TLS_AES_128_GCM_SHA256"), == SSL_SUCCESS); // TODO: more
        CHECK(wolfSSL_set_fd(ws, fd), == SSL_SUCCESS);
        return {.fd = fd, .ws = {ws, {}}};
    }
#endif

    //
    // read
    //

  private:
    struct read_res {
        int fd;
        char *buf;
        char *bufspn;
        size_t nbufspn;
        static JUTIL_INLINE detail::suspend_t suspend(auto) noexcept { return {EPOLLIN}; }
        JUTIL_INLINE loop_state state() noexcept
        {
            const auto ret = CHECK(::read(fd, bufspn, nbufspn),
                                   != -1 || errno == EAGAIN || errno == EIO || errno == ECONNRESET);
            if (ret == 0) return loop_state::error;
            if (ret != -1) {
                bufspn += ret;
                nbufspn -= ret;
                return loop_state::has_next;
            }
            return errno == EAGAIN ? loop_state::suspend : loop_state::error;
        }
        JUTIL_INLINE std::tuple<std::span<char>, read_res &> next() noexcept
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
        return {.fd = fd, .buf = buf, .bufspn = buf, .nbufspn = nbuf};
    }

    //
    // write
    //

  private:
    struct write_res {
        int fd;
        const char *buf;
        size_t nbuf;
        static JUTIL_INLINE detail::suspend_t suspend(auto) noexcept { return {EPOLLOUT}; }
        JUTIL_INLINE loop_state state() noexcept
        {
            const auto ret = ::write(fd, buf, nbuf);
            if (ret == -1) return errno == EAGAIN ? loop_state::suspend : loop_state::error;
            buf += ret;
            nbuf -= ret;
            return nbuf ? loop_state::has_next : loop_state::exhausted;
        }
    };

  public:
    [[nodiscard]] JUTIL_INLINE write_res write(const char *const buf, size_t nbuf) noexcept
    {
        return {.fd = fd, .buf = buf, .nbuf = nbuf};
    }
    [[nodiscard]] JUTIL_INLINE write_res write(const std::string_view buf) noexcept
    {
        return write(buf.data(), buf.size());
    }

    //
    // sendfile
    //

  private:
    struct sendfile_res {
        int fd;
        int infd;
        off_t bbuf;
        size_t nbuf;
        static JUTIL_INLINE detail::suspend_t suspend(auto) noexcept { return {EPOLLOUT}; }
        JUTIL_INLINE loop_state state() noexcept
        {
            const auto ret = CHECK(::sendfile(fd, infd, &bbuf, nbuf),
                                   != -1 || errno == EAGAIN || errno == ECONNRESET ||
                                       errno == EPIPE || errno == EBADMSG);
            if (ret == -1) return errno == EAGAIN ? loop_state::suspend : loop_state::error;
            nbuf -= ret;
            return nbuf ? loop_state::has_next : loop_state::exhausted;
        }
    };

  public:
    [[nodiscard]] JUTIL_INLINE sendfile_res sendfile(int infd, off_t offset, size_t count) noexcept
    {
        return {.fd = fd, .infd = infd, .bbuf = offset, .nbuf = count};
    }
};

class cork
{
    int fd_;

  public:
    [[nodiscard]] JUTIL_INLINE cork(socket s) noexcept : fd_{s.fd}
    {
        const int state = 1;
        CHECKU(setsockopt(fd_, IPPROTO_TCP, TCP_CORK, &state, sizeof(state)));
    }
    JUTIL_INLINE ~cork()
    {
        const int state = 0;
        CHECKU(setsockopt(fd_, IPPROTO_TCP, TCP_CORK, &state, sizeof(state)));
    }
};

struct run_server_options {
    const char *host = "0.0.0.0";
    uint16_t port    = 8080;
    timespec timeout = {.tv_sec = 5};
    // const char *ssl_cert = nullptr;
    // const char *ssl_pkey = nullptr;
    // const char *pk_pass  = {};
};
#ifdef PNEN_SERVER_TASK
} // namespace pnen
pnen::handler PNEN_SERVER_TASK(pnen::socket);
namespace pnen
{
void run_server(const run_server_options &o);
#else
void run_server(const run_server_options &o, handler (*on_accept)(socket &&));
#endif
} // namespace pnen
