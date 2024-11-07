// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Pavel Kirienko <pavel.kirienko@zubax.com>

#ifndef PLATFORM_STRING_HPP
#define PLATFORM_STRING_HPP

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <initializer_list>

namespace platform
{

/// A utility for storing and formatting strings of the specified capacity.
/// Content past the capacity is silently truncated.
/// This is a POD type; instances can be safely moved and copied.
/// The capacity does not include the null terminator; that is, the real storage is one byte larger.
/// This is convertible to/from `cetl::string_view`.
/// The most important methods of std::string are implemented here
/// (aside from those related to resizing, of course, as it makes no sense here).
///
/// Note that chars are treated as integers; if you need to add chars, either use push_back() or use strings.
/// Supports not only primitives like strings and numbers but also arrays, optionals, chrono time/duration,
/// enumerations, pairs, variants, and any combinations thereof.
///
/// operator<< is provided for std-like behavior (but there's no endl, use "\n" instead).
/// User code can provide custom formatting by overloading this operator as follows:
///
///     auto& operator<<(auto& str, const UserType& value) { <...formatting...> return str; }
///
/// Or less generically, but requires an explicit dependency on Dyshlo (which is often undesirable in abstract code):
///
///     template <std::size_t N>
///     platform::string<N>& operator<<(platform::string<N>& str, const UserType& value);
///
/// The ADL rules require that the overloaded operator<< is defined in the same namespace as the type being formatted.
/// Then define the formatting behavior in terms of the primitive types supported by String<>.
///
template <std::size_t N>
class String
{
public:
    String() = default;
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    String(const cetl::string_view str) noexcept  // NOSONAR implicit by design
    {
        operator=(str);
    }

    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    String& operator=(const cetl::string_view other)  // NOSONAR implicit by design
    {
        clear();
        operator+=(other);
        return *this;
    }

    CETL_NODISCARD auto data() noexcept -> char*
    {
        return buf_.data();
    }
    CETL_NODISCARD auto data() const noexcept -> const char*
    {
        return buf_.data();
    }
    CETL_NODISCARD auto c_str() const noexcept -> const char*
    {
        return data();
    }

    CETL_NODISCARD auto length() const noexcept -> std::size_t
    {
        return off_;
    }
    CETL_NODISCARD auto size() const noexcept -> std::size_t
    {
        return length();
    }
    CETL_NODISCARD auto empty() const noexcept -> bool
    {
        return 0 == size();
    }

    /// True if the buffer cannot accept extra data.
    /// This can be used to check for overflow: make the capacity one item greater than needed and ensure this is false.
    CETL_NODISCARD auto full() const noexcept -> bool
    {
        return off_ >= N;
    }

    CETL_NODISCARD constexpr auto capacity() const noexcept -> std::size_t
    {
        return N;
    }
    CETL_NODISCARD constexpr auto max_size() const noexcept -> std::size_t
    {
        return N;
    }

    /// The behavior is undefined if the string is empty.
    CETL_NODISCARD auto& front()
    {
        return buf_.front();
    }
    CETL_NODISCARD auto front() const
    {
        return buf_.front();
    }
    CETL_NODISCARD auto& back()
    {
        return buf_.at(off_ - 1);
    }
    CETL_NODISCARD auto back() const
    {
        return buf_.at(off_ - 1);
    }

