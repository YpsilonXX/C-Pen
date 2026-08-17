#include <catch2/catch_test_macros.hpp>

#include "cpen/render/renderer.hh"
#include "cpen/render/viewport.hh"
#include "support/log_capture.hh"
#include "support/trace.hh"

#include <glm/glm.hpp>

using cpen::log::Level;
using cpen::render::Renderer;
using cpen::render::ScaleMode;
using cpen::render::Viewport;
using cpen::test::LogCaptureGuard;
using cpen::test::trace;
using cpen::test::trace_step;

// A renderer built by its constructor rather than by create() owns no GPU
// resources, and the invariant these cases rest on is that it therefore makes no
// GL call at all. That is not a detail: it is what lets everything taking a
// GameContext — the state stack above all — be exercised on a machine with no
// driver, which is exactly where an automated suite is most useful.
//
// The cases below run in a process with no context ever made current. Every one of
// them would crash if that invariant were broken, which makes this whole file the
// assertion.

TEST_CASE("a renderer without resources knows it cannot draw", "[render][renderer]")
{
    Renderer renderer{Viewport{1920, 1080}};

    trace("can draw: {}, sprites(): {}", renderer.can_draw(),
          renderer.sprites() == nullptr ? "null" : "present");

    CHECK_FALSE(renderer.can_draw());

    // A pointer rather than a reference, so the absence is something a caller tests
    // for instead of something it discovers by dereferencing nothing.
    CHECK(renderer.sprites() == nullptr);

    CHECK(renderer.draw_calls() == 0);
    CHECK(renderer.sprite_count() == 0);
}

TEST_CASE("it still carries the coordinate system", "[render][renderer]")
{
    Renderer renderer{Viewport{1920, 1080}};

    // The half of a renderer that is arithmetic. Turning a click into a position
    // needs no GPU, and separating the two is what makes this class constructible
    // here at all.
    renderer.resize(2560, 1080);

    trace("content {}x{} at ({}, {})", renderer.viewport().rect().width,
          renderer.viewport().rect().height, renderer.viewport().rect().x,
          renderer.viewport().rect().y);

    CHECK(renderer.viewport().rect().width == 1920);
    CHECK(renderer.viewport().rect().x == 320);

    const glm::vec2 middle = renderer.viewport().to_virtual({1280.0f, 540.0f});
    trace("the middle of the window is virtual ({}, {})", middle.x, middle.y);

    CHECK(middle.x > 950.0f);
    CHECK(middle.x < 970.0f);
}

TEST_CASE("opening and closing a frame is safe with nothing to draw with",
          "[render][renderer]")
{
    Renderer renderer{Viewport{1920, 1080}};

    const LogCaptureGuard capture;

    renderer.begin_frame();
    CHECK(renderer.is_frame_open());

    renderer.end_frame();
    CHECK_FALSE(renderer.is_frame_open());

    trace("a frame opened and closed without a driver produced {} error(s)",
          capture.count(Level::ERROR));

    // Silent: there is nothing wrong with a frame that draws nothing, and the
    // Application opens one every tick whether or not its renderer has resources.
    CHECK(capture.count(Level::ERROR) == 0);
}

TEST_CASE("a mismatched frame is reported once", "[render][renderer]")
{
    SECTION("closing one that was never opened")
    {
        Renderer renderer{Viewport{}};
        const LogCaptureGuard capture;

        renderer.end_frame();
        renderer.end_frame();
        renderer.end_frame();

        trace("three unmatched closes produced {} error(s)", capture.count(Level::ERROR));

        // Once, not three times: a loop with the calls in the wrong order makes the
        // same mistake sixty times a second.
        CHECK(capture.count(Level::ERROR) == 1);
    }

    SECTION("opening one that is already open")
    {
        Renderer renderer{Viewport{}};
        const LogCaptureGuard capture;

        renderer.begin_frame();
        renderer.begin_frame();

        trace("a second open produced {} error(s)", capture.count(Level::ERROR));

        CHECK(capture.count(Level::ERROR) == 1);

        // Reported and recovered from rather than refused: the second frame really
        // is open, so the states about to render into it are not silently dropped.
        CHECK(renderer.is_frame_open());
    }
}

TEST_CASE("the clear colour is the renderer's to hold", "[render][renderer]")
{
    Renderer renderer{Viewport{}};

    constexpr glm::vec4 MIDNIGHT{0.05f, 0.05f, 0.12f, 1.0f};
    renderer.set_clear_color(MIDNIGHT);

    trace_step("a state sets it once, rather than clearing to it every frame");

    CHECK(renderer.clear_color() == MIDNIGHT);
}

TEST_CASE("the scale mode travels with the viewport it was built from",
          "[render][renderer]")
{
    Renderer renderer{Viewport{800, 600, ScaleMode::STRETCH}};

    renderer.resize(1600, 600);

    trace("stretched content {}x{}", renderer.viewport().rect().width,
          renderer.viewport().rect().height);

    CHECK(renderer.viewport().scale_mode() == ScaleMode::STRETCH);
    CHECK(renderer.viewport().rect().width == 1600);
}
