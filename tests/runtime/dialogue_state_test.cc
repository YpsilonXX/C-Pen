#include <catch2/catch_test_macros.hpp>

#include "cpen/assets/asset_manager.hh"
#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/blackboard.hh"
#include "cpen/core/value.hh"
#include "cpen/core/event_bus.hh"
#include "cpen/present/layout.hh"
#include "cpen/present/stage.hh"
#include "cpen/render/renderer.hh"
#include "cpen/render/viewport.hh"
#include "cpen/runtime/dialogue_state.hh"
#include "cpen/runtime/game_context.hh"
#include "cpen/runtime/state_stack.hh"
#include "cpen/script/virtual_machine.hh"
#include "support/log_capture.hh"
#include "support/temporary_directory.hh"
#include "support/value_printing.hh"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

using cpen::assets::AssetManager;
using cpen::assets::AssetResolver;
using cpen::assets::VirtualFileSystem;
using cpen::core::Blackboard;
using cpen::core::EventBus;
using cpen::platform::InputAction;
using cpen::platform::Key;
using cpen::platform::KeyEvent;
using cpen::platform::MouseButton;
using cpen::platform::MouseButtonEvent;
using cpen::platform::MouseMoveEvent;
using cpen::present::choice_rectangles;
using cpen::render::Renderer;
using cpen::render::Viewport;
using cpen::runtime::DialogueState;
using cpen::runtime::GameContext;
using cpen::runtime::StateStack;
using cpen::script::YieldStatus;
using cpen::test::LogCaptureGuard;
using cpen::test::TemporaryDirectory;

namespace
{
    /// A game, played with no window at all.
    ///
    /// The whole point of the split the script layer was built around: a renderer
    /// that cannot draw is still a renderer, so the state runs its story, answers
    /// clicks and finishes exactly as it would on screen, and nothing in the test
    /// needs a driver.
    struct Game
    {
        TemporaryDirectory directory;
        VirtualFileSystem files;
        AssetResolver resolver;
        AssetManager assets;
        EventBus event_bus;
        Blackboard blackboard;
        Renderer renderer;
        GameContext context;
        StateStack stack;

        Game()
            : resolver(files), assets(files, resolver),
              renderer(Viewport{}),
              context(GameContext{
                  .blackboard = blackboard,
                  .event_bus = event_bus,
                  .renderer = renderer,
                  .assets = assets,
              }),
              stack(context)
        {
            this->files.mount(this->directory.path());

            // The window a real game would have opened. Without it the viewport
            // has no mapping and every click would land at the origin.
            this->renderer.resize(1920, 1080);
        }

        void write_script(const std::string_view identifier, const std::string_view source)
        {
            this->directory.write(std::string{"script/"} + std::string{identifier} + ".pen",
                                  source);
        }

        /// Pushes a dialogue over the script and enters it.
        DialogueState& play(DialogueState::Config settings)
        {
            this->stack.push(std::make_unique<DialogueState>(std::move(settings)));
            this->stack.apply_pending();

            return *static_cast<DialogueState*>(this->stack.top());
        }

        /// The common case: a story with the typewriter turned off.
        DialogueState& play(const std::string_view identifier)
        {
            return this->play(DialogueState::Config{
                .script = std::string{identifier},
                .reveal_speed = 0.0,
            });
        }
    };

    KeyEvent press(const Key key)
    {
        return KeyEvent{.key = key, .action = InputAction::PRESS};
    }

    MouseButtonEvent click_at(const glm::vec2& point)
    {
        return MouseButtonEvent{
            .button = MouseButton::LEFT,
            .action = InputAction::PRESS,
            .x = point.x,
            .y = point.y,
        };
    }

    /// The middle of a choice's panel, which is where a reader aims.
    glm::vec2 centre_of_choice(const DialogueState& state, const std::size_t index)
    {
        const auto rectangles = choice_rectangles(cpen::present::DialogueTheme{},
                                                  glm::vec2{1920.0f, 1080.0f},
                                                  state.stage().choices().size());

        return rectangles[index].position + rectangles[index].size * 0.5f;
    }

