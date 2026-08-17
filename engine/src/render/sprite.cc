#include "cpen/render/sprite.hh"

#include <cmath>

namespace cpen::render
{
    SpriteTransform sprite_transform(const Sprite& sprite) noexcept
    {
        // The unrotated case, which is also the starting point for the rotated one:
        // the unit quad's axes scaled to the sprite's extent.
        glm::vec2 x_axis{sprite.size.x, 0.0f};
        glm::vec2 y_axis{0.0f, sprite.size.y};

        if (sprite.rotation != 0.0f)
        {
            const float cosine = std::cos(sprite.rotation);
            const float sine = std::sin(sprite.rotation);

            // The ordinary two-dimensional rotation, written out per axis. In a
            // space with y downwards it turns clockwise on screen, which is what
            // Sprite::rotation is documented to mean.
            x_axis = glm::vec2{cosine, sine} * sprite.size.x;
            y_axis = glm::vec2{-sine, cosine} * sprite.size.y;
        }

        // The origin is a fraction of the sprite, so it has to be carried through
        // the same axes as everything else: half a rotated width is not half a
        // width along the screen's x.
        const glm::vec2 pivot = x_axis * sprite.origin.x + y_axis * sprite.origin.y;

        return SpriteTransform{
            .x_axis = x_axis,
            .y_axis = y_axis,
            .translation = sprite.position - pivot,
        };
    }

    glm::vec4 normalized_region(const TextureRegion& region,
                                const glm::vec2& texture_size) noexcept
    {
        if (region.is_whole_texture() || texture_size.x <= 0.0f || texture_size.y <= 0.0f)
        {
            return glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
        }

        const glm::vec2 top_left = region.position / texture_size;
        const glm::vec2 bottom_right = (region.position + region.size) / texture_size;

        return glm::vec4{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    }
}