    CETL_NODISCARD auto begin() noexcept -> char*
    {
        return data();
    }
    CETL_NODISCARD auto begin() const noexcept -> const char*
    {
        return data();
    }
    CETL_NODISCARD auto cbegin() const noexcept -> const char*
    {
        return data();
    }
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    CETL_NODISCARD auto end() noexcept -> char*
    {
        return data() + size();
    }
    CETL_NODISCARD auto end() const noexcept -> const char*
    {
        return data() + size();
    }
    CETL_NODISCARD auto cend() const noexcept -> const char*
    {
        return data() + size();
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    CETL_NODISCARD operator cetl::string_view() const noexcept  // NOSONAR implicit
    {
        return {c_str(), size()};
    }

    /// Add one character unless the storage is already full, in which case do nothing.
    void push_back(const char ch) noexcept
    {
        if (off_ < N)
        {
            buf_.at(off_) = ch;
            off_++;
        }
    }
    String& operator+=(const char ch) noexcept
    {
        push_back(ch);
        return *this;
    }

    /// Remove the last character unless the string is empty, in which case do nothing.
    void pop_back() noexcept
    {
        if (off_ > 0)
        {
            off_--;
            buf_.at(off_) = '\0';
        }
    }

    /// Add the specified bytes to the string without checking their values.
    /// Items that would overflow the buffer are silently truncated.
    String& operator+=(const cetl::string_view str) noexcept
    {
        assert(off_ <= N);
        const auto am = std::min(str.size(), N - off_);
        (void) std::memmove(buf_.begin() + off_, str.cbegin(), am);
        off_ += am;
        assert(off_ <= N);
        return *this;
    }

    void clear()
    {
        off_ = 0;
        buf_.fill(0);
    }

private:
    std::size_t             off_ = 0;
    std::array<char, N + 1> buf_{};
};

// --------------------------------------------------------------------------------------------------------------------

template <std::size_t A, std::size_t B>
CETL_NODISCARD bool operator==(const String<A>& lhs, const String<B>& rhs) noexcept
{
    return static_cast<cetl::string_view>(lhs) == static_cast<cetl::string_view>(rhs);
}
template <std::size_t B>
CETL_NODISCARD bool operator==(const char* const lhs, const String<B>& rhs) noexcept
{
    return static_cast<cetl::string_view>(lhs) == static_cast<cetl::string_view>(rhs);
}
template <std::size_t A>
CETL_NODISCARD bool operator==(const String<A>& lhs, const char* const rhs) noexcept
{
    return static_cast<cetl::string_view>(lhs) == static_cast<cetl::string_view>(rhs);
}

template <std::size_t C>
String<C>& operator<<(String<C>& str, const cetl::string_view x) noexcept
{
    str += x;
    return str;
}

template <std::size_t C>
String<C>& operator<<(String<C>& str, const char* const x) noexcept
{
    str += x;
    return str;
}

template <std::size_t C, std::size_t Z>
String<C>& operator<<(String<C>& str, const String<Z>& x) noexcept
{
    return str << static_cast<cetl::string_view>(x);
}

template <std::size_t C, typename Container>
String<C>& operator<<(String<C>& str, const Container& x)
{
    str += '[';
    bool first = true;
    for (auto it = x.cbegin(); it != x.cend(); ++it)
    {
        if (!first)
        {
            str += ',';
        }
        first = false;
        str << *it;
    }
    str += ']';
    return str;
}

namespace detail
{

template <std::size_t C, typename Tuple, std::size_t... Is>
void PrintTupleValues(String<C>& str, const Tuple& t, std::index_sequence<Is...>)
{
    (void) std::initializer_list<int>{((str << (Is ? "," : "") << std::get<Is>(t)), 0)...};
}

}  // namespace detail

template <std::size_t C, typename... A>
String<C>& operator<<(String<C>& str, const std::tuple<A...>& x)
{
    str += '(';
    detail::PrintTupleValues(str, x, std::index_sequence_for<A...>{});
    str += ')';
    return str;
}

template <std::size_t C, typename... A>
String<C>& operator<<(String<C>& str, const cetl::variant<A...>& x)
{
    return cetl::visit([&str](const auto& val) -> String<C>& { return str << val; }, x);
}

template <std::size_t C, typename Left, typename Right>
String<C>& operator<<(String<C>& str, const std::pair<Left, Right>& x)
{
    str += '(';
    str << x.first;
    str += ':';
    str << x.second;
    str += ')';
    return str;
}

template <std::size_t C, typename M>
String<C>& operator<<(String<C>& str, const cetl::optional<M>& x)
{
    if (x.has_value())
    {
        str << x.value();
    }
    return str;
}

template <std::size_t C>
String<C>& operator<<(String<C>& str, cetl::nullopt_t)
{
    return str;
}

/// A helper that constructs a String<N> and formats the specified arguments into it using operator<<.
/// Users can therefore customize formatting for their types by overloading operator<<.
template <std::size_t N, typename... A>
String<N> format(A&&... ar)
{
    String<N> sb;
    (void) std::initializer_list<int>{(sb << std::forward<A>(ar), 0)...};
    return sb;
}

template <std::size_t N, typename... A>
CETL_NODISCARD String<N> formatln(A&&... ar)
{
    return format<N>(std::forward<A>(ar)..., "\n");
}

}  // namespace platform

#endif  // PLATFORM_STRING_HPP
