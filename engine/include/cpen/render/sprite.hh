#ifndef CPEN_RENDER_SPRITE_HH
#define CPEN_RENDER_SPRITE_HH

#include <glm/glm.hpp>

namespace cpen::render
{
    /// A rectangle of a texture, in texels.
    ///
    /// Texels rather than normalised coordinates because that is the unit every
    /// source of one speaks: an atlas packer, a level editor and FreeType all
    /// report a glyph or a frame as a pixel rectangle, and dividing by the texture
    /// size at each of those call sites is a division to get wrong in a different
    /// place each time. The batch knows the texture it is drawing with and does it
    /// once.
    struct TextureRegion
    {
        /// Top-left corner, in texels, measured from the top-left of the texture —
        /// the same direction as the rows arrive in from render::Image.
        glm::vec2 position{0.0f, 0.0f};

        /// Extent in texels. A zero or negative extent means the whole texture,
        /// which is what a default-constructed region asks for and what almost
        /// every sprite outside an atlas wants.
        glm::vec2 size{0.0f, 0.0f};

        bool is_whole_texture() const noexcept
        {
            return this->size.x <= 0.0f || this->size.y <= 0.0f;
        }
    };

    /// One quad to be drawn: where, how big, what part of what texture, what tint.
    ///
    /// Deliberately a plain aggregate with no behaviour and no reference to a
    /// texture. It is submitted to a batch together with the texture, and the batch
    /// keeps neither — a sprite is a description of a draw, not a thing that owns
    /// anything or outlives the frame it was described in.
    struct Sprite
    {
        /// Where the origin lands, in virtual pixels.
        glm::vec2 position{0.0f, 0.0f};

        /// Extent on screen, in virtual pixels, before rotation. Independent of the
        /// region's extent in texels: that is what lets a sprite be scaled.
        glm::vec2 size{0.0f, 0.0f};

        /// The point of the sprite that sits at `position` and that rotation turns
        /// about, as a fraction of `size`.
        ///
        /// A fraction rather than a length in pixels, so it survives a change of
        /// size unchanged: {0, 0} is the top-left corner, {0.5, 0.5} the middle,
        /// {0.5, 1} the middle of the bottom edge — which is how a character sprite
        /// is placed, standing at a point on the floor.
        glm::vec2 origin{0.0f, 0.0f};

        /// Rotation about the origin, in radians.
        ///
        /// Positive is clockwise **on screen**. Not a convention chosen here so
        /// much as a consequence of the one already chosen: virtual space has y
        /// growing downwards, and the ordinary rotation matrix in a space like that
        /// turns the other way round from the one everybody pictures.
        float rotation = 0.0f;

        TextureRegion region{};

        /// Multiplied into the sampled colour, so white leaves the texture alone.
        /// The alpha channel goes through the same multiplication, which is what
        /// makes fading a sprite out a matter of writing this field.
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    /// The affine transform that carries the unit quad onto a sprite.
    ///
    /// Two axes and a translation rather than a matrix type: this is exactly what
    /// travels to the GPU, where the vertex shader recovers a corner's position
    /// with one multiply-add per axis. Storing a full mat4 would send forty-eight
    /// bytes of zeroes and ones per sprite for a 2D transform that has six numbers
    /// in it.
    struct SpriteTransform
    {
        /// Where the unit quad's x axis ends up: the sprite's width, rotated.
        glm::vec2 x_axis{0.0f, 0.0f};

        /// Where its y axis ends up: the sprite's height, rotated. In virtual space
        /// this points *down* the screen for an unrotated sprite.
        glm::vec2 y_axis{0.0f, 0.0f};

        /// Where the unit quad's origin corner ends up.
        glm::vec2 translation{0.0f, 0.0f};

        /// Where a corner of the unit quad lands, in virtual pixels. The four
        /// corners are {0,0}, {1,0}, {1,1} and {0,1}.
        glm::vec2 apply(const glm::vec2& corner) const noexcept
        {
            return this->translation + this->x_axis * corner.x + this->y_axis * corner.y;
        }
    };

    /// Builds the transform for one sprite.
    ///
    /// Pure arithmetic, and public rather than buried in the batch, for the reason
    /// that decided the instance format in the first place: this is where a sprite
    /// can be wrong about where it is, and here it can be asserted against by
    /// naming four corners rather than by drawing and reading pixels back.
    ///
    /// A sprite with no rotation takes a branch that never calls a trigonometric
    /// function. Every glyph of a page of text and most of a visual novel's scene
    /// go down it, and the branch predicts perfectly because rotated and unrotated
    /// sprites arrive in runs rather than alternating.
    SpriteTransform sprite_transform(const Sprite& sprite) noexcept;

    /// Converts a region in texels to the normalised rectangle a sampler takes,
    /// packed as (u0, v0, u1, v1).
    ///
    /// A region asking for the whole texture, and a texture with no size to divide
    /// by, both give the full 0..1 rectangle.
    glm::vec4 normalized_region(const TextureRegion& region,
                                const glm::vec2& texture_size) noexcept;
}

#endif //CPEN_RENDER_SPRITE_HH
