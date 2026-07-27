#include "cpen/core/value.hh"

#include <format>

namespace cpen::core
{
    std::string_view to_string(const Value::Type type) noexcept
    {
        switch (type)
        {
            case Value::Type::NIL: return "nil";
            case Value::Type::BOOLEAN: return "boolean";
            case Value::Type::INTEGER: return "integer";
            case Value::Type::FLOATING: return "floating";
            case Value::Type::TEXT: return "text";
        }
        return "unknown";
    }

    std::string Value::to_string() const
    {
        switch (this->type())
        {
            case Type::NIL:
                return "nil";

            case Type::BOOLEAN:
                return this->as_boolean().value() ? "true" : "false";

            case Type::INTEGER:
                return std::format("{}", this->as_integer().value());

            case Type::FLOATING:
            {
                // std::format prints the shortest round-trippable form, which for
                // 1.0 is "1" — indistinguishable from the integer 1 in a log line.
                std::string text = std::format("{}", this->as_floating().value());
                if (text.find_first_of(".eEni") == std::string::npos)
                {
                    text += ".0";
                }
                return text;
            }

            case Type::TEXT:
                return std::format("\"{}\"", this->as_text());
        }
        return "unknown";
    }
}
