#pragma once

#include "buffer.h"
#include "message.h"

enum class http_status {
    unauth    = 401,
    notfound  = 404,
    forbidden = 403,
    internal  = 500,
    badreq    = 400,
    badver    = 505,
    badrg     = 416,
    ok        = 200
};

http_status custom_serve(pnen::buffer &b, pnen::message &rq);
