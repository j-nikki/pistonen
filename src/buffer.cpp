#include "buffer.h"

template <bool Copy>
void buffer::grow(std::size_t n)
{
    const auto new_cap = std::bit_ceil(n);
    auto new_mem       = std::make_unique_for_overwrite<char[]>(new_cap);
    if constexpr (Copy)
        std::copy_n(buf_.get(), n_, new_mem.get());
    buf_.reset(new_mem.release());
    cap_ = new_cap;
}
template void buffer::grow<false>(std::size_t);
template void buffer::grow<true>(std::size_t);