    constexpr std::string_view TWO_LINES =
        "\"Первая.\"\n"
        "alice \"Вторая.\"\n";
}

TEST_CASE("a story starts on its first line", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro", TWO_LINES);

    const DialogueState& state = game.play("intro");

    REQUIRE(state.failure().empty());
    REQUIRE(state.stage().line().has_value());
    CHECK(state.stage().line()->text == "Первая.");
    CHECK_FALSE(state.stage().line()->speaker.has_value());
    CHECK(state.machine()->status() == YieldStatus::WAITING_FOR_ADVANCE);
}

TEST_CASE("a click takes the reader to the next line", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro", TWO_LINES);

    DialogueState& state = game.play("intro");

    REQUIRE(state.handle_event(click_at({960.0f, 540.0f})));

    CHECK(state.stage().line()->text == "Вторая.");
    CHECK(state.stage().line()->speaker == "alice");
}

TEST_CASE("space and enter do what a click does", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro", TWO_LINES);

    DialogueState& state = game.play("intro");

    REQUIRE(state.handle_event(press(Key::SPACE)));
    CHECK(state.stage().line()->text == "Вторая.");
}

TEST_CASE("the first click finishes the line and the second goes on",
          "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro", TWO_LINES);

    DialogueState& state = game.play(DialogueState::Config{
        .script = "intro",
        .reveal_speed = 10.0,
    });

    state.update(0.1);
    REQUIRE_FALSE(state.stage().reveal_complete());

    REQUIRE(state.handle_event(click_at({960.0f, 540.0f})));

    // The line is finished, and the story has not moved: a reader who reads
    // faster than the typewriter should not lose a line to it.
    CHECK(state.stage().reveal_complete());
    CHECK(state.stage().line()->text == "Первая.");

    REQUIRE(state.handle_event(click_at({960.0f, 540.0f})));
    CHECK(state.stage().line()->text == "Вторая.");
}

TEST_CASE("a story that runs out pops itself off the stack", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro", "\"Одна строка.\"\n");

    DialogueState& state = game.play("intro");

    REQUIRE(state.handle_event(press(Key::SPACE)));

    CHECK(state.machine()->status() == YieldStatus::FINISHED);

    // Queued rather than immediate: a state that destroyed itself inside its own
    // event handler would be running in freed memory.
    REQUIRE(game.stack.has_pending());
    game.stack.apply_pending();
    CHECK(game.stack.empty());
}

TEST_CASE("the stage carries what the script put on it", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "scene room with fade\n"
                      "show alice happy at left\n"
                      "\"Привет.\"\n");

    const DialogueState& state = game.play("intro");

    CHECK(state.stage().background() == "room");
    CHECK(state.stage().background_transition() == "fade");
    REQUIRE(state.stage().sprite("alice") != nullptr);
    CHECK(state.stage().sprite("alice")->asset == "alice/happy");
    CHECK(state.stage().sprite("alice")->anchor == "left");
}

TEST_CASE("a pause is waited out by the clock, not by the reader", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "\"Первая.\"\n"
                      "pause 0.5\n"
                      "\"Вторая.\"\n");

    DialogueState& state = game.play("intro");

    REQUIRE(state.handle_event(press(Key::SPACE)));
    REQUIRE(state.machine()->status() == YieldStatus::WAITING_FOR_TIME);

    state.update(0.2);
    CHECK(state.machine()->status() == YieldStatus::WAITING_FOR_TIME);
    CHECK(state.stage().line()->text == "Первая.");

    state.update(0.4);
    CHECK(state.stage().line()->text == "Вторая.");
}

TEST_CASE("a click does not skip a pause", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro", "\"Первая.\"\npause 5\n\"Вторая.\"\n");

    DialogueState& state = game.play("intro");

    REQUIRE(state.handle_event(press(Key::SPACE)));

    CHECK_FALSE(state.handle_event(click_at({960.0f, 540.0f})));
    CHECK(state.machine()->status() == YieldStatus::WAITING_FOR_TIME);
}

