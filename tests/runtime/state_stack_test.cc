#include <catch2/catch_test_macros.hpp>

#include "cpen/assets/asset_manager.hh"
#include "cpen/assets/asset_resolver.hh"
#include "cpen/assets/virtual_file_system.hh"
#include "cpen/core/blackboard.hh"
#include "cpen/core/event_bus.hh"
#include "cpen/render/renderer.hh"
#include "cpen/runtime/game_context.hh"
#include "cpen/runtime/game_state.hh"
#include "cpen/runtime/state_stack.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using cpen::core::Blackboard;
using cpen::core::EventBus;
using cpen::platform::CloseEvent;
using cpen::platform::Event;
using cpen::render::Renderer;
using cpen::render::Viewport;
using cpen::assets::AssetManager;
using cpen::assets::AssetResolver;
using cpen::assets::VirtualFileSystem;
using cpen::runtime::GameContext;
using cpen::runtime::GameState;
using cpen::runtime::StateStack;
using cpen::test::trace;

namespace
{
    /// Records every callback it receives into a shared journal, so a test can
    /// assert on the exact sequence rather than on a count.
    class RecordingState final : public GameState
    {
    public:
        RecordingState(std::string state_name, std::vector<std::string>& shared_journal)
            : identity(std::move(state_name)), journal(&shared_journal)
        {
        }

        std::string_view name() const override { return this->identity; }

        void on_enter() override { this->record("enter"); }
        void on_exit() override { this->record("exit"); }
        void on_pause() override { this->record("pause"); }
        void on_resume() override { this->record("resume"); }

        void update(double) override { this->record("update"); }
        void render() override { this->record("render"); }

        bool handle_event(const Event&) override
        {
            this->record("event");
            return this->consumes_events;
        }

        bool blocks_update_below() const override { return this->blocks_update; }
        bool blocks_render_below() const override { return this->blocks_render; }
        bool blocks_input_below() const override { return this->blocks_input; }

        bool consumes_events = false;
        bool blocks_update = true;
        bool blocks_render = true;
        bool blocks_input = true;

    private:
        void record(const std::string_view what)
        {
            this->journal->push_back(this->identity + ":" + std::string(what));
        }

        std::string identity;
        std::vector<std::string>* journal = nullptr;
    };

    /// A stack over nothing but the shared services: no window, no GL context.
    /// Keeping the runtime layer testable this way is the reason the stack knows
    /// only about GameContext.
    struct Fixture
    {
        /// Declared before the stack, and that order is load-bearing. StateStack's
        /// destructor calls on_exit() on whatever is still on it, and those states
        /// write here; members are destroyed in reverse order of declaration, so
        /// the journal has to be declared first to still be alive at that point.
        /// The same rule is why Application declares its state stack last.
        std::vector<std::string> journal;

        EventBus event_bus;
        Blackboard blackboard{event_bus};

        /// A renderer with a coordinate system and no means to draw, which is why
        /// this file compiles into the suite that runs without a driver. These
        /// states render nothing; what they exercise is the order the stack calls
        /// them in, and that has nothing to do with a window.
        Renderer renderer{Viewport{}};

        /// An asset manager over a file system with nothing mounted. Every load
        /// through it fails, which is exactly right here: these states load
        /// nothing, and the manager is present only because GameContext carries
        /// one. That it can be built at all without a window is the invariant the
        /// assets layer keeps for the sake of this file.
        VirtualFileSystem files;
        AssetResolver asset_resolver{files};
        AssetManager assets{files, asset_resolver};

        GameContext context{
            .blackboard = blackboard,
            .event_bus = event_bus,
            .renderer = renderer,
            .assets = assets,
        };
        StateStack stack{context};

        std::unique_ptr<RecordingState> state(std::string state_name)
        {
            return std::make_unique<RecordingState>(std::move(state_name), this->journal);
        }

        void dump()
        {
            for (const std::string& entry : this->journal)
            {
                trace("{}", entry);
            }
        }
    };
}

TEST_CASE("a push takes effect only at the synchronisation point", "[runtime][state_stack]")
{
    Fixture fixture;
    fixture.stack.push(fixture.state("first"));

    trace("right after push(): size={}, has_pending={}", fixture.stack.size(),
          fixture.stack.has_pending());
    CHECK(fixture.stack.size() == 0);
    CHECK(fixture.stack.has_pending());
    CHECK(fixture.journal.empty());

    fixture.stack.apply_pending();

    trace("after apply_pending(): size={}, has_pending={}", fixture.stack.size(),
          fixture.stack.has_pending());
    fixture.dump();
    CHECK(fixture.stack.size() == 1);
    CHECK_FALSE(fixture.stack.has_pending());
    CHECK(fixture.journal == std::vector<std::string>{"first:enter"});
    CHECK(fixture.stack.top()->name() == "first");
}

