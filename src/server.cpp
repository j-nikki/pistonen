#include "server.h"

#include <coroutine>
#include <jutil/alg.h>
#include <jutil/b64.h>
#include <jutil/bit.h>
#include <jutil/core.h>
#include <jutil/macro.h>
#include <jutil/match.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "buffer.h"
#include "format.h"
#include "message.h"
#include "pistonen.h"
#include "vocabserv.h"
#include <res.h>

#define CERT_IMPL
#include <cert.inl>

#include <jutil/lmacro.inl>

namespace pnen
{
#ifdef PNEN_SERVER_TASK
void run_server(const run_server_options &o)
{
#define PNEN_stsk PNEN_SERVER_TASK
#else
void run_server(const run_server_options &o, task (*on_accept)(socket))
{
#define PNEN_stsk on_accept
#endif
    struct sigaction sact {};
    sact.sa_handler = SIG_IGN;
    CHECKZ(sigaction(SIGPIPE, &sact, nullptr));

    const auto acfd = CHECK(::socket(AF_INET, SOCK_STREAM, 0), != -1);
    const int flag  = 1;
    CHECK(setsockopt(acfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)), != -1);
    sockaddr_in sin{.sin_family = AF_INET, .sin_port = htons(o.port), .sin_addr{INADDR_ANY}};
    CHECK(bind(acfd, reinterpret_cast<const sockaddr *>(&sin), sizeof(sin)), != -1);
    CHECK(listen(acfd, 128), != -1);

#if PNEN_SSL
    const auto wm = CHECK(wolfSSLv23_server_method());
    const auto wc = CHECK(wolfSSL_CTX_new(wm));
    DEFER[=] { wolfSSL_CTX_free(wc); };
    const auto u8b = (const uint8_t *)cert::buf.data();
    CHECK(wolfSSL_CTX_use_PrivateKey_buffer(wc, u8b, (long)cert::bcert, SSL_FILETYPE_PEM),
          == SSL_SUCCESS);
    CHECK(wolfSSL_CTX_use_certificate_buffer(
              wc, u8b + cert::bcert, (long)(cert::buf.size() - cert::bcert), SSL_FILETYPE_PEM),
          == SSL_SUCCESS);
#define PNEN_rs_s_as wc, fd
#else
#define PNEN_rs_s_as fd
#endif

    const auto efd = CHECK(epoll_create1(0), != -1);
    epoll_event e{.events = EPOLLIN}, es[1];
    const itimerspec its{.it_value = o.timeout};
    CHECKU(epoll_ctl(efd, EPOLL_CTL_ADD, acfd, &e));
    e.events = 0; // EPOLLIN | EPOLLET | EPOLLONESHOT;
    for (;;) {
        using jutil::call_while;
        int i = call_while(L0(epoll_wait(efd, es, std::size(es), -1), &), L(x <= 0)) - 1;
        do {
            if (!es[i].data.ptr) {
                socklen_t nsin = sizeof(sin);
                const auto fd =
                    CHECKU(accept4(acfd, reinterpret_cast<sockaddr *>(&sin), &nsin, SOCK_NONBLOCK));
                // int val = 1;
                // CHECKU(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)));
                g_log.debug("accept fd=", fd);

                auto &p    = PNEN_stsk(pnen::socket{PNEN_rs_s_as}).p;
                p.efd      = efd;
                p.sfd      = fd;
                auto h     = crhdl::from_promise(p);
                e.data.ptr = h.address();
                CHECKU(epoll_ctl(efd, EPOLL_CTL_ADD, fd, &e));

                p.tfd = CHECK(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC), -1);
                // CHECK(timerfd_settime(p.tfd, 0, &its, nullptr), -1);
                JUTIL_PUSH_DIAG(JUTIL_WNO_PARENTHESES)
                e.data.ptr = jutil::to_ptr(CHECK(jutil::to_uint(e.data.ptr), &1 ^ 1) ^ 1);
                JUTIL_POP_DIAG()
                CHECKU(epoll_ctl(efd, EPOLL_CTL_ADD, p.tfd, &e));

                h.resume();
            } else if (const auto iptr = jutil::to_uint(es[i].data.ptr); iptr & 1) {
                crhdl::from_address(jutil::to_ptr(iptr ^ 1)).destroy();
            } else [[likely]] {
                crhdl::from_address(es[i].data.ptr).resume();
            }
        } while (i--);
    }
}
} // namespace pnen
