#ifndef CPEN_RENDER_DRAW_HH
#define CPEN_RENDER_DRAW_HH

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cpen::render
{
    class VertexArray;
    struct ViewportRect;

    /// What the stream of vertices is assembled into.
    enum class Primitive : std::uint8_t
    {
        TRIANGLES,
        TRIANGLE_STRIP,
        TRIANGLE_FAN,
        LINES,
        LINE_STRIP,
        POINTS,
    };

    /// The scalar type of one element index.
    enum class IndexType : std::uint8_t
    {
        /// Addresses 65536 vertices, which is well beyond what one sprite batch
        /// will hold, at half the bandwidth of the 32-bit form.
        UNSIGNED_SHORT,
        UNSIGNED_INT,
    };

    /// How a fragment is combined with what is already in the framebuffer.
    enum class BlendMode : std::uint8_t
    {
        /// The fragment replaces what is there, alpha included. GL's own default,
        /// and what an opaque background wants.
        NONE,

        /// Straight, non-premultiplied alpha: the fragment's own alpha decides how
        /// much of it shows.
        ///
        /// Straight rather than premultiplied because that is what a PNG contains
        /// as authored. Premultiplied blends more correctly under filtering and
        /// composes in more places, but it requires every asset to be converted on
        /// the way in, and there is no asset pipeline to convert them in yet.
        ALPHA,
    };

    constexpr std::string_view to_string(const BlendMode mode) noexcept
    {
        switch (mode)
        {
            case BlendMode::NONE:  return "none";
            case BlendMode::ALPHA: return "alpha";
        }
        return "unknown";
    }

    /// Sets the region of the framebuffer that normalised device coordinates map
    /// onto. Must be called whenever the framebuffer is resized, or the image keeps
    /// being stretched into the old rectangle.
    void set_viewport(int x, int y, int width, int height);

    /// Selects how fragments are blended into the framebuffer.
    ///
    /// Global state, like the viewport and unlike anything a Shader or a Buffer
    /// owns, so it lives here beside them and moves into Renderer with them.
    void set_blend(BlendMode mode);

    /// Applies the rectangle a Viewport computed. The overload exists so that the
    /// four components are never unpacked and reordered by hand at a call site:
    /// glViewport's origin is the bottom-left corner, and a transposed y is a bug
    /// that only shows up on a window whose aspect ratio is not the design one.
    void set_viewport(const ViewportRect& rect);

    /// Clears the colour buffer to `color`, with alpha as its fourth component.
    void clear(const glm::vec4& color);

    /// Draws `vertex_count` vertices read straight from the attribute buffers.
    ///
    /// Drawing is a free function rather than a method on VertexArray because a
    /// draw call is not the vertex array's property. It consumes the bound program,
    /// framebuffer, blend state and viewport just as much as the attribute state,
    /// and the vertex count is a property of what was written into the buffers, not
    /// of the layout that reads them. A VertexArray::draw() would have to grow a
    /// count field the GL object does not have, and would assert by its very
    /// existence that a vertex array is enough to draw with.
    ///
    /// The array is nonetheless passed explicitly rather than left implicit in the
    /// current binding: these functions bind it themselves, so that a call reads as
    /// "draw from this array" instead of "draw from whatever was bound last".
    void draw_arrays(const VertexArray& vertices, Primitive primitive,
                     std::size_t vertex_count, std::size_t first_vertex = 0);

    /// Draws `index_count` vertices selected by the array's index buffer.
    ///
    /// Reported and skipped if the array has no index buffer: without one the call
    /// would read indices from address zero, which is a fault on some drivers and
    /// arbitrary geometry on the rest.
    void draw_elements(const VertexArray& vertices, Primitive primitive,
                       std::size_t index_count,
                       IndexType index_type = IndexType::UNSIGNED_INT);

    /// Draws the same indexed geometry `instance_count` times, with the attributes
    /// whose layout carried a non-zero instance_divisor advancing once per instance
    /// instead of once per vertex.
    ///
    /// This is what turns a thousand sprites into one draw call: the quad's four
    /// vertices and six indices are uploaded once and never touched again, and only
    /// the per-instance record — where the sprite is, what part of the texture it
    /// shows, what colour it is tinted — is rewritten each frame.
    ///
    /// An instance count of zero draws nothing, as an index count of zero does.
    /// There is no draw_arrays_instanced beside this one: nothing needs it yet, and
    /// a draw call with no caller could not be tested against anything real.
    void draw_elements_instanced(const VertexArray& vertices, Primitive primitive,
                                 std::size_t index_count, std::size_t instance_count,
                                 IndexType index_type = IndexType::UNSIGNED_INT);
}

#endif //CPEN_RENDER_DRAW_HH
