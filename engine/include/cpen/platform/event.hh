#ifndef CPEN_PLATFORM_EVENT_HH
#define CPEN_PLATFORM_EVENT_HH

#include "cpen/platform/input.hh"

#include <cstdint>
#include <variant>

namespace cpen::platform
{
    /// A key changed state. `key` identifies a physical key; for typed text use
    /// TextEvent instead, which accounts for layout, dead keys and composition.
    struct KeyEvent
    {
        Key key = Key::UNKNOWN;
        InputAction action = InputAction::RELEASE;
        Modifiers modifiers{};

        /// Platform-specific physical code. Carried through for the rare case of
        /// distinguishing keys the Key enum does not name; not portable.
        int scancode = 0;
    };

    /// One character produced by the keyboard, already decoded to a Unicode code
    /// point by the operating system. This is the only correct source for text
    /// input: it honours the active layout, modifiers and dead keys.
    struct TextEvent
    {
        char32_t codepoint = 0;
    };

    /// A mouse button changed state. The cursor position is captured at the
    /// moment of the event, so a click is not mis-attributed to wherever the
    /// pointer has drifted by the time the event is handled.
    /// A press or a release, with the cursor position it happened at.
    ///
    /// The position is in framebuffer pixels, which is the space
    /// render::Viewport::to_virtual() converts from: the two are stated in the
    /// same units on purpose, so that turning a click into a place in the game
    /// world is one call and not a conversion somebody has to remember.
    struct MouseButtonEvent
    {
        MouseButton button = MouseButton::LEFT;
        InputAction action = InputAction::RELEASE;
        Modifiers modifiers{};
        double x = 0.0;
        double y = 0.0;
    };

    /// The cursor moved. Coordinates are in window coordinates, origin top-left.
    /// The cursor's new position, in framebuffer pixels. See MouseButtonEvent.
    struct MouseMoveEvent
    {
        double x = 0.0;
        double y = 0.0;
    };

    /// Wheel or trackpad scroll. Vertical scrolling reports `y_offset`.
    struct ScrollEvent
    {
        double x_offset = 0.0;
        double y_offset = 0.0;
    };

    /// The drawable surface changed size, in pixels.
    ///
    /// This is the framebuffer size, not the window size: on high-DPI displays
    /// they differ by the content scale, and every GL call (glViewport above all)
    /// works in pixels.
    struct ResizeEvent
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    /// The user asked to close the window (title-bar button, Alt+F4, and so on).
    /// Advisory: the close flag is already set, but the application decides when
    /// to honour it — a "save before quitting?" prompt lives here.
    struct CloseEvent
    {
    };

    /// The window gained or lost keyboard focus. A game typically pauses and
    /// releases held-key state on focus loss.
    struct FocusEvent
    {
        bool focused = false;
    };

    /// Everything the platform layer reports, as one value type.
    ///
    /// A variant rather than a class hierarchy: events are plain data with no
    /// behaviour, they live in a contiguous queue with no allocation, and
    /// std::visit gives exhaustiveness checking that a virtual dispatch would not.
    using Event = std::variant<
        KeyEvent,
        TextEvent,
        MouseButtonEvent,
        MouseMoveEvent,
        ScrollEvent,
        ResizeEvent,
        CloseEvent,
        FocusEvent>;
}

#endif //CPEN_PLATFORM_EVENT_HH
