// #include <benchmark/benchmark.h>
// #include <boost/align/aligned_allocator.hpp>
// #include <boost/compressed_pair.hpp>
// #include <boost/container/allocator.hpp>
// #include <boost/container/node_allocator.hpp>
// #include <functional>
// #include <ranges>

// #include "../jutil.h"

// #include "../lmacro_begin.h"

// namespace table
// {
// namespace bc = boost::container;
// namespace ba = boost::alignment;
// namespace sr = std::ranges;

// template <class K, class V>
// using pair = boost::compressed_pair<K, V>;

// template <class K, class V>
// using opaque_pair            = jutil::opaque<pair<K, V>>;

// static constexpr auto no_cap = static_cast<std::size_t>(-1);

// // https://planetmath.org/goodhashtableprimes
// static constexpr std::size_t primes[]{
//     53,       97,       193,      389,       769,       1543,      3079,      6151,       12289,
//     24593,    49157,    98317,    196613,    393241,    786433,    1572869,   3145739, 6291469,
//     12582917, 25165843, 50331653, 100663319, 201326611, 402653189, 805306457, 1610612741,
// };

// consteval std::size_t capidx(const std::size_t cap) noexcept
// {
//     return static_cast<std::size_t>(sr::lower_bound(primes, cap) - sr::begin(primes));
// }

// template <std::size_t Cap>
// class table_info
// {
//     static constexpr auto cap = primes[capidx(Cap)];

//   public:
//     JUTIL_CI table_info() {}
//     JUTIL_CI table_info(const table_info &) = default;
//     JUTIL_CI std::size_t capacity() const noexcept { return cap; }
// };

// template <>
// class table_info<no_cap>
// {
//   public:
//     consteval table_info(const std::size_t cap = 0) : capidx_{capidx(cap)}, cap_{primes[capidx_]}
//     {} JUTIL_CI table_info(const table_info &) = default; JUTIL_CI std::size_t capacity() const
//     noexcept { return cap_; }

//   private:
//     std::size_t capidx_, cap_;
// };

// template <class K, class V, std::size_t Cap = no_cap, class Hash = std::hash<K>,
//           class Eq    = std::equal_to<K>,
//           class Alloc = ba::aligned_allocator<std::byte, std::max(alignof(K), alignof(V))>>
// class table : table_info<Cap>
// {
//     using idx_ty = std::invoke_result_t<Hash, K>;
//     using pl_ty  = int8_t;

//     using tity   = table_info<Cap>;
//     using tity::capacity;
//     static constexpr auto soa = jutil::soa<K, V, int8_t>;

//   public:
//     JUTIL_CI table(tity ti = {}, Hash hash = {}, Eq eq = {}, Alloc alloc = {})
//         : tity{ti}, al_{alloc}, h_{hash}, eq_{eq}
//     {
//         p_ = al_.allocate(soa.size(capacity()));
//         jutil::construct(keys());
//         jutil::construct(vals());
//         jutil::construct(pls(), L(new (x) pl_ty{0}));
//     }
//     ~table()
//     {
//         sr::destroy(keys());
//         sr::destroy(vals());
//         al_.deallocate(p_, soa.size(capacity()));
//     }

//     JUTIL_CI auto begin() noexcept { return sr::begin(soa(p_, capacity())); }
//     JUTIL_CI auto end() noexcept { return sr::end(soa(p_, capacity())); }
//     JUTIL_CI auto begin() const noexcept { return sr::begin(soa(p_, capacity())); }
//     JUTIL_CI auto end() const noexcept { return sr::end(soa(p_, capacity())); }
//     JUTIL_CI auto cbegin() const noexcept { return sr::begin(soa(p_, capacity())); }
//     JUTIL_CI auto cend() const noexcept { return sr::end(soa(p_, capacity())); }

