#pragma push_macro("L")
#pragma push_macro("L2")
#pragma push_macro("L0")

#define L(Expr, ...)                                                                               \
    [__VA_ARGS__]([[maybe_unused]] const auto &x) -> decltype(Expr) { return Expr; }
#define L2(Expr, ...)                                                                              \
    [__VA_ARGS__]([[maybe_unused]] const auto &x,                                                  \
                  [[maybe_unused]] const auto &y) -> decltype(Expr) { return Expr; }
#define L0(Expr, ...) [__VA_ARGS__]() -> decltype(Expr) { return Expr; }