TEST_CASE("a menu is offered and answered by clicking it", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "menu:\n"
                      "\t\"Поздороваться\":\n"
                      "\t\t\"Ты поздоровался.\"\n"
                      "\t\"Промолчать\":\n"
                      "\t\t\"Ты промолчал.\"\n");

    DialogueState& state = game.play("intro");

    REQUIRE(state.stage().has_menu());
    REQUIRE(state.stage().choices().size() == 2);
    CHECK(state.machine()->status() == YieldStatus::WAITING_FOR_CHOICE);

    REQUIRE(state.handle_event(click_at(centre_of_choice(state, 1))));

    CHECK_FALSE(state.stage().has_menu());
    CHECK(state.stage().line()->text == "Ты промолчал.");
}

TEST_CASE("a number key answers a menu", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "menu:\n"
                      "\t\"Первый\":\n"
                      "\t\t\"Взял первый.\"\n"
                      "\t\"Второй\":\n"
                      "\t\t\"Взял второй.\"\n");

    DialogueState& state = game.play("intro");

    REQUIRE(state.handle_event(press(Key::DIGIT_2)));
    CHECK(state.stage().line()->text == "Взял второй.");
}

TEST_CASE("a number nobody offered answers nothing", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "menu:\n"
                      "\t\"Первый\":\n"
                      "\t\t\"Взял первый.\"\n");

    DialogueState& state = game.play("intro");

    CHECK_FALSE(state.handle_event(press(Key::DIGIT_5)));
    CHECK(state.stage().has_menu());
}

TEST_CASE("a click beside the menu is swallowed and answers nothing",
          "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "menu:\n"
                      "\t\"Первый\":\n"
                      "\t\t\"Взял первый.\"\n");

    DialogueState& state = game.play("intro");

    CHECK_FALSE(state.handle_event(click_at({10.0f, 10.0f})));
    CHECK(state.stage().has_menu());
}

TEST_CASE("the arrows move a highlight and enter takes it", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "menu:\n"
                      "\t\"Первый\":\n"
                      "\t\t\"Взял первый.\"\n"
                      "\t\"Второй\":\n"
                      "\t\t\"Взял второй.\"\n");

    DialogueState& state = game.play("intro");

    // Nothing is picked out until the reader says so: a menu that opens with an
    // answer already highlighted invites taking it by accident.
    REQUIRE_FALSE(state.highlighted_choice().has_value());

    REQUIRE(state.handle_event(press(Key::DOWN)));
    CHECK(state.highlighted_choice() == 0u);

    REQUIRE(state.handle_event(press(Key::DOWN)));
    CHECK(state.highlighted_choice() == 1u);

    // Wrapping at the end rather than stopping there.
    REQUIRE(state.handle_event(press(Key::DOWN)));
    CHECK(state.highlighted_choice() == 0u);

    REQUIRE(state.handle_event(press(Key::UP)));
    CHECK(state.highlighted_choice() == 1u);

    REQUIRE(state.handle_event(press(Key::ENTER)));
    CHECK(state.stage().line()->text == "Взял второй.");
}

TEST_CASE("space does not answer a menu nobody has moved through",
          "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "menu:\n"
                      "\t\"Первый\":\n"
                      "\t\t\"Взял первый.\"\n");

    DialogueState& state = game.play("intro");

    CHECK_FALSE(state.handle_event(press(Key::SPACE)));
    CHECK(state.stage().has_menu());
}

TEST_CASE("the pointer lights the choice it is over", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "menu:\n"
                      "\t\"Первый\":\n"
                      "\t\t\"Взял первый.\"\n"
                      "\t\"Второй\":\n"
                      "\t\t\"Взял второй.\"\n");

    DialogueState& state = game.play("intro");

    const glm::vec2 over = centre_of_choice(state, 1);

    // Moving a pointer is not an answer, so it travels on down the stack.
    CHECK_FALSE(state.handle_event(MouseMoveEvent{.x = over.x, .y = over.y}));
    CHECK(state.highlighted_choice() == 1u);

    CHECK_FALSE(state.handle_event(MouseMoveEvent{.x = 4.0, .y = 4.0}));
    CHECK_FALSE(state.highlighted_choice().has_value());
}

