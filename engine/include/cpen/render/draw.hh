#ifndef CPEN_RENDER_DRAW_HH
#define CPEN_RENDER_DRAW_HH

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace cpen::render
{
    class VertexArray;

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

    /// Sets the region of the framebuffer that normalised device coordinates map
    /// onto. Must be called whenever the framebuffer is resized, or the image keeps
    /// being stretched into the old rectangle.
    void set_viewport(int x, int y, int width, int height);

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
}

#endif //CPEN_RENDER_DRAW_HH
