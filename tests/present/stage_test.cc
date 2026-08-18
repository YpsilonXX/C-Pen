#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "cpen/present/stage.hh"
#include "cpen/script/command_sink.hh"
#include "support/log_capture.hh"

#include <string>
#include <string_view>

using cpen::present::Anchor;
using cpen::present::Stage;
using cpen::present::anchor_from_name;
using cpen::present::layer_of;
using cpen::present::placement_of;
using cpen::present::position_of;
using cpen::script::HideCommand;
using cpen::script::MenuCommand;
using cpen::script::SayCommand;
using cpen::script::SceneCommand;
using cpen::script::ScreenPosition;
using cpen::script::ShowCommand;

namespace
{
    /// Turns the typewriter off, which is what every test that is not about the
    /// typewriter wants: a line is then complete the moment it is said.
    ///
    /// A function taking a reference rather than one returning a configured
    /// Stage, because a sink is neither copyable nor movable — the machine holds
    /// one by reference for its whole run, and a sink that could be moved out
    /// from under it would be a way to lose commands.
    void reveal_at_once(Stage& stage)
    {
        stage.set_reveal_speed(0.0);
    }

    ShowCommand show_at(std::string asset, std::string anchor)
    {
        return ShowCommand{.asset = std::move(asset), .anchor = std::move(anchor)};
    }
}

TEST_CASE("an anchor round-trips through its written name", "[present]")
{
    for (const Anchor anchor : {Anchor::OFFSCREEN_LEFT, Anchor::FAR_LEFT, Anchor::LEFT,
                                Anchor::CENTER, Anchor::RIGHT, Anchor::FAR_RIGHT,
                                Anchor::OFFSCREEN_RIGHT})
    {
        REQUIRE(anchor_from_name(to_string(anchor)) == anchor);
    }
}

TEST_CASE("centre and center are the same place", "[present]")
{
    REQUIRE(anchor_from_name("centre") == Anchor::CENTER);
    REQUIRE(anchor_from_name("center") == Anchor::CENTER);

    // Only one of them comes back out, so a log line or a save file has one
    // spelling rather than whichever the author happened to type.
    REQUIRE(to_string(Anchor::CENTER) == "center");
}

TEST_CASE("an unknown word is not an anchor", "[present]")
{
    REQUIRE_FALSE(anchor_from_name("stage_left").has_value());
    REQUIRE_FALSE(anchor_from_name("").has_value());
    REQUIRE_FALSE(anchor_from_name("LEFT").has_value());
}

TEST_CASE("every anchor stands on the floor line, left to right", "[present]")
{
    REQUIRE(position_of(Anchor::CENTER).x == 0.5);

    REQUIRE(position_of(Anchor::OFFSCREEN_LEFT).x < 0.0);
    REQUIRE(position_of(Anchor::FAR_LEFT).x < position_of(Anchor::LEFT).x);
    REQUIRE(position_of(Anchor::LEFT).x < position_of(Anchor::CENTER).x);
    REQUIRE(position_of(Anchor::CENTER).x < position_of(Anchor::RIGHT).x);
    REQUIRE(position_of(Anchor::RIGHT).x < position_of(Anchor::FAR_RIGHT).x);
    REQUIRE(position_of(Anchor::OFFSCREEN_RIGHT).x > 1.0);

    for (const Anchor anchor : {Anchor::LEFT, Anchor::CENTER, Anchor::RIGHT})
    {
        REQUIRE(position_of(anchor).y == 1.0);
    }
}

TEST_CASE("a layer is the asset name up to the first separator", "[present]")
{
    REQUIRE(layer_of("alice/happy") == "alice");
    REQUIRE(layer_of("alice") == "alice");
    REQUIRE(layer_of("alice/happy/blinking") == "alice");
    REQUIRE(layer_of("") == "");
}

TEST_CASE("a scene changes the background", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.scene(SceneCommand{.background = "room", .transition = "fade"});

    REQUIRE(stage.background() == "room");
    REQUIRE(stage.background_transition() == "fade");
}

TEST_CASE("showing a sprite puts it on its own layer", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.show(show_at("alice/happy", "left"));
    stage.show(show_at("bob/idle", "right"));

    REQUIRE(stage.sprites().size() == 2);
    REQUIRE(stage.sprites()[0].layer == "alice");
    REQUIRE(stage.sprites()[1].layer == "bob");
    REQUIRE(stage.sprite("alice") != nullptr);
    REQUIRE(stage.sprite("bob")->asset == "bob/idle");
    REQUIRE(stage.sprite("carol") == nullptr);
}

