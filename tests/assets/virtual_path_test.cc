#include <catch2/catch_test_macros.hpp>

#include "cpen/assets/virtual_path.hh"
#include "support/temporary_directory.hh"
#include "support/trace.hh"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using cpen::assets::audit_path_case;
using cpen::assets::CaseMismatchOutcome;
using cpen::assets::format_case_mismatch_report;
using cpen::assets::PathCaseMismatch;
using cpen::assets::split_virtual_path;
using cpen::assets::validate_virtual_path;
using cpen::test::TemporaryDirectory;
using cpen::test::trace;
using cpen::test::trace_step;

namespace
{
    void check_accepted(const std::string_view path)
    {
        const auto result = validate_virtual_path(path);

        if (!result.has_value())
        {
            trace("unexpectedly refused '{}': {}", path, result.error());
        }

        CHECK(result.has_value());
    }

    void check_refused(const std::string_view path)
    {
        const auto result = validate_virtual_path(path);

        REQUIRE_FALSE(result.has_value());
        trace("'{}' -> {}", path, result.error().message);
    }
}

TEST_CASE("an ordinary asset path is accepted", "[assets][virtual_path]")
{
    check_accepted("ui.png");
    check_accepted("fonts/ui.ttf");
    check_accepted("sprites/alice/happy.png");
    check_accepted("bg/room 2.png");
    check_accepted("sprites/alice.happy.v2.png");

    trace_step("case is not this function's business, and neither is Cyrillic");
    check_accepted("Sprites/Alice.png");
    check_accepted("фоны/комната.png");

    trace_step("a name that merely begins like a reserved device is a normal name");
    check_accepted("console.png");
    check_accepted("audio/nullify.ogg");
}

TEST_CASE("a path that leaves the mounted roots is refused", "[assets][virtual_path]")
{
    check_refused("/etc/passwd");
    check_refused("../saves/slot_01.save");
    check_refused("sprites/../../secret.png");
    check_refused("./ui.png");
    check_refused("C:/Windows/Fonts/arial.ttf");
}

TEST_CASE("a path that means two things on two systems is refused", "[assets][virtual_path]")
{
    trace_step("a backslash separates on Windows and is part of the name on Linux");
    check_refused("sprites\\alice.png");

    trace_step("an empty component is a join that added one separator too many");
    check_refused("");
    check_refused("bg//room.png");
    check_refused("bg/");

    trace_step("Windows drops a space or a dot at the end of a component");
    check_refused("bg/room ");
    check_refused("bg/room.");
    check_refused("bg /room.png");

    trace_step("control characters are not file names anywhere");
    check_refused(std::string_view{"bg/ro\tom.png"});
}

TEST_CASE("a name Windows reserves for a device is refused", "[assets][virtual_path]")
{
    // The trap is one-directional and silent: these files can be created on Linux
    // and cannot exist on Windows at all, so the game simply has no such asset
    // there.
    check_refused("nul.png");
    check_refused("audio/con.ogg");
    check_refused("COM1");
    check_refused("lpt9.txt");
    check_refused("sprites/AUX.png");
}

TEST_CASE("splitting a virtual path", "[assets][virtual_path]")
{
    CHECK(split_virtual_path("sprites/alice/happy.png") ==
          std::vector<std::string_view>{"sprites", "alice", "happy.png"});

    CHECK(split_virtual_path("ui.png") == std::vector<std::string_view>{"ui.png"});

    trace_step("empty components are kept, so validation can complain about them");
    CHECK(split_virtual_path("bg//room.png") ==
          std::vector<std::string_view>{"bg", "", "room.png"});
}

TEST_CASE("a path spelled exactly as it is on disk audits clean", "[assets][virtual_path]")
{
    const TemporaryDirectory directory;
    directory.write("sprites/alice.png", "pixels");

    CHECK(audit_path_case(directory.path(), "sprites/alice.png").empty());
}

TEST_CASE("the audit names every component that differs in case", "[assets][virtual_path]")
{
    const TemporaryDirectory directory;
    directory.write("Sprites/Alice.png", "pixels");

    const std::vector<PathCaseMismatch> mismatches =
        audit_path_case(directory.path(), "sprites/alice.png");

    REQUIRE(mismatches.size() == 2);

    for (const PathCaseMismatch& mismatch : mismatches)
    {
        trace("'{}' is really '{}'", mismatch.requested, mismatch.actual);
    }

    CHECK(mismatches[0].requested == "sprites");
    CHECK(mismatches[0].actual == "Sprites");
    CHECK(mismatches[1].requested == "alice.png");
    CHECK(mismatches[1].actual == "Alice.png");

    trace_step("a directory that agrees is not reported, only the file that does not");
    directory.write("bg/Room.png", "pixels");

    const std::vector<PathCaseMismatch> file_only =
        audit_path_case(directory.path(), "bg/room.png");

    REQUIRE(file_only.size() == 1);
    CHECK(file_only[0].actual == "Room.png");
}

TEST_CASE("a file that is not there in any case is not a case problem",
          "[assets][virtual_path]")
{
    const TemporaryDirectory directory;
    directory.write("sprites/alice.png", "pixels");

    trace_step("a name nothing matches belongs to whoever reports missing files");
    CHECK(audit_path_case(directory.path(), "sprites/bob.png").empty());

    trace_step("and so does a directory that does not exist");
    CHECK(audit_path_case(directory.path(), "audio/theme.ogg").empty());
}

TEST_CASE("only ASCII letters are folded", "[assets][virtual_path]")
{
    const TemporaryDirectory directory;
    directory.write("фоны/Комната.png", "pixels");

    // Folding "Комната" against "комната" needs the Unicode case tables, which
    // this engine does not carry. The consequence is documented rather than
    // hidden: such a mismatch is reported as a missing file, not as a case
    // problem.
    CHECK(audit_path_case(directory.path(), "фоны/комната.png").empty());
}

TEST_CASE("the mismatch report says what will happen", "[assets][virtual_path]")
{
    const std::vector<PathCaseMismatch> mismatches = {
        PathCaseMismatch{.requested = "Alice.png", .actual = "alice.png"},
    };

    trace_step("found on a case-insensitive system: the warning is about Linux");
    const std::string loaded = format_case_mismatch_report(
        "sprites/Alice.png", mismatches, CaseMismatchOutcome::LOADED_ANYWAY);
    trace("{}", loaded);

    CHECK(loaded.find("sprites/Alice.png") != std::string::npos);
    CHECK(loaded.find("sprites/alice.png") != std::string::npos);
    CHECK(loaded.find("WILL NOT LOAD") != std::string::npos);

    trace_step("missing on a case-sensitive one: the message is why this load failed");
    const std::string missing = format_case_mismatch_report(
        "sprites/Alice.png", mismatches, CaseMismatchOutcome::NOT_FOUND,
        "the character will not be drawn");

    CHECK(missing.find("ASSET NOT LOADED") != std::string::npos);
    CHECK(missing.find("the character will not be drawn") != std::string::npos);
}
