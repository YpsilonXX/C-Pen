
#include "cpen/render/draw.hh"

#include "cpen/core/log.hh"
#include "cpen/render/vertex_array.hh"
#include "cpen/render/viewport.hh"

#include <glad/glad.h>

namespace cpen::render
{
    namespace
    {
        GLenum to_gl_primitive(const Primitive primitive) noexcept
        {
            switch (primitive)
            {
                case Primitive::TRIANGLES:      return GL_TRIANGLES;
                case Primitive::TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
                case Primitive::TRIANGLE_FAN:   return GL_TRIANGLE_FAN;
                case Primitive::LINES:          return GL_LINES;
                case Primitive::LINE_STRIP:     return GL_LINE_STRIP;
                case Primitive::POINTS:         return GL_POINTS;
            }
            return GL_TRIANGLES;
        }

        GLenum to_gl_index_type(const IndexType type) noexcept
        {
            switch (type)
            {
                case IndexType::UNSIGNED_SHORT: return GL_UNSIGNED_SHORT;
                case IndexType::UNSIGNED_INT:   return GL_UNSIGNED_INT;
            }
            return GL_UNSIGNED_INT;
        }
    }

    void set_viewport(const int x, const int y, const int width, const int height)
    {
        glViewport(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }

    void set_viewport(const ViewportRect& rect)
    {
        set_viewport(rect.x, rect.y, rect.width, rect.height);
    }

    void set_blend(const BlendMode mode)
    {
        if (mode == BlendMode::NONE)
        {
            glDisable(GL_BLEND);
            return;
        }

        glEnable(GL_BLEND);

        // The alpha channel is blended separately, and not with the same factors.
        // Taken through GL_SRC_ALPHA like the colour channels, the destination's
        // alpha would come out as the product of the two rather than their union,
        // and a stack of half-transparent sprites drawn into an offscreen target
        // would end up more transparent the more of them there were. GL_ONE against
        // GL_ONE_MINUS_SRC_ALPHA accumulates coverage instead. It makes no
        // difference to the default framebuffer, whose alpha nothing reads, and all
        // the difference to a render target that is composited later.
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    void clear(const glm::vec4& color)
    {
        glClearColor(color.r, color.g, color.b, color.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void draw_arrays(const VertexArray& vertices, const Primitive primitive,
                     const std::size_t vertex_count, const std::size_t first_vertex)
    {
        if (vertex_count == 0)
        {
            return;
        }

        vertices.bind();
        glDrawArrays(to_gl_primitive(primitive), static_cast<GLint>(first_vertex),
                     static_cast<GLsizei>(vertex_count));
        VertexArray::unbind();
    }

    void draw_elements(const VertexArray& vertices, const Primitive primitive,
                       const std::size_t index_count, const IndexType index_type)
    {
        if (index_count == 0)
        {
            return;
        }

        if (!vertices.has_index_buffer())
        {
            log::error(log::Category::RENDER,
                       "vertex array {}: an indexed draw of {} index/indices was asked for, "
                       "but no index buffer was ever recorded; the draw is skipped",
                       vertices.id(), index_count);
            return;
        }

        vertices.bind();

        // The final argument is a byte offset into the index buffer, not a pointer:
        // it is typed as one only because the entry point predates buffer objects.
        // Drawing always starts at the beginning here — a first-index parameter
        // would be the sprite batch's business, and it does not exist yet.
        glDrawElements(to_gl_primitive(primitive), static_cast<GLsizei>(index_count),
                       to_gl_index_type(index_type), nullptr);

        VertexArray::unbind();
    }

    void draw_elements_instanced(const VertexArray& vertices, const Primitive primitive,
                                 const std::size_t index_count,
                                 const std::size_t instance_count,
                                 const IndexType index_type)
    {
        if (index_count == 0 || instance_count == 0)
        {
            return;
        }

        if (!vertices.has_index_buffer())
        {
            log::error(log::Category::RENDER,
                       "vertex array {}: an instanced indexed draw of {} index/indices "
                       "across {} instance(s) was asked for, but no index buffer was ever "
                       "recorded; the draw is skipped",
                       vertices.id(), index_count, instance_count);
            return;
        }

        vertices.bind();

        glDrawElementsInstanced(to_gl_primitive(primitive), static_cast<GLsizei>(index_count),
                                to_gl_index_type(index_type), nullptr,
                                static_cast<GLsizei>(instance_count));

        VertexArray::unbind();
    }
}