TEST_CASE("pushing pauses the state below, popping resumes it", "[runtime][state_stack]")
{
    Fixture fixture;
    fixture.stack.push(fixture.state("bottom"));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.push(fixture.state("top"));
    fixture.stack.apply_pending();
    trace("after pushing 'top':");
    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"bottom:pause", "top:enter"});

    fixture.journal.clear();
    fixture.stack.pop();
    fixture.stack.apply_pending();
    trace("after popping 'top':");
    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"top:exit", "bottom:resume"});
    CHECK(fixture.stack.size() == 1);
}

TEST_CASE("replace swaps the top without disturbing the state below", "[runtime][state_stack]")
{
    Fixture fixture;
    fixture.stack.push(fixture.state("bottom"));
    fixture.stack.push(fixture.state("old top"));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.replace(fixture.state("new top"));
    fixture.stack.apply_pending();

    trace("bottom stays covered throughout, so it is neither resumed nor paused:");
    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"old top:exit", "new top:enter"});
    CHECK(fixture.stack.size() == 2);
    CHECK(fixture.stack.top()->name() == "new top");
}

TEST_CASE("clear removes every state from the top down", "[runtime][state_stack]")
{
    Fixture fixture;
    fixture.stack.push(fixture.state("first"));
    fixture.stack.push(fixture.state("second"));
    fixture.stack.push(fixture.state("third"));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.clear();
    fixture.stack.apply_pending();

    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"third:exit", "second:exit", "first:exit"});
    CHECK(fixture.stack.empty());
}

TEST_CASE("a state may pop itself from inside update", "[runtime][state_stack]")
{
    Fixture fixture;

    /// The case the deferred queue exists for: applying the pop immediately would
    /// destroy the object whose update() is still on the call stack.
    class SelfPopping final : public GameState
    {
    public:
        std::string_view name() const override { return "self-popping"; }
        void update(double) override { this->stack().pop(); }
        void render() override { this->rendered = true; }

        bool rendered = false;
    };

    auto owned = std::make_unique<SelfPopping>();
    SelfPopping& state = *owned;
    fixture.stack.push(std::move(owned));
    fixture.stack.apply_pending();

    fixture.stack.update(0.016);
    trace("update() asked for a pop: size={}, has_pending={}", fixture.stack.size(),
          fixture.stack.has_pending());
    CHECK(fixture.stack.size() == 1);
    CHECK(fixture.stack.has_pending());

    // Still alive here, exactly as it is between update() and apply_pending() in a
    // real frame.
    state.render();
    CHECK(state.rendered);

    fixture.stack.apply_pending();
    trace("after apply_pending(): size={}, empty={}", fixture.stack.size(),
          fixture.stack.empty());
    CHECK(fixture.stack.empty());
}

TEST_CASE("events travel top-down until consumed", "[runtime][state_stack]")
{
    Fixture fixture;

    auto middle = fixture.state("middle");
    middle->blocks_input = false;
    auto top = fixture.state("top");
    top->blocks_input = false;

    fixture.stack.push(fixture.state("bottom"));
    fixture.stack.push(std::move(middle));
    fixture.stack.push(std::move(top));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.handle_event(CloseEvent{});
    trace("nobody consumes, nobody blocks:");
    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"top:event", "middle:event", "bottom:event"});
}

TEST_CASE("a consuming state stops the event", "[runtime][state_stack]")
{
    Fixture fixture;

    auto top = fixture.state("top");
    top->blocks_input = false;
    top->consumes_events = true;

    fixture.stack.push(fixture.state("bottom"));
    fixture.stack.push(std::move(top));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.handle_event(CloseEvent{});
    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"top:event"});
}

TEST_CASE("a modal state blocks input it does not consume", "[runtime][state_stack]")
{
    Fixture fixture;

    auto top = fixture.state("top");
    top->blocks_input = true;      // default: modal
    top->consumes_events = false;  // and still not interested in the event

    fixture.stack.push(fixture.state("bottom"));
    fixture.stack.push(std::move(top));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.handle_event(CloseEvent{});
    trace("the event reached the top only, though it was not consumed:");
    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"top:event"});
}

TEST_CASE("update stops at the first state that blocks it", "[runtime][state_stack]")
{
    Fixture fixture;

    auto overlay = fixture.state("overlay");
    overlay->blocks_update = true;

    fixture.stack.push(fixture.state("bottom"));
    fixture.stack.push(std::move(overlay));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.update(0.016);
    trace("frozen dialogue underneath an opaque overlay:");
    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"overlay:update"});
}

TEST_CASE("a transparent overlay renders over the state below", "[runtime][state_stack]")
{
    Fixture fixture;

    auto overlay = fixture.state("menu");
    overlay->blocks_render = false;
    overlay->blocks_update = true;

    fixture.stack.push(fixture.state("dialogue"));
    fixture.stack.push(std::move(overlay));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.update(0.016);
    fixture.stack.render();
    trace("update stops at the menu, rendering runs bottom-up through both:");
    fixture.dump();
    CHECK(fixture.journal ==
          std::vector<std::string>{"menu:update", "dialogue:render", "menu:render"});
}

