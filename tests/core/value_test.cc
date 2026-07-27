#include <catch2/catch_test_macros.hpp>

#include "cpen/core/value.hh"
#include "support/trace.hh"

#include <cstdint>
#include <string>

using cpen::core::Value;
using cpen::test::trace;

TEST_CASE("a default value is nil", "[core][value]")
{
    const Value value;

    trace("default-constructed value reports type '{}'", cpen::core::to_string(value.type()));

    CHECK(value.is_nil());
    CHECK(value.type() == Value::Type::NIL);
    CHECK_FALSE(value.is_number());
    CHECK(value.to_string() == "nil");
}

TEST_CASE("constructors pick the intended alternative", "[core][value]")
{
    // Every one of these would resolve differently without the constrained
    // templates and the explicit const char* overload.
    const Value boolean{true};
    const Value integer{5};
    const Value small_integer{static_cast<short>(7)};
    const Value floating{2.5};
    const Value single{1.5f};
    const Value literal{"текст"};
    const Value text{std::string("owned")};

    trace("true -> {}, 5 -> {}, (short)7 -> {}", cpen::core::to_string(boolean.type()),
          cpen::core::to_string(integer.type()), cpen::core::to_string(small_integer.type()));
    trace("2.5 -> {}, 1.5f -> {}", cpen::core::to_string(floating.type()),
          cpen::core::to_string(single.type()));
    trace("\"текст\" -> {} (not boolean), std::string -> {}",
          cpen::core::to_string(literal.type()), cpen::core::to_string(text.type()));

    CHECK(boolean.type() == Value::Type::BOOLEAN);
    CHECK(integer.type() == Value::Type::INTEGER);
    CHECK(small_integer.type() == Value::Type::INTEGER);
    CHECK(floating.type() == Value::Type::FLOATING);
    CHECK(single.type() == Value::Type::FLOATING);

    // The regression this guards: a string literal converts to bool, so without
    // the const char* overload `Value("текст")` would silently become `true`.
    REQUIRE(literal.type() == Value::Type::TEXT);
    CHECK(literal.as_text() == "текст");
    CHECK(text.as_text() == "owned");
}

TEST_CASE("typed access refuses foreign types", "[core][value]")
{
    const Value integer{42};

    trace("integer 42: as_integer={}, as_floating set={}, as_boolean set={}",
          integer.as_integer().value(), integer.as_floating().has_value(),
          integer.as_boolean().has_value());

    CHECK(integer.as_integer() == std::int64_t{42});
    CHECK_FALSE(integer.as_floating().has_value());
    CHECK_FALSE(integer.as_boolean().has_value());
    CHECK(integer.as_text().empty());

    const Value text{"hello"};
    trace("text \"hello\": as_integer set={}, as_text=\"{}\"", text.as_integer().has_value(),
          text.as_text());
    CHECK_FALSE(text.as_integer().has_value());
}

TEST_CASE("to_floating widens integers and nothing else", "[core][value]")
{
    trace("integer 3 -> {}", Value{3}.to_floating().value());
    trace("floating 3.5 -> {}", Value{3.5}.to_floating().value());
    trace("text has value: {}", Value{"x"}.to_floating().has_value());

    CHECK(Value{3}.to_floating() == 3.0);
    CHECK(Value{3.5}.to_floating() == 3.5);
    CHECK_FALSE(Value{"x"}.to_floating().has_value());
    CHECK_FALSE(Value{true}.to_floating().has_value());
    CHECK_FALSE(Value{}.to_floating().has_value());
}

TEST_CASE("equality is type-aware, without numeric promotion", "[core][value]")
{
    trace("Value(1) == Value(1.0) -> {} (promotion is the VM's job)",
          Value{1} == Value{1.0});

    CHECK(Value{1} == Value{1});
    CHECK(Value{1.0} == Value{1.0});
    CHECK_FALSE(Value{1} == Value{1.0});
    CHECK_FALSE(Value{1} == Value{true});
    CHECK(Value{} == Value{});
    CHECK(Value{"a"} == Value{std::string("a")});
    CHECK_FALSE(Value{"a"} == Value{"b"});
}

TEST_CASE("to_string keeps integers and floats distinguishable", "[core][value]")
{
    trace("integer 1 -> {} | floating 1.0 -> {}", Value{1}.to_string(), Value{1.0}.to_string());
    trace("floating 2.5 -> {} | text -> {} | boolean -> {}", Value{2.5}.to_string(),
          Value{"hi"}.to_string(), Value{false}.to_string());

    CHECK(Value{1}.to_string() == "1");
    CHECK(Value{1.0}.to_string() == "1.0");
    CHECK(Value{2.5}.to_string() == "2.5");
    CHECK(Value{"hi"}.to_string() == "\"hi\"");
    CHECK(Value{false}.to_string() == "false");
}

TEST_CASE("type names are the canonical spellings", "[core][value]")
{
    trace("nil/boolean/integer/floating/text");

    CHECK(cpen::core::to_string(Value::Type::NIL) == "nil");
    CHECK(cpen::core::to_string(Value::Type::BOOLEAN) == "boolean");
    CHECK(cpen::core::to_string(Value::Type::INTEGER) == "integer");
    CHECK(cpen::core::to_string(Value::Type::FLOATING) == "floating");
    CHECK(cpen::core::to_string(Value::Type::TEXT) == "text");
}
