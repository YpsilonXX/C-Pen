#include <catch2/catch_test_macros.hpp>

#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/error.hh"
#include "cpen/core/file_system.hh"
#include "support/temporary_directory.hh"
#include "support/trace.hh"

#include <string>
#include <string_view>

using cpen::assets::CaseMismatchOutcome;
using cpen::assets::DEFAULT_PATH_AUDIT;
using cpen::assets::VirtualFileSystem;
using cpen::core::ErrorCode;
using cpen::core::path_to_utf8;
using cpen::test::TemporaryDirectory;
using cpen::test::trace;
using cpen::test::trace_step;

TEST_CASE("an asset is read through its virtual path", "[assets][virtual_file_system]")
{
    const TemporaryDirectory game;
    game.write("script/intro.pen", "label start\r\n");

    VirtualFileSystem files;
    files.mount(game.path());

    const auto located = files.locate("script/intro.pen");
    REQUIRE(located.has_value());
    trace("resolved to {}", located->string());

    CHECK(files.exists("script/intro.pen"));

    trace_step("read_text normalises, read does not");
    const auto text = files.read_text("script/intro.pen");
    REQUIRE(text.has_value());
    CHECK(*text == "label start\n");

    const auto bytes = files.read("script/intro.pen");
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() == 13);
}

TEST_CASE("the first root that has the file wins", "[assets][virtual_file_system]")
{
    const TemporaryDirectory game;
    const TemporaryDirectory engine;

    // What shadowing is for: the engine ships a default, the game replaces it
    // without having to know that a default existed.
    engine.write("fonts/ui.ttf", "engine typeface");
    engine.write("shaders/sprite.frag", "engine shader");
    game.write("fonts/ui.ttf", "game typeface");

    VirtualFileSystem files;
    files.mount(game.path());
    files.mount(engine.path());

    CHECK(*files.read_text("fonts/ui.ttf") == "game typeface");

    trace_step("and what the game does not carry still comes from the engine");
    CHECK(*files.read_text("shaders/sprite.frag") == "engine shader");

    CHECK(files.roots().size() == 2);
}

TEST_CASE("a missing asset names every root that was searched",
          "[assets][virtual_file_system]")
{
    const TemporaryDirectory game;
    const TemporaryDirectory engine;

    VirtualFileSystem files;
    files.mount(game.path());
    files.mount(engine.path());

    const auto located = files.locate("bg/room.png");

    REQUIRE_FALSE(located.has_value());
    trace("{}", located.error());

    CHECK(located.error().code == ErrorCode::FILE_NOT_FOUND);
    CHECK(located.error().message.find("bg/room.png") != std::string::npos);

    // Without the list, "asset not found" leaves the reader guessing which
    // directory the engine actually looked in — the one thing they need.
    //
    // Compared against the roots the file system reports, not against the paths
    // handed to mount, and rendered with the same function the message uses. A
    // root is canonicalised when it is mounted, and the two spellings of one
    // directory are genuinely different strings: on Windows the temporary
    // directory arrives in its short 8.3 form (C:\Users\RUNNER~1\...) and comes
    // back expanded (C:\Users\runneradmin\...). Comparing to what was passed in
    // only worked because on Linux canonicalising /tmp changes nothing.
    REQUIRE(files.roots().size() == 2);
    CHECK(located.error().message.find(path_to_utf8(files.roots()[0])) != std::string::npos);
    CHECK(located.error().message.find(path_to_utf8(files.roots()[1])) != std::string::npos);

    CHECK_FALSE(files.exists("bg/room.png"));
}

TEST_CASE("a directory is not an asset", "[assets][virtual_file_system]")
{
    const TemporaryDirectory game;
    game.write("sprites/alice.png", "pixels");

    VirtualFileSystem files;
    files.mount(game.path());

    CHECK_FALSE(files.exists("sprites"));
}

