#ifndef CPEN_CORE_VALUE_HH
#define CPEN_CORE_VALUE_HH

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace cpen::core
{
    /// The engine's dynamic value: what the blackboard stores and what the script
    /// VM computes with.
    ///
    /// It lives in core rather than in script because the blackboard holds script
    /// globals, and core must not depend on a layer above it. The class wraps the
    /// variant instead of exposing it so that reference types (arrays, object
    /// handles) can be added as further alternatives without touching call sites.
    ///
    /// Integers and floating-point numbers are deliberately distinct types, not a
    /// single number type: the DSL promises `1 / 2 == 0` and `1.0 / 2 == 0.5`.
    /// Promotion between them is arithmetic semantics and therefore belongs to the
    /// VM; this class only stores and reports, and never coerces implicitly.
    class Value
    {
    public:
        enum class Type : std::uint8_t
        {
            NIL,
            BOOLEAN,
            INTEGER,
            FLOATING,
            TEXT,
        };

        /// Default-constructs to NIL, which is also what an unassigned blackboard
        /// slot reads as.
        Value() noexcept = default;

        Value(const bool boolean) noexcept
            : storage(boolean)
        {
        }

        /// Every integral type collapses to int64. Constrained templates rather
        /// than plain overloads: `Value(5)` would otherwise be ambiguous between
        /// the bool, int64 and double conversions.
        template <std::integral IntegerType>
            requires (!std::same_as<IntegerType, bool>)
        Value(const IntegerType integer) noexcept
            : storage(static_cast<std::int64_t>(integer))
        {
        }

        template <std::floating_point FloatingType>
        Value(const FloatingType floating) noexcept
            : storage(static_cast<double>(floating))
        {
        }

        Value(std::string text)
            : storage(std::move(text))
        {
        }

        Value(const std::string_view text)
            : storage(std::string(text))
        {
        }

        /// Without this overload a string literal would select the bool
        /// constructor, silently turning "text" into `true`.
        Value(const char* text)
            : storage(std::string(text))
        {
        }

        Type type() const noexcept
        {
            return static_cast<Type>(this->storage.index());
        }

        bool is_nil() const noexcept { return this->type() == Type::NIL; }
        bool is_boolean() const noexcept { return this->type() == Type::BOOLEAN; }
        bool is_integer() const noexcept { return this->type() == Type::INTEGER; }
        bool is_floating() const noexcept { return this->type() == Type::FLOATING; }
        bool is_text() const noexcept { return this->type() == Type::TEXT; }

        bool is_number() const noexcept
        {
            return this->is_integer() || this->is_floating();
        }

        /// Typed access without coercion: a mismatch yields nullopt rather than a
        /// converted value, so a caller that meant to accept both numeric types
        /// has to say so explicitly.
        std::optional<bool> as_boolean() const noexcept
        {
            return this->is_boolean() ? std::optional(std::get<bool>(this->storage)) : std::nullopt;
        }

        std::optional<std::int64_t> as_integer() const noexcept
        {
            return this->is_integer()
                       ? std::optional(std::get<std::int64_t>(this->storage))
                       : std::nullopt;
        }

        std::optional<double> as_floating() const noexcept
        {
            return this->is_floating() ? std::optional(std::get<double>(this->storage)) : std::nullopt;
        }

        /// Empty view for every non-TEXT value. The view is owned by this Value
        /// and dies with it.
        std::string_view as_text() const noexcept
        {
            return this->is_text() ? std::string_view(std::get<std::string>(this->storage))
                                   : std::string_view{};
        }

        /// Numeric widening used by mixed-type arithmetic: INTEGER converts,
        /// FLOATING passes through, anything else fails. Integers beyond 2^53
        /// lose precision, as they would in any int-to-double conversion.
        std::optional<double> to_floating() const noexcept
        {
            switch (this->type())
            {
                case Type::INTEGER:
                    return static_cast<double>(std::get<std::int64_t>(this->storage));
                case Type::FLOATING:
                    return std::get<double>(this->storage);
                default:
                    return std::nullopt;
            }
        }

        /// Strict, type-aware equality: `Value(1) != Value(1.0)`, because the two
        /// are different types holding different representations. The language's
        /// `==` operator, which promotes across numeric types, is a VM concern and
        /// is built on top of this, not the other way round.
        bool operator==(const Value& other) const
        {
            return this->storage == other.storage;
        }

        /// Diagnostic rendering for logs and test output; not a language-level
        /// string conversion. TEXT is quoted and FLOATING always carries a
        /// fractional part, so `1` and `1.0` remain distinguishable by eye.
        std::string to_string() const;

    private:
        using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

        // Type is read straight off the variant index, so the two orders must
        // agree; these assertions are what keeps that coupling honest.
        static_assert(std::variant_size_v<Storage> == 5);
        static_assert(std::is_same_v<
            std::variant_alternative_t<static_cast<std::size_t>(Type::NIL), Storage>, std::monostate>);
        static_assert(std::is_same_v<
            std::variant_alternative_t<static_cast<std::size_t>(Type::BOOLEAN), Storage>, bool>);
        static_assert(std::is_same_v<
            std::variant_alternative_t<static_cast<std::size_t>(Type::INTEGER), Storage>, std::int64_t>);
        static_assert(std::is_same_v<
            std::variant_alternative_t<static_cast<std::size_t>(Type::FLOATING), Storage>, double>);
        static_assert(std::is_same_v<
            std::variant_alternative_t<static_cast<std::size_t>(Type::TEXT), Storage>, std::string>);

        Storage storage{};
    };

    /// Canonical lowercase spelling of a value type ("nil", "integer", ...).
    std::string_view to_string(Value::Type type) noexcept;
}

#endif //CPEN_CORE_VALUE_HH