TEST_CASE("hidden states below an opaque top are not rendered", "[runtime][state_stack]")
{
    Fixture fixture;
    fixture.stack.push(fixture.state("hidden"));
    fixture.stack.push(fixture.state("opaque"));
    fixture.stack.apply_pending();
    fixture.journal.clear();

    fixture.stack.render();
    fixture.dump();
    CHECK(fixture.journal == std::vector<std::string>{"opaque:render"});
}

TEST_CASE("a transition queued from on_enter runs in the next generation",
          "[runtime][state_stack]")
{
    Fixture fixture;

    /// A loading state that immediately replaces itself with the state it
    /// prepared — the shape a real "load a chapter" transition takes.
    class Bootstrapping final : public GameState
    {
    public:
        explicit Bootstrapping(std::vector<std::string>& shared_journal)
            : journal(&shared_journal)
        {
        }

        std::string_view name() const override { return "bootstrap"; }

        void on_enter() override
        {
            this->journal->push_back("bootstrap:enter");
            this->stack().replace(std::make_unique<RecordingState>("chapter", *this->journal));
        }

        void on_exit() override { this->journal->push_back("bootstrap:exit"); }

    private:
        std::vector<std::string>* journal = nullptr;
    };

    fixture.stack.push(std::make_unique<Bootstrapping>(fixture.journal));
    fixture.stack.apply_pending();

    fixture.dump();
    CHECK(fixture.journal ==
          std::vector<std::string>{"bootstrap:enter", "bootstrap:exit", "chapter:enter"});
    CHECK(fixture.stack.size() == 1);
    CHECK(fixture.stack.top()->name() == "chapter");
}

TEST_CASE("a transition cycle is capped rather than hanging the frame", "[runtime][state_stack]")
{
    Fixture fixture;

    /// Pushes a copy of itself on entry, forever.
    class Multiplying final : public GameState
    {
    public:
        std::string_view name() const override { return "multiplying"; }
        void on_enter() override { this->stack().push(std::make_unique<Multiplying>()); }
    };

    const cpen::test::LogCaptureGuard logs;
    fixture.stack.push(std::make_unique<Multiplying>());
    fixture.stack.apply_pending();

    trace("depth after the cap: {} (limit is {} passes), still pending={}", fixture.stack.size(),
          StateStack::MAX_APPLY_PASSES, fixture.stack.has_pending());
    trace("reported as an error: {}", logs.contains("did not settle"));

    CHECK(fixture.stack.size() == StateStack::MAX_APPLY_PASSES);
    CHECK(fixture.stack.has_pending());
    CHECK(logs.contains("did not settle"));
}

TEST_CASE("popping an empty stack is reported, not fatal", "[runtime][state_stack]")
{
    Fixture fixture;

    const cpen::test::LogCaptureGuard logs;
    fixture.stack.pop();
    fixture.stack.apply_pending();

    trace("pop on empty stack: logged={}, empty={}", logs.contains("empty state stack"),
          fixture.stack.empty());
    CHECK(logs.contains("empty state stack"));
    CHECK(fixture.stack.empty());
}

TEST_CASE("the stack exits its states when destroyed", "[runtime][state_stack]")
{
    // Written out rather than using the fixture: here the stack has to die first,
    // while the journal it writes into is still alive.
    std::vector<std::string> journal;
    EventBus event_bus;
    Blackboard blackboard(event_bus);
    Renderer renderer{Viewport{}};
    VirtualFileSystem files;
    AssetResolver asset_resolver{files};
    AssetManager assets{files, asset_resolver};
    GameContext context{
        .blackboard = blackboard,
        .event_bus = event_bus,
        .renderer = renderer,
        .assets = assets,
    };

    {
        StateStack stack(context);
        stack.push(std::make_unique<RecordingState>("first", journal));
        stack.push(std::make_unique<RecordingState>("second", journal));
        stack.apply_pending();
        journal.clear();
    }

    trace("teardown order:");
    for (const std::string& entry : journal)
    {
        trace("{}", entry);
    }
    CHECK(journal == std::vector<std::string>{"second:exit", "first:exit"});
}

TEST_CASE("states reach the shared services through their context", "[runtime][state_stack]")
{
    Fixture fixture;

    /// Writing to the blackboard from a state is the seam that lets a mini-game
    /// and the script VM share one set of variables.
    class WritingState final : public GameState
    {
    public:
        std::string_view name() const override { return "writing"; }
        void on_enter() override { this->context().blackboard.set("visited", true); }
    };

    fixture.stack.push(std::make_unique<WritingState>());
    fixture.stack.apply_pending();

    trace("blackboard after the state entered: visited={}",
          fixture.blackboard.get("visited").to_string());
    CHECK(fixture.blackboard.get("visited").as_boolean() == true);
}
