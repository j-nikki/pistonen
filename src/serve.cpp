#include "server.h"

#include <chrono>
#include <jutil/b64.h>
#include <jutil/match.h>
#include <magic_enum.hpp>
#include <ranges>
#include <unordered_map>

#if PNEN_TRANSCODE
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
}
#endif

#ifdef TIMESTAMP_DIR
#include <filesystem>
#endif

#include "custom_serve.h"
#include "message.h"

#include <jutil/lmacro.inl>

namespace sc = std::chrono;
namespace sf = std::filesystem;
namespace sr = std::ranges;
namespace sv = sr::views;

enum class mime { js, json, css, html, png, jpg, webm, mp4, gif, unknown };
static constexpr std::string_view exts[]{".js",  ".json", ".css", ".html", ".png",
                                         ".jpg", ".webm", "mp4",  ".gif"};
static constexpr std::string_view mt_ss[]{"text/javascript; charset=utf-8",
                                          "application/json",
                                          "text/css; charset=utf-8",
                                          "text/html; charset=utf-8",
                                          "image/png",
                                          "image/jpeg",
                                          "video/webm",
                                          "video/mp4",
                                          "image/gif"};

[[nodiscard]] JUTIL_INLINE const std::string_view &mime_to_string(mime mt) noexcept
{
    return mt_ss[CHECK(std::to_underlying(mt), >= 0, < std::to_underlying(mime::unknown))];
}

[[nodiscard]] JUTIL_INLINE mime get_mime(const std::string_view uri) noexcept
{
    return static_cast<mime>(jutil::find_if_unrl_idx(exts, L(uri.ends_with(x), &)));
}

template <std::integral Size, jutil::callable<char *, std::size_t> F>
constexpr format::custom_formatable auto lazywrite(Size n_, F &&f_)
{
    struct R {
        Size n;
        F f;
        constexpr std::size_t size() const noexcept { return static_cast<std::size_t>(n); }
        constexpr char *write(char *p) const noexcept { return f(p, n), p + n; }
    };
    return R{n_, static_cast<F &&>(f_)};
}

// TODO: remove creds from source code
std::unordered_map<std::string_view, std::string_view> g_creds{{"admin", "admin123"}};

[[nodiscard]] bool check_creds(const std::string_view &uname, const std::string_view &pass) noexcept
{
    const auto it = g_creds.find(uname);
    const auto ok = it != g_creds.end() && it->second == pass;
    // g_log.debug("checking creds: UNAME=", uname, "; PASS=", pass, "; OK=", ok);
    return ok;
}

[[nodiscard]] bool check_auth(const std::string_view sv) noexcept
{
    if (sv.starts_with("Basic ")) {
        char buf[256];
        auto dit       = jutil::b64_decode(sv.substr(6), buf);
        const auto col = sr::find(buf, dit, ':');
        while (dit != col && !dit[-1])
            --dit;
        return check_creds({buf, col}, {col + 1, dit});
    }

    // TODO: support more auth types
    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Authentication

    return false;
}

// DBGSTMT(static int ncon = 0;)

// #undef PNEN_KEEP_ALIVE

#if PNEN_KEEP_ALIVE
#define SERVE_FOREVER_BEGIN()                                                                      \
    for (;;) {                                                                                     \
        (void)0
#define SERVE_FOREVER_END(Sock, Buf, NBuf)                                                         \
    do {                                                                                           \
    } while (0);                                                                                   \
    }                                                                                              \
    (void)0
#define KEEP_ALIVE "Connection: keep-alive\r\nKeep-Alive: timeout=" STR(PNEN_KEEP_ALIVE) "\r\n"
#else
#define SERVE_FOREVER_BEGIN()              (void)0
#define SERVE_FOREVER_END(Sock, Buf, NBuf) (void)0
#define KEEP_ALIVE                         "Connection: close\r\n"
#endif

#define DATE "Date: ", format::hdr_time{}, "\r\n"

using namespace jutil::literals;
constexpr std::tuple map_k{
    "$"_js,
    "search"_js,
    "[a-z]+\\.js"_js,
    "[a-z]+\\.css"_js,
};

