#include "cpen/render/viewport.hh"

#include "cpen/core/log.hh"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace cpen::render
{
    Viewport::Viewport(const std::uint32_t virtual_width, const std::uint32_t virtual_height,
                       const ScaleMode mode)
        : virtual_resolution(std::max(virtual_width, 1u), std::max(virtual_height, 1u)),
          fit_mode(mode)
    {
        if (virtual_width == 0 || virtual_height == 0)
        {
            log::error(log::Category::RENDER,
                       "a virtual resolution of {}x{} has a zero dimension and every "
                       "quantity of the mapping divides by it; 1 is used instead",
                       virtual_width, virtual_height);
        }

        // The near and far planes are GL's own defaults. The argument order is what
        // puts the origin at the top left: the third argument is the coordinate of
        // the *bottom* edge, so naming the height there and zero for the top makes
        // y grow downwards.
        this->projection_matrix =
            glm::ortho(0.0f, static_cast<float>(this->virtual_resolution.x),
                       static_cast<float>(this->virtual_resolution.y), 0.0f,
                       -1.0f, 1.0f);
    }

    void Viewport::resize(const std::uint32_t framebuffer_width,
                          const std::uint32_t framebuffer_height)
    {
        this->physical_size = {framebuffer_width, framebuffer_height};

        if (framebuffer_width == 0 || framebuffer_height == 0)
        {
            this->content_rect = ViewportRect{};
            this->content_scale = {0.0f, 0.0f};
            return;
        }

        const auto width = static_cast<int>(framebuffer_width);
        const auto height = static_cast<int>(framebuffer_height);

        const auto virtual_width = static_cast<float>(this->virtual_resolution.x);
        const auto virtual_height = static_cast<float>(this->virtual_resolution.y);

        if (this->fit_mode == ScaleMode::STRETCH)
        {
            this->content_rect = ViewportRect{.x = 0, .y = 0, .width = width, .height = height};
        }
        else
        {
            // The smaller ratio is the one that fits: taking the larger would scale
            // the other axis past the edge of the framebuffer and crop it.
            const float factor = std::min(static_cast<float>(width) / virtual_width,
                                          static_cast<float>(height) / virtual_height);

            // Rounded, not truncated, and then held inside the framebuffer.
            // Truncating loses a pixel on the very axis the factor was derived
            // from — 1920 * (1000 / 1920) evaluates to 999.99994 in single
            // precision — which puts a one-pixel black column down the side of a
            // window that was supposed to be filled edge to edge. The clamp guards
            // the other direction, where the same rounding error would ask for a
            // pixel the framebuffer does not have.
            const int content_width =
                std::min(static_cast<int>(std::lround(virtual_width * factor)), width);
            const int content_height =
                std::min(static_cast<int>(std::lround(virtual_height * factor)), height);

            // Integer division leaves the odd pixel of a leftover on the far side.
            this->content_rect = ViewportRect{
                .x = (width - content_width) / 2,
                .y = (height - content_height) / 2,
                .width = content_width,
                .height = content_height,
            };
        }

        // Derived from the rectangle rather than from the ratio it was computed
        // with, and derived the same way in both modes. The rectangle is what GL is
        // given, so the rectangle is what the hardware scales the virtual space
        // onto; recomputing the factor from it keeps to_virtual() an exact inverse
        // of the transform on screen instead of one that is a truncated pixel off.
        this->content_scale = {
            static_cast<float>(this->content_rect.width) / virtual_width,
            static_cast<float>(this->content_rect.height) / virtual_height,
        };
    }

    int Viewport::top_margin() const noexcept
    {
        return static_cast<int>(this->physical_size.y) - this->content_rect.height -
               this->content_rect.y;
    }

    glm::vec2 Viewport::to_virtual(const glm::vec2& framebuffer_point) const
    {
        if (this->content_scale.x <= 0.0f || this->content_scale.y <= 0.0f)
        {
            return {0.0f, 0.0f};
        }

        return {
            (framebuffer_point.x - static_cast<float>(this->content_rect.x)) /
                this->content_scale.x,
            (framebuffer_point.y - static_cast<float>(this->top_margin())) /
                this->content_scale.y,
        };
    }

    glm::vec2 Viewport::to_framebuffer(const glm::vec2& virtual_point) const
    {
        return {
            static_cast<float>(this->content_rect.x) + virtual_point.x * this->content_scale.x,
            static_cast<float>(this->top_margin()) + virtual_point.y * this->content_scale.y,
        };
    }
}
