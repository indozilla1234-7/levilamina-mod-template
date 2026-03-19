// NOLINTBEGIN

#include <format>   // required for std::formatter

namespace std {

// Only support char formatting (std::string / std::string_view)
template <ll::concepts::IsVectorBase T>
struct formatter<T, char> : std::formatter<std::string_view, char> {
    template <class FormatContext>
    auto format(T const& t, FormatContext& ctx) const noexcept {
        std::string_view sv{t.toString()};
        return std::formatter<std::string_view, char>::format(sv, ctx);
    }
};

template <ll::concepts::IsVectorBase T>
struct hash<T> {
    constexpr size_t operator()(T const& vec) const noexcept {
        return vec.hash();
    }
};

} // namespace std

// fmt support
template <ll::concepts::IsVectorBase T>
struct fmt::formatter<T, char> : fmt::formatter<std::string_view, char> {
    template <class FormatContext>
    auto format(T const& t, FormatContext& ctx) const {
        std::string_view sv{t.toString()};
        return fmt::formatter<std::string_view, char>::format(sv, ctx);
    }
};

// NOLINTEND
