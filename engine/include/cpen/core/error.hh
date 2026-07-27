#ifndef CPEN_CORE_ERROR_HH
#define CPEN_CORE_ERROR_HH

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace cpen::core
{
    /// Classifies a failure for the rare caller that branches on it.
    ///
    /// Deliberately small, and meant to stay that way. A new enumerator is added
    /// only once some caller actually distinguishes that case; everything else
    /// reports UNSPECIFIED and relies on the message. This is what keeps the enum
    /// from growing into the usual eighty-value list of which three are ever
    /// tested.
    enum class ErrorCode : std::uint8_t
    {
        UNSPECIFIED,
        FILE_NOT_FOUND,
        INVALID_FORMAT,
        COMPILATION_FAILED,
    };

    constexpr std::string_view to_string(const ErrorCode code) noexcept
    {
        switch (code)
        {
            case ErrorCode::UNSPECIFIED:        return "unspecified";
            case ErrorCode::FILE_NOT_FOUND:     return "file not found";
            case ErrorCode::INVALID_FORMAT:     return "invalid format";
            case ErrorCode::COMPILATION_FAILED: return "compilation failed";
        }
        return "unknown";
    }

    /// The engine's failure type, carried as the error alternative of
    /// std::expected.
    ///
    /// One type for the whole engine rather than one per subsystem: std::expected
    /// composes through and_then only while the error type stays the same, and
    /// asset loading, shader creation and script compilation are all meant to
    /// chain. Per-subsystem error types would put a transform_error at every
    /// layer boundary.
    ///
    /// The message is the substance. Failures at this level are reported to a
    /// human and handled by falling back or giving up, not by inspecting fields,
    /// so the message must be specific enough to act on: what was being done, to
    /// what, and what the underlying component said about it.
    struct Error
    {
        ErrorCode code = ErrorCode::UNSPECIFIED;
        std::string message;
    };

    /// Builds an Error with a formatted message.
    ///
    /// Exists because the alternative at every call site is a designated
    /// initialiser wrapped around a std::format call, which buries the message
    /// in punctuation.
    template <typename... Arguments>
    Error make_error(const ErrorCode code,
                     const std::format_string<Arguments...> format,
                     Arguments&&... arguments)
    {
        return Error{
            .code = code,
            .message = std::format(format, std::forward<Arguments>(arguments)...),
        };
    }
}

/// Lets an Error be logged directly with "{}" instead of being unpacked at every
/// reporting site. No format specification is accepted.
template <>
struct std::formatter<cpen::core::Error>
{
    constexpr auto parse(std::format_parse_context& context) const
    {
        return context.begin();
    }

    auto format(const cpen::core::Error& value, std::format_context& context) const
    {
        return std::format_to(context.out(), "[{}] {}",
                              cpen::core::to_string(value.code), value.message);
    }
};

#endif //CPEN_CORE_ERROR_HH
