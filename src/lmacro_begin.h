#include <boost/preprocessor/cat.hpp>

#pragma push_macro("L")
#pragma push_macro("L2")
#pragma push_macro("L0")

#define L0(Expr, ...) [__VA_ARGS__] -> decltype(Expr) { return Expr; }
#define L(Expr, ...)                                                                               \
    [__VA_ARGS__]<class BOOST_PP_CAT(T, __LINE__)>(                                                \
        [[maybe_unused]] const BOOST_PP_CAT(T, __LINE__) & x) -> decltype(Expr) { return Expr; }
#define L2(Expr, ...)                                                                              \
    [__VA_ARGS__]<class BOOST_PP_CAT(T, __LINE__), class BOOST_PP_CAT(U, __LINE__)>(               \
        [[maybe_unused]] const BOOST_PP_CAT(T, __LINE__) & x,                                      \
        [[maybe_unused]] const BOOST_PP_CAT(U, __LINE__) & y) -> decltype(Expr) { return Expr; }
#define M0(Expr, ...) [__VA_ARGS__] mutable -> decltype(auto) { return Expr; }
#define M(Expr, ...)                                                                               \
    [__VA_ARGS__]<class BOOST_PP_CAT(T, __LINE__)>(                                                \
        [[maybe_unused]] const BOOST_PP_CAT(T, __LINE__) & x) mutable -> decltype(auto) {          \
        return Expr;                                                                               \
    }
#define M2(Expr, ...)                                                                              \
    [__VA_ARGS__]<class BOOST_PP_CAT(T, __LINE__), class BOOST_PP_CAT(U, __LINE__)>(               \
        [[maybe_unused]] const BOOST_PP_CAT(T, __LINE__) & x,                                      \
        [[maybe_unused]] const BOOST_PP_CAT(U, __LINE__) & y) mutable -> decltype(auto) {          \
        return Expr;                                                                               \
    }