//     JUTIL_CI auto keys() noexcept { return soa(jutil::idx<0>, p_, capacity()); }
//     JUTIL_CI auto keys() const noexcept { return soa(jutil::idx<0>, p_, capacity()); }
//     JUTIL_CI auto vals() noexcept { return soa(jutil::idx<1>, p_, capacity()); }
//     JUTIL_CI auto vals() const noexcept { return soa(jutil::idx<1>, p_, capacity()); }
//     JUTIL_CI auto pls() noexcept { return soa(jutil::idx<2>, p_, capacity()); }
//     JUTIL_CI auto pls() const noexcept { return soa(jutil::idx<2>, p_, capacity()); }

//   private:
//     template <class Fail = jutil::never, class Ok = std::identity>
//     static JUTIL_CI auto
//     Find(const auto t, const auto &&key, Fail &&fail = {},
//          Ok &&ok = {}) noexcept(noexcept(fail(idx_ty{})) && noexcept(ok(idx_ty{})))
//         -> std::common_type_t<decltype(fail(idx_ty{})), decltype(ok(idx_ty{}))>
//     {
//         const auto idx = t->h_(key) % capacity();
//         for (pl_ty i = 0;;) {
//             while (t->pls()[idx + i] > i)
//                 i = i == capacity() ? 0 : i + 1;
//             if (t->pls()[idx + i] < i) return fail(idx, i);
//             if (t->eq_(t->keys()[idx + i], key)) return ok(idx + i);
//         }
//     }

//   public:
//     // Just like for `T[n]` subscript outside `[0..n)` is UB, used key not being in map is UB.
//     // You should use `get()` for a safe alternative.
//     template <class T>
//     JUTIL_CI auto &operator[](T &&key) noexcept
//     {
//         return vals()[Find(this, static_cast<T &&>(key))];
//     }
//     template <class T>
//     JUTIL_CI auto &operator[](T &&key) const noexcept
//     {
//         return vals()[Find(this, static_cast<T &&>(key))];
//     }

//   private:
//     JUTIL_CI V &Reserve(const auto idx, const auto off, K &&key,
//                         auto &&init) noexcept(noexcept(init(std::declval<V *>())))
//     {
//         const auto base = idx + off;
//         auto i          = base;
//         auto j          = i;
//         for (auto x = static_cast<pl_ty>(off);; ++j) {
//             if (x < 0) {
//                 std::shift_right(&keys()[i], &keys()[j], 1);
//                 std::shift_right(&vals()[i], &vals()[j], 1);
//                 if (j != capacity()) {
//                     if (i == 0) {
//                         // capacity() TODO: implement
//                         break;
//                     }
//                     i = j = 0;
//                 }
//             } else {
//                 x = std::exchange(pls()[j], x + 1);
//             }
//         }
//         keys()[base] = static_cast<K &&>(key);
//         std::move(init)(&vals()[base]);
//         return vals()[base];
//     }

//   public:
//     /// @brief retrieves value at `key`, or initializes it with `init(ptr)`
//     /// @param key locates either a preëxisting value or a to-be-initialized value
//     /// @param init callable that, called with `V *p`, initializes `*p`
//     /// @return value resultantly resident at `key`
//     template <class T, jutil::callable<V *> F>
//     JUTIL_CI V &get(T &&key, F &&init) noexcept(noexcept(init(std::declval<V *>())))
//     {
//         return Find(key, L2(Reserve(x, y, {static_cast<T &&>(key)}, static_cast<F &&>(init)), &),
//                     L(&vals()[x], &));
//     }
//     /// @brief addresses value at `key`, if any
//     /// @param key represents the checked value location
//     /// @return value resident at `key` or nullptr
//     template <class T>
//     JUTIL_CI V *get(T &&key) noexcept
//     {
//         return Find(key, L2(static_cast<V *>(nullptr)), L(&vals()[x], &));
//     }

//   private:
//     std::byte *p_;
//     [[no_unique_address]] Alloc al_;
//     [[no_unique_address]] Hash h_;
//     [[no_unique_address]] Eq eq_;
// };

// static table<std::string_view, std::string_view> asd;
// } // namespace table

// #include "../lmacro_end.h"
