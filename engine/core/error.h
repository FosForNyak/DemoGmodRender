#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace gmdr {

// A recoverable failure. `code` is a stable dotted identifier (e.g. "demo.bad_header") that the UI maps to
// its own text; `message` is English for logs; `details` holds extra context (offsets, sizes).
struct Error {
    std::string code;
    std::string message;
    std::string details;
};

inline Error makeError(std::string code, std::string message, std::string details = {}) {
    return Error{std::move(code), std::move(message), std::move(details)};
}

template <class T> class [[nodiscard]] Result {
public:
    Result(T value) : v_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : v_(std::in_place_index<1>, std::move(error)) {}

    bool ok() const { return v_.index() == 0; }
    explicit operator bool() const { return ok(); }

    T& value() & { return std::get<0>(v_); }
    const T& value() const& { return std::get<0>(v_); }
    T&& value() && { return std::get<0>(std::move(v_)); }
    T* operator->() { return &std::get<0>(v_); }
    const T* operator->() const { return &std::get<0>(v_); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

    const Error& error() const { return std::get<1>(v_); }

private:
    std::variant<T, Error> v_;
};

template <> class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }
    const Error& error() const { return *error_; }

private:
    std::optional<Error> error_;
};

inline Result<void> okResult() { return {}; }

} // namespace gmdr

// Propagates an error from an expression returning Result<...>; usable in functions returning any Result.
#define GMDR_TRY(expr)                                                                                        \
    do {                                                                                                      \
        auto&& gmdr_try_result_ = (expr);                                                                     \
        if (!gmdr_try_result_)                                                                                \
            return gmdr_try_result_.error();                                                                  \
    } while (0)

// Assigns the value of a Result<T> expression to `lhs` or propagates the error.
#define GMDR_ASSIGN(lhs, expr)                                                                                \
    auto&& GMDR_CONCAT(gmdr_assign_, __LINE__) = (expr);                                                      \
    if (!GMDR_CONCAT(gmdr_assign_, __LINE__))                                                                 \
        return GMDR_CONCAT(gmdr_assign_, __LINE__).error();                                                   \
    lhs = std::move(GMDR_CONCAT(gmdr_assign_, __LINE__)).value()

#define GMDR_CONCAT_INNER(a, b) a##b
#define GMDR_CONCAT(a, b) GMDR_CONCAT_INNER(a, b)
