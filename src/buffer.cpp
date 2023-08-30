#include "buffer.h"

namespace pnen
{
template <class ChTy, std::size_t DefCap, bool View>
template <bool Copy>
void basic_buffer<ChTy, DefCap, View>::grow(std::size_t n)
{
    const auto new_cap = std::bit_ceil(n);
    auto new_mem       = std::make_unique_for_overwrite<char[]>(new_cap);
    if constexpr (Copy) std::copy_n(s_.buf.get(), s_.n, new_mem.get());
    s_.buf.reset(new_mem.release());
    s_.cap = new_cap;
}
template void buffer::grow<false>(std::size_t);
template void buffer::grow<true>(std::size_t);
} // namespace pnen
