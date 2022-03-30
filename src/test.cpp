#include <range/v3/view/drop.hpp>
#include <range/v3/view/enumerate.hpp>
#include <ranges>
#include <unordered_map>
#include <vector>

#include "jutil.h"

#include "lmacro_begin.h"

#include "pistonen.h"

namespace sr = std::ranges;
namespace sv = std::views;
namespace rv = ranges::view;

int main(const int argc, char *const argv[])
{
    using SV = std::string_view;
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port-number>\n", argv[0]);
        return 1;
    }
    const pnen::run_server_options o{.hostport = static_cast<uint16_t>(atoi(argv[1])),
                                     .timeout  = {.tv_sec = 3}};
    pnen::run_server(o, [](pnen::socket s) -> pnen::task {
        static constexpr size_t nbuffer = 8 * 1024 * 1024;
        char buffer[nbuffer];
        FOR_CO_AWAIT (buf, rs, s.read(buffer, nbuffer)) {
            const auto [it, _] = sr::search(buf, SV{"\r\n\r\n"});
            if (it == buf.end()) {
                if (rs.nbufspn == 0) {
                    s.write("431 Request Header Fields Too Large\r\n\r\n");
                    co_return;
                }
            } else { // end of header (CRLFCRLF)
                const auto frstcr = buf.find("\r");
                for (const auto ln : sv::split(buf, SV{"\r\n"}) |
                                         sv::transform(L(SV(&*x.begin(), sr::distance(x))))) {
                    printf("'%.*s'\n", static_cast<int>(ln.size()), ln.data());
                    const auto colon = sr::find(ln, ':');
                    if (colon == ln.end())
                        continue;
                    // TODO: parse key:<values...>
                }

                // TODO: maybe read body
                // determining message length (after CRLFCRLF):
                // https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4

                // TODO: write response
                s.write("HTTP/1.1 200 OK\r\ncontent-type: "
                        "text/html;charset=UTF-8\r\ncontent-length: 75\r\n\r\n<!DOCTYPE "
                        "html><metacharset=utf-8><title>my site</title><p>this is my site");
                co_return;
            }
        }
    });
}