constexpr std::tuple map_v{
    L2(x.put("front/index.html")),
    L2(x.put("front/index.html")),
    L2(x.put("front/build/", y)),
    L2(x.put("front/", y)),
};

//
// transcode
//

#if PNEN_TRANSCODE
struct StreamContext {
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;

    AVFrame *dec_frame;
};

int open_input_file(const char *filename, AVFormatContext *&ifmt_ctx, StreamContext *&stream_ctx)
{
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;

    AVFrame *dec_frame;

    int ret;
    unsigned int i;

    ifmt_ctx = NULL;
    if ((ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    stream_ctx =
        static_cast<StreamContext *>(av_calloc(ifmt_ctx->nb_streams, sizeof(StreamContext)));
    if (!stream_ctx) return AVERROR(ENOMEM);

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *stream   = ifmt_ctx->streams[i];
        const AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *codec_ctx;
        if (!dec) {
            av_log(NULL, AV_LOG_ERROR, "Failed to find decoder for stream #%u\n", i);
            return AVERROR_DECODER_NOT_FOUND;
        }
        codec_ctx = avcodec_alloc_context3(dec);
        if (!codec_ctx) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u\n",
                   i);
            return AVERROR(ENOMEM);
        }
        ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to copy decoder parameters to input decoder context "
                   "for stream #%u\n",
                   i);
            return ret;
        }

        /* Inform the decoder about the timebase for the packet timestamps.
         * This is highly recommended, but not mandatory. */
        codec_ctx->pkt_timebase = stream->time_base;

        /* Reencode video & audio and remux subtitles etc. */
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
            codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
                codec_ctx->framerate = av_guess_frame_rate(ifmt_ctx, stream, NULL);
            /* Open decoder */
            ret = avcodec_open2(codec_ctx, dec, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }
        stream_ctx[i].dec_ctx   = codec_ctx;

        stream_ctx[i].dec_frame = av_frame_alloc();
        if (!stream_ctx[i].dec_frame) return AVERROR(ENOMEM);
    }

    av_dump_format(ifmt_ctx, 0, filename, 0);
    return 0;
}

int open_output_file(const char *filename, AVFormatContext *ifmt_ctx, AVFormatContext *&ofmt_ctx,
                     StreamContext *stream_ctx)
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    const AVCodec *encoder;
    int ret;
    unsigned int i;

    ofmt_ctx = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename);
    if (!ofmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }

        in_stream = ifmt_ctx->streams[i];
        dec_ctx   = stream_ctx[i].dec_ctx;

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
            dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* in this example, we choose transcoding to same codec */
            encoder = avcodec_find_encoder(dec_ctx->codec_id);
            if (!encoder) {
                av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
                return AVERROR_INVALIDDATA;
            }
            enc_ctx = avcodec_alloc_context3(encoder);
            if (!enc_ctx) {
                av_log(NULL, AV_LOG_FATAL, "Failed to allocate the encoder context\n");
                return AVERROR(ENOMEM);
            }

            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                enc_ctx->height              = dec_ctx->height;
                enc_ctx->width               = dec_ctx->width;
                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                else
                    enc_ctx->pix_fmt = dec_ctx->pix_fmt;
                /* video time_base can be set to whatever is handy and supported by encoder */
                enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
            } else {
                enc_ctx->sample_rate = dec_ctx->sample_rate;
                ret = av_channel_layout_copy(&enc_ctx->ch_layout, &dec_ctx->ch_layout);
                if (ret < 0) return ret;
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base  = (AVRational){1, enc_ctx->sample_rate};
            }

            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(enc_ctx, encoder, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
                return ret;
            }
            ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Failed to copy encoder parameters to output stream #%u\n", i);
                return ret;
            }

            out_stream->time_base = enc_ctx->time_base;
            stream_ctx[i].enc_ctx = enc_ctx;
        } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n",
                   i);
            return AVERROR_INVALIDDATA;
        } else {
            /* if this stream must be remuxed */
            ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Copying parameters for stream #%u failed\n", i);
                return ret;
            }
            out_stream->time_base = in_stream->time_base;
        }
    }
    av_dump_format(ofmt_ctx, 0, filename, 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

    return 0;
}
#endif

