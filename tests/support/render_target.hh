#ifndef CPEN_TESTS_SUPPORT_RENDER_TARGET_HH
#define CPEN_TESTS_SUPPORT_RENDER_TARGET_HH

#include "cpen/render/draw.hh"

#include <glad/glad.h>

#include <array>
#include <cstdint>

namespace cpen::test
{
    /// A colour renderbuffer and the framebuffer it is attached to, so that a draw
    /// can be checked by reading the pixels back.
    ///
    /// Deliberately raw GL rather than a render:: type: there is no
    /// render::Framebuffer yet, and inventing one here would mean testing two
    /// things at once. Drawing into the default framebuffer instead would need no
    /// code at all, but its contents are only defined for a window that is actually
    /// on screen, and the fixture's window is hidden.
    ///
    /// Header-only and in support/ so that both the draw tests and the sprite batch
    /// tests read pixels back the same way. Nothing here runs before a case does,
    /// so a target may only be constructed once a context is current.
    class RenderTarget
    {
    public:
        /// Side of the target, in pixels. Square, so that a coordinate can be read
        /// the same way on both axes.
        static constexpr int SIZE = 64;

        explicit RenderTarget(const int side = SIZE)
            : extent(side)
        {
            glGetIntegerv(GL_VIEWPORT, this->previous_viewport.data());

            glGenRenderbuffers(1, &this->color_buffer);
            glBindRenderbuffer(GL_RENDERBUFFER, this->color_buffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, this->extent, this->extent);

            glGenFramebuffers(1, &this->framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, this->framebuffer);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, this->color_buffer);

            render::set_viewport(0, 0, this->extent, this->extent);
        }

        ~RenderTarget()
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &this->framebuffer);
            glDeleteRenderbuffers(1, &this->color_buffer);

            // Restored rather than left at the target's size: the fixture's context
            // is shared by every case in the process, and global state left behind
            // here would surface as an unrelated failure later.
            glViewport(this->previous_viewport[0], this->previous_viewport[1],
                       this->previous_viewport[2], this->previous_viewport[3]);
        }

        RenderTarget(const RenderTarget&) = delete;
        RenderTarget& operator=(const RenderTarget&) = delete;

        bool is_complete() const
        {
            return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }

        int size() const noexcept { return this->extent; }

        /// Reads one pixel, with the origin at the bottom-left corner — glReadPixels'
        /// own convention, and the opposite of the engine's virtual space. A case
        /// that draws through a projection has to flip for itself, and saying so
        /// here is cheaper than a helper that silently picks one.
        std::array<std::uint8_t, 4> pixel_at(const int x, const int y) const
        {
            std::array<std::uint8_t, 4> color{};
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
            return color;
        }

        std::array<std::uint8_t, 4> centre_pixel() const
        {
            return this->pixel_at(this->extent / 2, this->extent / 2);
        }

    private:
        int extent = SIZE;
        GLuint framebuffer = 0;
        GLuint color_buffer = 0;
        std::array<GLint, 4> previous_viewport{};
    };
}

#endif //CPEN_TESTS_SUPPORT_RENDER_TARGET_HH