TEST_CASE("a choice runs its own block and the story goes on after the menu",
          "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "$ score = 0\n"
                      "menu:\n"
                      "\t\"Взять\":\n"
                      "\t\t$ score = score + 1\n"
                      "\t\"Оставить\":\n"
                      "\t\t$ score = score - 1\n"
                      "\"Дальше.\"\n");

    DialogueState& state = game.play("intro");

    REQUIRE(state.handle_event(press(Key::DIGIT_1)));

    CHECK(state.stage().line()->text == "Дальше.");

    // Variables live in the blackboard, which is what lets a mini-game or a C++
    // behaviour read exactly what the script wrote.
    CHECK(game.blackboard.get("score") == cpen::core::Value{std::int64_t{1}});
}

TEST_CASE("a story can be started at a label", "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro",
                      "\"Это не играется.\"\n"
                      "\n"
                      "label chapter_two:\n"
                      "\t\"Вторая глава.\"\n");

    const DialogueState& state = game.play(DialogueState::Config{
        .script = "intro",
        .label = "chapter_two",
        .reveal_speed = 0.0,
    });

    CHECK(state.stage().line()->text == "Вторая глава.");
}

TEST_CASE("a label nobody wrote is a failure the reader can see", "[runtime][dialogue]")
{
    Game game;
    const LogCaptureGuard capture;
    game.write_script("intro", "\"Одна строка.\"\n");

    const DialogueState& state = game.play(DialogueState::Config{
        .script = "intro",
        .label = "nowhere",
        .reveal_speed = 0.0,
    });

    CHECK(state.failure().find("nowhere") != std::string::npos);
}

TEST_CASE("a script that will not compile is shown, not only logged",
          "[runtime][dialogue]")
{
    Game game;
    const LogCaptureGuard capture;
    game.write_script("broken", "label start:\n\tif :\n\t\t\"one\"\n");

    const DialogueState& state = game.play("broken");

    CHECK_FALSE(state.failure().empty());
    CHECK(state.failure().find("script/broken.pen") != std::string::npos);
    CHECK(state.machine() == nullptr);
}

TEST_CASE("a script that is not there is shown, not only logged", "[runtime][dialogue]")
{
    Game game;
    const LogCaptureGuard capture;

    const DialogueState& state = game.play("nowhere");

    CHECK_FALSE(state.failure().empty());
    CHECK(state.machine() == nullptr);
}

TEST_CASE("a fault names the line that was executing", "[runtime][dialogue]")
{
    Game game;
    const LogCaptureGuard capture;

    game.write_script("intro",
                      "\"Первая.\"\n"
                      "$ broken = 1 / 0\n");

    DialogueState& state = game.play("intro");

    REQUIRE(state.handle_event(press(Key::SPACE)));

    CHECK(state.machine()->status() == YieldStatus::FAULTED);
    CHECK_FALSE(state.failure().empty());

    // Rendered as a diagnostic: the file, the line, and a caret under what was
    // running. A fault that only said "instruction 214" would tell an author
    // nothing.
    CHECK(state.failure().find("script/intro.pen") != std::string::npos);
    CHECK(state.failure().find('^') != std::string::npos);
}

TEST_CASE("nothing answers a story that has failed", "[runtime][dialogue]")
{
    Game game;
    const LogCaptureGuard capture;
    game.write_script("broken", "label start:\n\tif :\n\t\t\"one\"\n");

    DialogueState& state = game.play("broken");

    CHECK_FALSE(state.handle_event(press(Key::SPACE)));
    CHECK_FALSE(state.handle_event(click_at({960.0f, 540.0f})));

    // The stage was emptied with the failure: leaving the scene under the message
    // would suggest the story is still running.
    CHECK_FALSE(state.stage().line().has_value());
    CHECK(state.stage().sprites().empty());
}

TEST_CASE("rendering without a graphics context does nothing at all",
          "[runtime][dialogue]")
{
    Game game;
    game.write_script("intro", TWO_LINES);

    DialogueState& state = game.play("intro");

    // The claim the whole suite rests on: this is the same code path a real
    // window drives, minus the drawing.
    REQUIRE_FALSE(game.renderer.can_draw());
    state.render();

    CHECK(state.stage().line()->text == "Первая.");
}
