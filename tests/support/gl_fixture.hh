#ifndef CPEN_TESTS_SUPPORT_GL_FIXTURE_HH
#define CPEN_TESTS_SUPPORT_GL_FIXTURE_HH

#include "cpen/platform/context.hh"
#include "cpen/platform/window.hh"

namespace cpen::test
{
    /// A hidden window and its GL context, created once for the whole process and
    /// current from the first call until the process ends.
    ///
    /// One instance rather than one per case, for three separate reasons:
    /// platform::Context refuses to exist twice, GLFW must be initialised on the
    /// main thread, and context creation costs far more than the tests that use
    /// it. Catch2 runs cases sequentially, so a shared context needs no guarding —
    /// but tests must not leave global GL state behind them either.
    ///
    /// This deliberately goes through the engine's own platform layer instead of
    /// calling GLFW directly, so the tests exercise the same context-creation path
    /// the game does. That is also why Window::Config grew a `visible` flag rather
    /// than the fixture setting the window hint itself.
    ///
    /// Every test that touches GL must call this before its first GL call.
    inline platform::Window& gl_context()
    {
        static platform::Context context;
        static platform::Window window(context, platform::Window::Config{
                                                    .width = 256,
                                                    .height = 256,
                                                    .title = "cpen gpu tests",
                                                    .vsync = false,
                                                    .resizable = false,
                                                    .visible = false,
                                                });
        return window;
    }
}

#endif //CPEN_TESTS_SUPPORT_GL_FIXTURE_HH
