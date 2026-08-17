#ifndef CPEN_RENDER_RENDERER_HH
#define CPEN_RENDER_RENDERER_HH

#include "cpen/core/error.hh"
#include "cpen/render/sprite_batch.hh"
#include "cpen/render/viewport.hh"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace cpen::render
{
    /// Everything a game state needs in order to draw, and the only thing it is
    /// given.
    ///
    /// This is the seam the whole render layer was built towards: below it are
    /// shaders, buffers, vertex arrays and GL entry points, and above it a state
    /// asks for a sprite to be drawn at a position in virtual pixels. Nothing in
    /// games/ names a GL type, includes a GL header, compiles a shader or manages a
    /// frame.
    ///
    /// The frame is opened and closed by the Application, once, around the state
    /// stack's render pass — not by the states. Two things follow from that, and
    /// both are the point. A state cannot forget to close what it did not open. And
    /// because the stack renders bottom upwards, every state in it submits into one
    /// batch: a menu drawn over a dialogue merges its text with the dialogue's
    /// instead of starting a second run, so an overlay costs nothing beyond the
    /// textures it actually introduces.
    ///
    /// The cost of that choice, stated plainly: one projection for the whole frame.
    /// A heads-up display wanting its own camera would have to close the frame's
    /// batch and open another, which is not expressible here yet. It becomes
    /// expressible when something needs it.
    ///
    /// A renderer built by create() owns GPU resources and must not outlive the GL
    /// context. One built by the constructor owns none, and the invariant worth
    /// relying on is that it then makes **no GL call at all** — not a guarded one,
    /// not a harmless one. That is what lets everything taking a GameContext be
    /// exercised on a machine with no driver, and it is why the absence is a real
    /// state of this class rather than a test fixture pretending to be one.
    class Renderer
    {
    public:
        /// Builds a renderer with a coordinate system and the means to draw in it.
        ///
        /// Fails only for the sprite batch's shader; the viewport is arithmetic and
        /// cannot.
        static std::expected<Renderer, core::Error> create(
            std::uint32_t virtual_width = Viewport::DEFAULT_VIRTUAL_WIDTH,
            std::uint32_t virtual_height = Viewport::DEFAULT_VIRTUAL_HEIGHT,
            ScaleMode mode = ScaleMode::LETTERBOX,
            std::size_t sprite_capacity = SpriteBatch::DEFAULT_CAPACITY);

        /// Builds a renderer that knows where things are and cannot draw them.
        ///
        /// Not a degenerate case: a coordinate system is useful on its own — it is
        /// what turns a click into a position — and separating it from the means to
        /// draw is what keeps the runtime layer testable without a driver.
        explicit Renderer(Viewport initial_viewport);

        ~Renderer() = default;

        Renderer(Renderer&&) noexcept = default;
        Renderer& operator=(Renderer&&) noexcept = default;

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        /// True when this renderer owns the resources drawing needs.
        bool can_draw() const noexcept { return this->batch.has_value(); }

        /// Refits the viewport to a framebuffer size and hands the rectangle to GL.
        ///
        /// The refit always happens; the rectangle is only applied when there is
        /// something to apply it with.
        void resize(std::uint32_t framebuffer_width, std::uint32_t framebuffer_height);

        /// Clears the framebuffer and opens the frame's batch. Called by the
        /// Application, not by a state.
        void begin_frame();

        /// Draws whatever the states submitted and closes the frame.
        void end_frame();

        bool is_frame_open() const noexcept { return this->frame_open; }

        /// The batch open for this frame, or nullptr if this renderer cannot draw.
        ///
        /// A pointer rather than a reference so that the absence is something a
        /// caller can test for, rather than something it finds out about by
        /// dereferencing nothing.
        SpriteBatch* sprites() noexcept;

        const Viewport& viewport() const noexcept { return this->frame_viewport; }

        void set_clear_color(const glm::vec4& color) noexcept { this->background = color; }
        const glm::vec4& clear_color() const noexcept { return this->background; }

        /// What the last frame cost. Valid from end_frame() until the next
        /// begin_frame() resets it.
        std::size_t draw_calls() const noexcept;
        std::size_t sprite_count() const noexcept;

    private:
        Renderer(Viewport initial_viewport, SpriteBatch initial_batch);

        Viewport frame_viewport;
        std::optional<SpriteBatch> batch;

        /// Dark rather than black, so that a frame which drew nothing at all is
        /// visibly a frame rather than a window that failed to open.
        glm::vec4 background{0.06f, 0.06f, 0.08f, 1.0f};

        bool frame_open = false;

        /// Set once a frame has been opened twice or closed unopened, so a mistake
        /// in the loop is reported once instead of sixty times a second.
        bool reported_misuse = false;
    };
}

#endif //CPEN_RENDER_RENDERER_HH