TEST_CASE("an unusable path fails before the disk is touched",
          "[assets][virtual_file_system]")
{
    VirtualFileSystem files;

    const auto located = files.locate("../saves/slot_01.save");

    REQUIRE_FALSE(located.has_value());
    trace("{}", located.error());

    // Not FILE_NOT_FOUND: nothing was looked for. The path itself is the fault,
    // and it would be the same fault with every root mounted.
    CHECK(located.error().code == ErrorCode::INVALID_FORMAT);
}

TEST_CASE("nothing to report leaves the summary empty", "[assets][virtual_file_system]")
{
    const TemporaryDirectory game;
    game.write("sprites/alice.png", "pixels");

    VirtualFileSystem files;
    files.set_path_audit(true);
    files.mount(game.path());

    REQUIRE(files.read("sprites/alice.png").has_value());

    CHECK(files.case_mismatches().empty());
    CHECK(files.format_case_mismatch_summary().empty());
}

TEST_CASE("a name that differs only in case is reported either way",
          "[assets][virtual_file_system]")
{
    const TemporaryDirectory game;
    game.write("Sprites/Alice.png", "pixels");

    VirtualFileSystem files;
    files.set_path_audit(true);
    files.mount(game.path());

    const auto located = files.locate("sprites/alice.png");

    // The outcome depends on the file system, and both are the same mistake. On
    // Windows and macOS the file is found and the audit warns about the machine
    // this game will fail on; on Linux it is not found and the audit explains why
    // the load failed. Either way it is recorded, and neither is allowed to pass
    // silently — which is what this case is really asserting.
    if (located.has_value())
    {
        trace_step("case-insensitive file system: loaded, and warned about");
        REQUIRE(files.case_mismatches().size() == 1);
        CHECK(files.case_mismatches()[0].outcome == CaseMismatchOutcome::LOADED_ANYWAY);
    }
    else
    {
        trace_step("case-sensitive file system: not loaded, and told why");
        trace("{}", located.error());

        CHECK(located.error().code == ErrorCode::FILE_NOT_FOUND);
        CHECK(located.error().message.find("ASSET NOT LOADED") != std::string::npos);
        CHECK(located.error().message.find("Alice.png") != std::string::npos);

        REQUIRE(files.case_mismatches().size() == 1);
        CHECK(files.case_mismatches()[0].outcome == CaseMismatchOutcome::NOT_FOUND);
    }

    const std::string summary = files.format_case_mismatch_summary();
    trace("{}", summary);

    CHECK(summary.find("sprites/alice.png") != std::string::npos);
    CHECK(summary.find("Alice.png") != std::string::npos);

    trace_step("asked for a second time, it is not recorded twice");
    static_cast<void>(files.locate("sprites/alice.png"));
    CHECK(files.case_mismatches().size() == 1);
}

TEST_CASE("turning the audit off does not silence a failed load",
          "[assets][virtual_file_system]")
{
    const TemporaryDirectory game;
    game.write("bg/Room.png", "pixels");

    VirtualFileSystem files;
    files.set_path_audit(false);
    files.mount(game.path());

    CHECK(files.path_audit() == false);

    const auto located = files.locate("bg/room.png");

    // The setting governs the scan that every successful load would pay for. A
    // load that already failed costs nothing to explain, so the explanation is
    // not something a release build gives up.
    if (!located.has_value())
    {
        CHECK(located.error().message.find("Room.png") != std::string::npos);
        CHECK(files.case_mismatches().size() == 1);
    }
    else
    {
        trace_step("case-insensitive file system: found, and not audited");
        CHECK(files.case_mismatches().empty());
    }
}

TEST_CASE("the audit defaults to the build type", "[assets][virtual_file_system]")
{
    const VirtualFileSystem files;

    trace("path audit defaults to {}", DEFAULT_PATH_AUDIT);
    CHECK(files.path_audit() == DEFAULT_PATH_AUDIT);
}