TEST_CASE("changing an expression keeps the sprite where it was in the order", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.show(show_at("alice/happy", "left"));
    stage.show(show_at("bob/idle", "right"));
    stage.show(show_at("alice/sad", "left"));

    REQUIRE(stage.sprites().size() == 2);

    // Drawing order is the order of this vector, and Alice must not step in front
    // of Bob merely because she changed her face.
    REQUIRE(stage.sprites()[0].asset == "alice/sad");
    REQUIRE(stage.sprites()[1].asset == "bob/idle");
}

TEST_CASE("hiding takes the whole layer off, whatever it is showing", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.show(show_at("alice/happy", "left"));
    stage.show(show_at("bob/idle", "right"));
    stage.hide(HideCommand{.name = "alice"});

    REQUIRE(stage.sprites().size() == 1);
    REQUIRE(stage.sprites()[0].layer == "bob");
    REQUIRE(stage.sprite("alice") == nullptr);
}

TEST_CASE("hiding what is not there is reported and changes nothing", "[present]")
{
    Stage stage;
    reveal_at_once(stage);
    const cpen::test::LogCaptureGuard capture;

    stage.show(show_at("alice/happy", "left"));
    stage.hide(HideCommand{.name = "carol"});

    REQUIRE(stage.sprites().size() == 1);
    REQUIRE(capture.count(cpen::log::Level::WARN) == 1);
}

TEST_CASE("coordinates win over an anchor and are kept exactly", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.show(ShowCommand{
        .asset = "alice/happy",
        .anchor = "left",
        .position = ScreenPosition{.x = 0.3, .y = 0.9},
    });

    const ScreenPosition placed = placement_of(*stage.sprite("alice"));

    REQUIRE(placed.x == 0.3);
    REQUIRE(placed.y == 0.9);
}

TEST_CASE("an anchor is resolved late, not stored as a number", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.show(show_at("alice/happy", "right"));

    // The word the author wrote is still there, which is what lets a layout
    // change reinterpret it.
    REQUIRE(stage.sprite("alice")->anchor == "right");
    REQUIRE_FALSE(stage.sprite("alice")->position.has_value());

    REQUIRE(placement_of(*stage.sprite("alice")).x == position_of(Anchor::RIGHT).x);
}

TEST_CASE("an unknown anchor is reported once and stands at the centre", "[present]")
{
    Stage stage;
    reveal_at_once(stage);
    const cpen::test::LogCaptureGuard capture;

    stage.show(show_at("alice/happy", "stage_left"));

    REQUIRE(capture.count(cpen::log::Level::WARN) == 1);
    REQUIRE(placement_of(*stage.sprite("alice")).x == position_of(Anchor::CENTER).x);

    // Resolving it again is what every drawn frame does, and it must not add a
    // second copy of the same complaint.
    static_cast<void>(placement_of(*stage.sprite("alice")));
    REQUIRE(capture.count(cpen::log::Level::WARN) == 1);
}

TEST_CASE("a sprite placed with no anchor at all stands at the centre", "[present]")
{
    Stage stage;
    reveal_at_once(stage);
    const cpen::test::LogCaptureGuard capture;

    stage.show(ShowCommand{.asset = "alice/happy"});

    REQUIRE(placement_of(*stage.sprite("alice")).x == position_of(Anchor::CENTER).x);

    // No anchor is not a wrong anchor: the author said nothing, so there is
    // nothing to complain about.
    REQUIRE(capture.count(cpen::log::Level::WARN) == 0);
}

TEST_CASE("a line is held whole and its speaker with it", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    REQUIRE_FALSE(stage.line().has_value());

    stage.say(SayCommand{.speaker = "Алиса", .text = "Привет."});

    REQUIRE(stage.line()->speaker == "Алиса");
    REQUIRE(stage.line()->text == "Привет.");
    REQUIRE(stage.revealed_text() == "Привет.");
}

TEST_CASE("narration has no speaker", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.say(SayCommand{.text = "Шёл дождь."});

    REQUIRE_FALSE(stage.line()->speaker.has_value());
}

