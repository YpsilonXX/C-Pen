#include <catch2/catch_test_macros.hpp>

/// Temporary test — keeps the test target buildable before real test suites
/// exist. Delete once the first real test file lands in tests/.
TEST_CASE("placeholder", "[meta]") {
    REQUIRE(true);
}
