#ifndef CPEN_RENDER_VIEWPORT_HH
#define CPEN_RENDER_VIEWPORT_HH

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

namespace cpen::render
{
    /// How the virtual resolution is fitted into a framebuffer whose aspect ratio
    /// does not match it.
    enum class ScaleMode : std::uint8_t
    {
        /// Scale both axes by the smaller of the two ratios and centre the result,
        /// leaving bars along the axis that is left over. Nothing is distorted and
        /// nothing is cropped, which is what a hand-composed scene requires: a
        /// background painted for 1920x1080 must arrive on screen with the same
        /// proportions and the same content it was drawn with.
        LETTERBOX,

        /// Scale each axis independently so that the framebuffer is filled
        /// completely. Distorts everything on any aspect ratio but the design one.
        STRETCH,
    };

    constexpr std::string_view to_string(const ScaleMode mode) noexcept
    {
        switch (mode)
        {
            case ScaleMode::LETTERBOX: return "letterbox";
            case ScaleMode::STRETCH:   return "stretch";
        }
        return "unknown";
    }

    /// A rectangle of the framebuffer, in pixels, with the origin at its
    /// bottom-left corner.
    ///
    /// Signed and bottom-left because that is what glViewport takes, and this
    /// struct exists to be handed to it. Note that the origin convention here is
    /// the opposite of the virtual space Viewport projects onto it; the conversion
    /// happens in one place, inside Viewport, and nowhere else.
    struct ViewportRect
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    constexpr bool operator==(const ViewportRect& left, const ViewportRect& right) noexcept
    {
        return left.x == right.x && left.y == right.y &&
               left.width == right.width && left.height == right.height;
    }

    /// The mapping between the fixed resolution the game is authored for and the
    /// arbitrary framebuffer it is displayed in.
    ///
    /// Everything above the render layer positions things in *virtual* pixels — a
    /// character stands at x = 960 whatever the window happens to be — and this
    /// class is the single place that knows what those become on screen. It holds
    /// three related answers: the projection matrix a shader multiplies by, the
    /// rectangle of the framebuffer that is drawn into, and the conversion between
    /// a physical pixel and a virtual coordinate that turns a mouse position into
    /// something a button can be hit-tested against.
    ///
    /// Virtual space has its origin at the *top left* with y increasing downwards.
    /// That is the same convention as the texture coordinates the engine already
    /// uses (v = 0 is the top row), as the cursor positions the platform layer
    /// reports, and as the baseline arithmetic of a text layout. The cost is that
    /// it reverses triangle winding on the way to clip space: a triangle wound
    /// counter-clockwise in virtual coordinates arrives clockwise. Face culling is
    /// off, and a 2D engine has no reason to turn it on, so nothing depends on
    /// this today — but it is the answer to why enabling GL_CULL_FACE would make
    /// every sprite vanish.
    ///
    /// Pure arithmetic: no GL call is made from here, and no GL context is needed
    /// to construct one. Applying the rectangle is the caller's job — see
    /// set_viewport() in render/draw.hh — which is what lets the whole mapping be
    /// tested without a driver.
    class Viewport
    {
    public:
        /// The resolution the engine's own assets are authored for.
        static constexpr std::uint32_t DEFAULT_VIRTUAL_WIDTH = 1920;
        static constexpr std::uint32_t DEFAULT_VIRTUAL_HEIGHT = 1080;

        /// A degenerate virtual size is reported and replaced by 1, because every
        /// quantity here divides by it. The scale mode is fixed at construction,
        /// for the same reason TextureConfig has no setters: a mapping that can
        /// change under a renderer holding it by const reference turns a layout
        /// bug into a frame-order bug.
        explicit Viewport(std::uint32_t virtual_width = DEFAULT_VIRTUAL_WIDTH,
                          std::uint32_t virtual_height = DEFAULT_VIRTUAL_HEIGHT,
                          ScaleMode mode = ScaleMode::LETTERBOX);

        /// Recomputes the mapping for a new framebuffer size, in pixels.
        ///
        /// A framebuffer with a zero dimension is not an error: a window minimised
        /// on Windows reports exactly that. It yields an empty rectangle and a zero
        /// scale, and the conversions below answer with the origin rather than a
        /// NaN that would spread through every position computed from them.
        void resize(std::uint32_t framebuffer_width, std::uint32_t framebuffer_height);

        /// Virtual coordinates to clip space.
        ///
        /// Constant for the lifetime of the Viewport: letterboxing changes which
        /// part of the framebuffer is drawn into, never what the virtual space is.
        /// A shader's projection uniform therefore needs writing once, not on every
        /// resize and certainly not on every frame.
        const glm::mat4& projection() const noexcept { return this->projection_matrix; }

        /// The part of the framebuffer the virtual space maps onto — the whole of
        /// it minus the bars.
        const ViewportRect& rect() const noexcept { return this->content_rect; }

        /// Physical pixels per virtual pixel, per axis. Equal on both axes under
        /// LETTERBOX and generally unequal under STRETCH, which is why this is a
        /// vector and not a single factor.
        const glm::vec2& scale() const noexcept { return this->content_scale; }

        /// Converts a point in framebuffer pixels, origin top-left, into virtual
        /// coordinates.
        ///
        /// This is what a click becomes before anything is hit-tested. A point on
        /// one of the bars converts to a coordinate outside 0..virtual_size, which
        /// is the honest answer and the one a caller should test for rather than
        /// have clamped for it.
        ///
        /// The argument is in *framebuffer* pixels, not the window coordinates the
        /// cursor is reported in. The two differ on a high-DPI display, and the
        /// caller converts — Viewport is deliberately not given a second size to
        /// keep in step with the first.
        glm::vec2 to_virtual(const glm::vec2& framebuffer_point) const;

        /// The inverse of to_virtual(), in the same units and conventions.
        glm::vec2 to_framebuffer(const glm::vec2& virtual_point) const;

        const glm::uvec2& virtual_size() const noexcept { return this->virtual_resolution; }
        const glm::uvec2& framebuffer_size() const noexcept { return this->physical_size; }
        ScaleMode scale_mode() const noexcept { return this->fit_mode; }

    private:
        /// Distance from the top edge of the framebuffer to the top edge of the
        /// content, in pixels.
        ///
        /// Derived rather than stored, and derived by subtraction rather than
        /// assumed equal to content_rect.y: the two bars differ by a pixel whenever
        /// the leftover space is odd, and this is the one that virtual y = 0 sits
        /// against.
        int top_margin() const noexcept;

        glm::uvec2 virtual_resolution;
        ScaleMode fit_mode;

        glm::uvec2 physical_size{0, 0};
        ViewportRect content_rect;
        glm::vec2 content_scale{0.0f, 0.0f};
        glm::mat4 projection_matrix;
    };
}

#endif //CPEN_RENDER_VIEWPORT_HH
