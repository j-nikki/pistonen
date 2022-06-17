#include <benchmark/benchmark.h>
#include <boost/align/aligned_allocator.hpp>
#include <boost/compressed_pair.hpp>
#include <boost/container/allocator.hpp>
#include <boost/container/node_allocator.hpp>
#include <functional>
#include <ranges>

#include "../jutil.h"

namespace table
{
namespace bc = boost::container;
namespace ba = boost::alignment;
namespace sr = std::ranges;

template <class K, class V>
using pair = boost::compressed_pair<K, V>;

template <class K, class V>
using opaque_pair            = jutil::opaque<pair<K, V>>;

static constexpr auto no_cap = static_cast<std::size_t>(-1);

// https://planetmath.org/goodhashtableprimes
static constexpr std::size_t primes[]{
    53,       97,       193,      389,       769,       1543,      3079,      6151,       12289,
    24593,    49157,    98317,    196613,    393241,    786433,    1572869,   3145739,    6291469,
    12582917, 25165843, 50331653, 100663319, 201326611, 402653189, 805306457, 1610612741,
};

consteval std::size_t capidx(const std::size_t cap) noexcept
{
    return static_cast<std::size_t>(sr::lower_bound(primes, cap) - sr::begin(primes));
}

template <std::size_t Cap>
class table_info
{
    static constexpr auto cap = primes[capidx(Cap)];

  public:
    JUTIL_CI table_info() {}
    JUTIL_CI table_info(const table_info &) = default;
    JUTIL_CI std::size_t capacity() const noexcept { return cap; }
};

template <>
class table_info<no_cap>
{
  public:
    consteval table_info(const std::size_t cap = 0) : capidx_{capidx(cap)}, cap_{primes[capidx_]} {}
    JUTIL_CI table_info(const table_info &) = default;
    JUTIL_CI std::size_t capacity() const noexcept { return cap_; }

  private:
    std::size_t capidx_, cap_;
};

template <class K, class V, std::size_t Cap = no_cap, class Hash = std::hash<K>,
          class Eq    = std::equal_to<K>,
          class Alloc = ba::aligned_allocator<std::byte, std::max(alignof(K), alignof(V))>>
class table : table_info<Cap>
{
    using tity = table_info<Cap>;
    using tity::capacity;
    static constexpr auto soa = jutil::soa<K, V, uint8_t>;

  public:
    JUTIL_CI table(tity ti = {}, Hash hash = {}, Eq eq = {}, Alloc alloc = {})
        : tity{ti}, al_{alloc}, hash_{hash}, eq_{eq}
    {
        p_ = al_.allocate(soa.size(capacity()));
    }
    ~table()
    {
        sr::destroy(keys());
        sr::destroy(values());
        al_.deallocate(p_, soa.size(capacity()));
    }

    auto keys() noexcept { return soa(jutil::sidx<0>, p_, capacity(), sz_); }
    auto keys() const noexcept { return soa(jutil::sidx<0>, p_, capacity(), sz_); }
    auto values() noexcept { return soa(jutil::sidx<1>, p_, capacity(), sz_); }
    auto values() const noexcept { return soa(jutil::sidx<1>, p_, capacity(), sz_); }
    auto pls() noexcept { return soa(jutil::sidx<2>, p_, capacity(), sz_); }
    auto pls() const noexcept { return soa(jutil::sidx<2>, p_, capacity(), sz_); }

  private:
    std::byte *p_;
    std::size_t sz_ = 0;
    [[no_unique_address]] Alloc al_;
    [[no_unique_address]] Hash hash_;
    [[no_unique_address]] Eq eq_;
};

static table<std::string_view, std::string_view> asd;
} // namespace table
