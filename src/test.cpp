#include <algorithm>
#include <array>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/pop_back.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <boost/preprocessor/variadic/size.hpp>
#include <concepts>
#include <coroutine>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <numeric>
#include <ranges>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace sr = std::ranges;

[[noreturn]] void error(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

#define CHECK(E, C)                                                                                \
    []<class BOOST_PP_CAT(T, __LINE__)>(BOOST_PP_CAT(T, __LINE__) &&                               \
                                        BOOST_PP_CAT(x, __LINE__)) -> BOOST_PP_CAT(T, __LINE__) {  \
        if (!(BOOST_PP_CAT(x, __LINE__) C))                                                        \
            error(__FILE__ ":" BOOST_PP_STRINGIZE(__LINE__) ": check failed: " #E " " #C "\n");    \
        return static_cast<BOOST_PP_CAT(T, __LINE__) &&>(BOOST_PP_CAT(x, __LINE__));               \
    }(E)

// clang-format off
template <class T>
concept has_first = requires(T x) {
    x.first();
    { x.has_first() } -> std::same_as<bool>;
};
    // clang-format on

#define FOR_CO_AWAIT_bind(X) FOR_CO_AWAIT_bind_exp X
#define FOR_CO_AWAIT_bind_exp(x, ...) __VA_OPT__([)                                                \
            x __VA_OPT__(,) __VA_ARGS__ __VA_OPT__(])
#define FOR_CO_AWAIT(x, y, ...)                                                                    \
    if (auto BOOST_PP_CAT(fcw_it, __LINE__) =                                                      \
            BOOST_PP_VARIADIC_ELEM(BOOST_PP_VARIADIC_SIZE(__VA_ARGS__), y, __VA_ARGS__);           \
        0) {                                                                                       \
    } else if (bool BOOST_PP_CAT(fcw_ok, __LINE__) =                                               \
                   [&] {                                                                           \
                       if constexpr (has_first<decltype(BOOST_PP_CAT(fcw_it, __LINE__))>)          \
                           return BOOST_PP_CAT(fcw_it, __LINE__).has_first();                      \
                       else                                                                        \
                           return BOOST_PP_CAT(fcw_it, __LINE__).has_next();                       \
                   }();                                                                            \
               !BOOST_PP_CAT(fcw_ok, __LINE__)) {                                                  \
    } else                                                                                         \
        for (auto &&BOOST_PP_CAT(fcw_var, __LINE__) =                                              \
                 co_await [&] {                                                                    \
                     if constexpr (has_first<decltype(BOOST_PP_CAT(fcw_it, __LINE__))>)            \
                         return BOOST_PP_CAT(fcw_it, __LINE__).first();                            \
                     else                                                                          \
                         return BOOST_PP_CAT(fcw_it, __LINE__).next();                             \
                 }();                                                                              \
             BOOST_PP_CAT(fcw_ok, __LINE__);                                                       \
             (BOOST_PP_CAT(fcw_ok, __LINE__) = BOOST_PP_CAT(fcw_it, __LINE__).has_next()) &&       \
             (BOOST_PP_CAT(fcw_var, __LINE__) = co_await BOOST_PP_CAT(fcw_it, __LINE__).next(),    \
                                    0))                                                            \
            if (auto &&FOR_CO_AWAIT_bind(BOOST_PP_TUPLE_POP_BACK((x, y, __VA_ARGS__))) =           \
                    static_cast<decltype(BOOST_PP_CAT(fcw_var, __LINE__)) &&>(                     \
                        BOOST_PP_CAT(fcw_var, __LINE__));                                          \
                0) {                                                                               \
            } else

struct task {
    struct promise_type {
        task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

struct server {
    const int epfd = CHECK(epoll_create1(0), != -1);

    //
    // read
    //

  private:
    template <bool First>
    struct read_res;
    struct read_res_base {
        const int epfd;
        const int fd;
        void *buf;
        size_t count;
    };
    struct read_res_iter : read_res_base {
        [[nodiscard]] inline bool has_first() { return true; }
        [[nodiscard]] inline read_res<true> first() { return {*this}; }
        [[nodiscard]] inline bool has_next() const noexcept { return true; }
        [[nodiscard]] inline read_res<false> next() { return {*this}; }
        ~read_res_iter() { CHECK(epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr), != -1); }
    };
    template <bool First>
    struct read_res {
        read_res_base &b;
        inline bool await_ready() const { return false; }
        inline bool await_suspend([[maybe_unused]] std::coroutine_handle<> h) const noexcept
        {
            if constexpr (First) {
                epoll_event ev{.events = EPOLLIN, .data{.ptr = h.address()}};
                CHECK(epoll_ctl(b.epfd, EPOLL_CTL_ADD, b.fd, &ev), != -1);
            }
            return !First;
        }
        inline std::tuple<ssize_t, void *&, size_t &> await_resume() const noexcept
        {
            return {CHECK(::read(b.fd, b.buf, b.count), != -1), b.buf, b.count};
        }
    };

  public:
    //! @brief Read indefinitely from socket
    //! @param fd File descriptor of the read-from socket
    //! @param buf The start of destination buffer
    //! @param count The maximum amount of bytes to read
    //! @return read_res_iter Await-iterable yielding amount of bytes read
    read_res_iter read(const int fd, void *const buf, const size_t count) const noexcept
    {
        return {epfd, fd, buf, count};
    }
};

struct file {
    int fd;
    constexpr inline operator int() const noexcept { return fd; }
    constexpr inline file(int fd_) : fd{fd_} {}
    constexpr inline file(const file &rhs) = delete;
    constexpr inline file(file &&rhs) : fd{rhs.fd} { rhs.fd = -1; }
    ~file()
    {
        if (fd != -1)
            close(fd);
    }
};

struct run_server_options {
    uint16_t hostport;
    int timeout = -1;
};

void run_server(const run_server_options o, std::invocable<server &, file> auto on_accept)
{
    const auto acfd = CHECK(socket(AF_INET, SOCK_STREAM, 0), != -1);
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(o.hostport), .sin_addr{INADDR_ANY}};
    CHECK(bind(acfd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)), != -1);
    CHECK(listen(acfd, 5), != -1);
    const auto epfd = CHECK(epoll_create1(0), != -1);
    epoll_event ev{.events = EPOLLIN};
    CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, acfd, &ev), != -1);

    for (;;) {
        epoll_event es[16];
        const auto nfds = CHECK(epoll_wait(epfd, es, 16, o.timeout), != -1);
        if (nfds == 0) {
            // TODO: enact timeout
        }
        for (int i = 0; i < nfds; ++i) {
            if (es[i].data.ptr) {
                std::coroutine_handle<>::from_address(es[i].data.ptr).resume();
            } else {
                socklen_t addrlen = sizeof(addr);
                const auto fd     = CHECK(
                        accept(acfd, reinterpret_cast<struct sockaddr *>(&addr), &addrlen), != -1);
                const auto flags = CHECK(fcntl(fd, F_GETFL), != -1);
                CHECK(fcntl(fd, F_SETFL, flags | O_NONBLOCK), != -1);
                server s{epfd};
                on_accept(s, file{fd});
            }
        }
    }
}

int main(const int argc, char *const argv[])
{
    using SV = std::string_view;
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port-number>\n", argv[0]);
    }
    run_server({static_cast<uint16_t>(atoi(argv[1]))}, [](server &s, file fd) -> task {
        static constexpr size_t nbuffer = 8 * 1024 * 1024;
        char buffer[nbuffer];
        FOR_CO_AWAIT (n, buf, nbuf, s.read(fd, buffer, nbuffer)) {
            const SV svbuf{buffer, static_cast<size_t>(n)};
            const auto [it, _] = sr::search(svbuf, SV{"\r\n\r\n"});
            if (it == svbuf.end()) {
                reinterpret_cast<char *&>(buf) += n;
                nbuf -= n;
                if (nbuf == 0)
                    co_return; // TODO: return "431 Request Header Fields Too Large"
            } else {
                printf("received message:\n");
                printf("vvv\n%.*s\n^^^\n", static_cast<int>(n), buffer);
                buf  = buffer;
                nbuf = nbuffer;
                // TODO: read header and respond to it
            }
        }
    });
}