TEST_CASE("a line types itself out one code point at a time", "[present]")
{
    Stage stage;
    stage.set_reveal_speed(10.0);

    stage.say(SayCommand{.text = "Привет"});

    REQUIRE(stage.revealed_text().empty());
    REQUIRE_FALSE(stage.reveal_complete());

    // A tenth of a second is one code point, and a code point of this text is two
    // bytes: a typewriter that counted bytes would show half of a letter here.
    stage.advance_reveal(0.1);
    REQUIRE(stage.revealed_text() == "П");

    stage.advance_reveal(0.2);
    REQUIRE(stage.revealed_text() == "При");
    REQUIRE_FALSE(stage.reveal_complete());

    stage.advance_reveal(1.0);
    REQUIRE(stage.revealed_text() == "Привет");
    REQUIRE(stage.reveal_complete());
}

TEST_CASE("the typewriter never runs past the end of the line", "[present]")
{
    Stage stage;
    stage.set_reveal_speed(10.0);

    stage.say(SayCommand{.text = "да"});
    stage.advance_reveal(100.0);

    REQUIRE(stage.revealed_text() == "да");
    REQUIRE(stage.reveal_complete());
}

TEST_CASE("finishing the line is what the reader's first click does", "[present]")
{
    Stage stage;
    stage.set_reveal_speed(10.0);

    stage.say(SayCommand{.text = "Привет"});
    stage.advance_reveal(0.1);
    stage.complete_reveal();

    REQUIRE(stage.revealed_text() == "Привет");
    REQUIRE(stage.reveal_complete());
}

TEST_CASE("a new line restarts the typewriter", "[present]")
{
    Stage stage;
    stage.set_reveal_speed(10.0);

    stage.say(SayCommand{.text = "Первая"});
    stage.complete_reveal();

    stage.say(SayCommand{.text = "Вторая"});

    REQUIRE(stage.revealed_text().empty());
    REQUIRE_FALSE(stage.reveal_complete());
}

TEST_CASE("a reveal speed of zero puts the whole line up at once", "[present]")
{
    Stage stage;
    stage.set_reveal_speed(0.0);

    stage.say(SayCommand{.text = "Мгновенно"});

    REQUIRE(stage.revealed_text() == "Мгновенно");
    REQUIRE(stage.reveal_complete());
}

TEST_CASE("an empty line is complete the moment it is said", "[present]")
{
    Stage stage;
    stage.set_reveal_speed(10.0);

    stage.say(SayCommand{.text = ""});

    REQUIRE(stage.reveal_complete());
    REQUIRE(stage.revealed_text().empty());
}

TEST_CASE("nothing said yet counts as nothing left to reveal", "[present]")
{
    const Stage stage;

    REQUIRE(stage.reveal_complete());
    REQUIRE(stage.revealed_text().empty());
}

TEST_CASE("a menu is offered until the choice is taken", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    REQUIRE_FALSE(stage.has_menu());

    stage.offer(MenuCommand{.prompts = {"Да", "Нет"}});

    REQUIRE(stage.has_menu());
    REQUIRE(stage.choices().size() == 2);
    REQUIRE(stage.choices()[0] == "Да");

    stage.close_menu();

    REQUIRE_FALSE(stage.has_menu());
    REQUIRE(stage.choices().empty());
}

TEST_CASE("a menu finishes the line it interrupts", "[present]")
{
    Stage stage;
    stage.set_reveal_speed(10.0);

    stage.say(SayCommand{.text = "Что дальше?"});
    stage.advance_reveal(0.1);
    stage.offer(MenuCommand{.prompts = {"Идти", "Ждать"}});

    // Choosing between options while the question is still appearing means
    // answering something that has not been asked.
    REQUIRE(stage.reveal_complete());
    REQUIRE(stage.revealed_text() == "Что дальше?");
}

TEST_CASE("the line stays on screen under the menu", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.say(SayCommand{.speaker = "Алиса", .text = "Что дальше?"});
    stage.offer(MenuCommand{.prompts = {"Идти"}});

    REQUIRE(stage.line().has_value());
    REQUIRE(stage.line()->text == "Что дальше?");
}

TEST_CASE("clearing empties the whole stage", "[present]")
{
    Stage stage;
    reveal_at_once(stage);

    stage.scene(SceneCommand{.background = "room"});
    stage.show(show_at("alice/happy", "left"));
    stage.say(SayCommand{.text = "Привет."});
    stage.offer(MenuCommand{.prompts = {"Да"}});

    stage.clear();

    REQUIRE(stage.background().empty());
    REQUIRE(stage.sprites().empty());
    REQUIRE_FALSE(stage.line().has_value());
    REQUIRE_FALSE(stage.has_menu());
    REQUIRE(stage.revealed_text().empty());
}
