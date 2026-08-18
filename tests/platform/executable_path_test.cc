#include <catch2/catch_test_macros.hpp>

#include "cpen/platform/executable_path.hh"
#include "support/trace.hh"

#include <filesystem>

using cpen::platform::executable_directory;
using cpen::platform::executable_path;
using cpen::test::trace;
using cpen::test::trace_step;

TEST_CASE("the running executable knows where it is", "[platform][executable_path]")
{
    const auto path = executable_path();

    REQUIRE(path.has_value());
    trace("running from {}", path->string());

    CHECK(path->is_absolute());
    CHECK(std::filesystem::is_regular_file(*path));

    // Named rather than merely "some file exists": the value of this function is
    // that it answers with *this* program however it was started, and a check that
    // accepted any path at all would pass on an answer read from argv[0].
    CHECK(path->stem().string().starts_with("cpen_tests"));

    trace_step("and the directory is that path's parent, not the one we happen to be in");
    const auto directory = executable_directory();

    REQUIRE(directory.has_value());
    CHECK(*directory == path->parent_path());
}

TEST_CASE("the answer does not depend on the working directory",
          "[platform][executable_path]")
{
    const auto before = executable_path();
    REQUIRE(before.has_value());

    const std::filesystem::path original = std::filesystem::current_path();

    // The whole point of asking the operating system. A game started from a
    // desktop shortcut, a launcher or a debugger gets a different working
    // directory every time, and none of them is where its files are.
    std::filesystem::current_path(std::filesystem::temp_directory_path());

    const auto after = executable_path();

    std::filesystem::current_path(original);

    REQUIRE(after.has_value());
    CHECK(*after == *before);
}
