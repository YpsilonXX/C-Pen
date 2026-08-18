#ifndef CPEN_TESTS_SUPPORT_VALUE_PRINTING_HH
#define CPEN_TESTS_SUPPORT_VALUE_PRINTING_HH

#include "cpen/core/value.hh"

#include <catch2/catch_tostring.hpp>

#include <string>

/// Teaches Catch2 to print a core::Value.
///
/// Without this a failed comparison reads "{?} == {?}", which says only that two
/// values differed -- and the two most useful questions, which value and of what
/// type, both go unanswered. Value::to_string is already built for exactly this:
/// it quotes text and keeps 1 distinguishable from 1.0.
///
/// Include it before the cases in any test that compares values.
template <>
struct Catch::StringMaker<cpen::core::Value>
{
    static std::string convert(const cpen::core::Value& value)
    {
        return value.to_string();
    }
};

#endif //CPEN_TESTS_SUPPORT_VALUE_PRINTING_HH