//
// server task
//

pnen::handler PNEN_SERVER_TASK(pnen::socket s)
{
    CO_AWAIT_INIT((), s.handshake());

    // Read into buffer
    char buf[8 * 1024 * 1024];
    pnen::message rq;
    pnen::buffer rs;
    SERVE_FOREVER_BEGIN();
    FOR_CO_AWAIT ((b, r), s.read(buf, std::size(buf))) {
        const auto [it, _] = sr::search(b, std::string_view{"\r\n\r\n"});
        if (it == b.end()) {
            if (r.nbufspn == 0) {
                FOR_CO_AWAIT (s.write("431 Request Header Fields Too Large\r\n\r\n"))
                    ;
                co_return;
            }
        } else { // end of header (\r\n\r\n)
            parse_header(buf, &*it, rq);
            break;
        }
    } else
        co_return;

        // Handle request & build response
#ifdef TIMESTAMP_DIR
    const auto nolog = rq.strt.tgt == "/ts";
#else
    static constexpr std::false_type nolog{};
#endif
    // DBGEXPR(!nolog && printf("vvv con#%d: received message with the header:\n", id_));
    // DBGEXPR(!nolog && (print_header(rq), 1));
    // DBGEXPR(!nolog && printf("^^^\n"));
    // TODO: read rq body
    // determining message length (after \r\n\r\n):
    // https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
    if (rq.strt.mtd == pnen::method::err) goto badreq;
    if (rq.strt.ver == pnen::version::err) goto badver;
    if (const auto auth = get<"Authorization", "">(rq.hdrs); !check_auth(auth)) goto unauth;

    !nolog &&g_log.info(magic_enum::enum_name(rq.strt.mtd), " ", rq.strt.tgt);
    switch (rq.strt.mtd) {
    case pnen::method::GET: {
        auto name = rq.strt.tgt.substr(1);
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (... || (get<0>(jutil::match<std::get<Is>(map_k)>(name.begin(), name.end())) &&
                     (std::get<Is>(map_v)(rs, name), name = {rs.data(), rs.size()}, 1)));
        }(std::make_index_sequence<std::tuple_size_v<decltype(map_k)>>{});
        if (get<0>(jutil::match<"[0-9A-Za-z_\\-][0-9A-Za-z_\\-/]+\\.[a-z0-9]+$">(name))) {
            const auto mt = get_mime(name);
            if (mt == mime::unknown) goto default_;
            name[name.size()] = '\0';
            const auto fd     = open(name.data(), O_RDONLY);
            if (fd < 0) goto notfound;
            DEFER[=] { close(fd); };
            struct stat st;
            if (fstat(fd, &st) != 0) goto internal;
            if (!S_ISREG(st.st_mode)) goto forbidden;
            static constexpr auto nblk = 4z * 1024z * 1024z;
            std::size_t off;
            size_t n;
            if (auto rg = get<"Range">(rq.hdrs)) {
                const auto m = jutil::match<"bytes=%u?-%u?$">(rg->begin(), rg->end());
                if (!get<0>(m)) goto badrg;
                const auto a = get<1>(m).value_or(0), b = get<2>(m).value_or(st.st_size - 1);
                if (a > b || b - a + 1 > st.st_size) goto badreq;
                off = a;
                n   = std::min(b - a + 1z, nblk);
#if PNEN_TRANSCODE
                if (mt == mime::webm || mt == mime::mp4) {
#define VIDHDRA                                                                                    \
    "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n" DATE "Content-Length:        "
#define VIDHDRB "\r\nContent-Range:                  "
#define VIDHDRC "/*\r\n" KEEP_ALIVE "\r\n"
#define VIDHDR  VIDHDRA VIDHDRB VIDHDRC
#pragma push_macro("ERRFMT")
#pragma push_macro("ERRARGS")
#undef ERRFMT
#undef ERRARGS
                    char averrbuf[2 + AV_ERROR_MAX_STRING_SIZE] = ": ";
#define ERRFMT "%s"
#define ERRARGS(X)                                                                                 \
    , [&] {                                                                                        \
        if constexpr (std::is_same_v<std::decay_t<decltype((X))>, int>)                            \
            return av_make_error_string(averrbuf + 2, AV_ERROR_MAX_STRING_SIZE, X), averrbuf;      \
        else                                                                                       \
            return ": no info";                                                                    \
    }()
                    rs.put(VIDHDR);
                    AVFormatContext *ictx, *octx;
                    StreamContext *sc;
                    CHECK(open_input_file(name.data(), ictx, sc) == 0);
                    CHECK(open_output_file(name.data(), ictx, octx, sc) == 0);
                    CHECK(avformat_open_input(&ictx, name.data(), nullptr, nullptr) == 0);
                    DEFER[&] { avformat_close_input(&ictx); };
                    CHECK(avformat_find_stream_info(ictx, nullptr) >= 0);
                    const AVCodec *vc, *ac;
                    const auto ivs =
                        CHECK(av_find_best_stream(ictx, AVMEDIA_TYPE_VIDEO, -1, -1, &vc, 0), >= 0);
                    const auto ias =
                        CHECK(av_find_best_stream(ictx, AVMEDIA_TYPE_AUDIO, -1, -1, &ac, 0), >= 0);
                    const auto vs = ictx->streams[ivs];
                    // TODO: implement
                }
#pragma pop_macro("ERRFMT")
#pragma pop_macro("ERRARGS")
#endif
                rs.put("HTTP/1.1 206 Partial Content\r\nContent-Type: ", mime_to_string(mt),
                       "\r\n" DATE "Content-Length: ", n, "\r\nContent-Range: bytes ", a, "-",
                       (a + n - 1), "/", st.st_size, "\r\n" KEEP_ALIVE "\r\n");
            } else {
                rs.put(
                    "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nContent-Type: ", mime_to_string(mt),
                    "\r\n" DATE "Content-Length: ", st.st_size, "\r\n" KEEP_ALIVE "\r\n");
                off = 0;
                n   = st.st_size;
            }
            pnen::cork c{s};
            FOR_CO_AWAIT ((), s.write(rs.data(), rs.size()))
                ;
            else
                goto internal;
            FOR_CO_AWAIT ((), s.sendfile(fd, off, n))
                ;
            goto end;
        }
#ifdef TIMESTAMP_DIR
        else if (name == "ts") {
            char *it = buf;
            *it++    = '{';
            for (const auto &e : sf::recursive_directory_iterator{TIMESTAMP_DIR} |
                                     sv::filter(L(x.is_regular_file()))) {
                const auto time = e.last_write_time();
                const auto ts   = sc::duration_cast<sc::milliseconds>(time.time_since_epoch());
                it              = std::format_to(it, "\"{}\": {},", e.path().c_str(), ts.count());
            }
            it[-1] = '}';
            rs.put("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n" DATE KEEP_ALIVE
                   "Content-Length: ",
                   it - buf, "\r\n\r\n", buf);
            break;
        }
    }
#endif
        [[fallthrough]];
    default_:
    default: {
        switch (custom_serve(rs, rq)) {
            // clang-format off
        case http_status::unauth: unauth: rs.put("HTTP/1.1 401 Unauthorized\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 3\r\nWWW-Authenticate: Basic\r\n\r\n401"); break;
        case http_status::notfound: notfound: rs.put("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 3\r\n\r\n404"); break;
        case http_status::forbidden: forbidden: rs.put("HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 3\r\n\r\n403"); break;
        case http_status::internal: internal: rs.put("HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 3\r\n\r\n500"); break;
        case http_status::badreq: badreq: rs.put("HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 3\r\n\r\n400"); break;
        case http_status::badver: badver: rs.put("HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 3\r\n\r\n505"); break;
        case http_status::badrg: badrg: rs.put("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 3\r\n\r\n416"); break;
        case http_status::ok: break;
            // clang-format on
            JUTIL_NO_DEFAULT();
        }
    }
    }
    FOR_CO_AWAIT ((), s.write(rs.data(), rs.size()))
        ;
end:
    SERVE_FOREVER_END(s, buf, nbuf);
}

#include <jutil/lmacro.inl>
