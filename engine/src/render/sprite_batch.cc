#include "cpen/render/sprite_batch.hh"

#include "cpen/core/log.hh"
#include "cpen/render/draw.hh"
#include "cpen/render/texture.hh"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cpen::render
{
    namespace
    {
        /// Positions in clip space come from the projection, so the vertex shader
        /// carries no assumption about the coordinate system beyond the one the
        /// matrix encodes.
        ///
        /// The unit quad's corner does double duty: it is the fraction along each
        /// axis of the sprite, and it is the fraction along each axis of the
        /// sampled region. That is only true because both spaces put their origin
        /// at the top left — the engine's texture convention and its virtual-space
        /// convention agree, and this line is where the agreement pays.
        constexpr const char* SPRITE_VERTEX_SHADER = R"(#version 330 core
layout (location = 0) in vec2 in_corner;

layout (location = 1) in vec4 in_axes;
layout (location = 2) in vec4 in_region;
layout (location = 3) in vec2 in_translation;
layout (location = 4) in vec4 in_color;

uniform mat4 projection;

out vec2 texture_coordinate;
out vec4 tint;

void main()
{
    vec2 world = in_translation + in_axes.xy * in_corner.x + in_axes.zw * in_corner.y;
    gl_Position = projection * vec4(world, 0.0, 1.0);

    texture_coordinate = mix(in_region.xy, in_region.zw, in_corner);
    tint = in_color;
}
)";

        constexpr const char* SPRITE_FRAGMENT_SHADER = R"(#version 330 core
in vec2 texture_coordinate;
in vec4 tint;

uniform sampler2D source;

out vec4 fragment_color;

void main()
{
    fragment_color = texture(source, texture_coordinate) * tint;
}
)";

        /// The texture unit every batch samples from.
        constexpr unsigned int SPRITE_TEXTURE_UNIT = 0;

        /// The unit quad, corners in the order the indices below assume: top left,
        /// top right, bottom right, bottom left. "Top" is the smaller y, since
        /// virtual space grows downwards.
        constexpr std::array<float, 8> QUAD_CORNERS = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f,
        };

        constexpr std::array<std::uint32_t, 6> QUAD_INDICES = {0, 1, 2, 2, 3, 0};

        constexpr std::size_t QUAD_INDEX_COUNT = QUAD_INDICES.size();

        VertexLayout corner_layout()
        {
            return VertexLayout{
                .attributes = {{.type = AttributeType::FLOAT, .component_count = 2}},
                .first_location = 0,
            };
        }

        VertexLayout instance_layout()
        {
            return VertexLayout{
                .attributes = {
                    // axes
                    {.type = AttributeType::FLOAT, .component_count = 4},
                    // region
                    {.type = AttributeType::FLOAT, .component_count = 4},
                    // translation
                    {.type = AttributeType::FLOAT, .component_count = 2},
                    // colour, normalised so the shader reads 0..1 from four bytes
                    {.type = AttributeType::UNSIGNED_BYTE,
                     .component_count = 4,
                     .normalized = true},
                },
                .first_location = 1,
                .instance_divisor = 1,
                .trailing_padding = 4,
            };
        }

        std::uint8_t to_channel(const float value) noexcept
        {
            // Rounded rather than truncated, so that 1.0 comes back as 255 and not
            // as 254, and clamped because a tint outside 0..1 is a caller's
            // arithmetic rather than a request for a wider colour space.
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
        }
    }

    std::expected<SpriteBatch, core::Error> SpriteBatch::create(const std::size_t capacity)
    {
        // Held to the shader's own attribute declarations and to instance_layout()
        // above. All three describe the same 48 bytes, and only these assertions
        // stop them drifting apart into geometry that is wrong in a way nothing
        // reports.
        static_assert(sizeof(InstanceRecord) == 48);
        static_assert(offsetof(InstanceRecord, axes) == 0);
        static_assert(offsetof(InstanceRecord, region) == 16);
        static_assert(offsetof(InstanceRecord, translation) == 32);
        static_assert(offsetof(InstanceRecord, color) == 40);

        const std::size_t sprites = std::max<std::size_t>(capacity, 1);

        auto shader = Shader::create("render.sprite_batch", SPRITE_VERTEX_SHADER,
                                     SPRITE_FRAGMENT_SHADER);
        if (!shader)
        {
            return std::unexpected(shader.error());
        }

        Buffer corners = Buffer::vertex(QUAD_CORNERS);
        Buffer indices = Buffer::index(QUAD_INDICES);
        Buffer instances = Buffer::vertex_storage(sprites * sizeof(InstanceRecord),
                                                  BufferUsage::STREAM);

        VertexArray array;
        array.attach(corners, corner_layout());
        array.attach(instances, instance_layout());
        array.set_index_buffer(indices);

        log::info(log::Category::RENDER,
                  "sprite batch ready: {} sprite(s) per call, {} byte(s) of instance store",
                  sprites, instances.size_in_bytes());

        return SpriteBatch{std::move(*shader), std::move(corners), std::move(indices),
                           std::move(instances), std::move(array), sprites};
    }

    SpriteBatch::SpriteBatch(Shader batch_shader, Buffer corner_buffer, Buffer index_buffer,
                             Buffer instance_buffer, VertexArray batch_array,
                             const std::size_t capacity)
        : shader(std::move(batch_shader)),
          corners(std::move(corner_buffer)),
          indices(std::move(index_buffer)),
          instances(std::move(instance_buffer)),
          array(std::move(batch_array)),
          batch_capacity(capacity)
    {
        this->pending.reserve(capacity);
    }

    SpriteBatch::InstanceRecord SpriteBatch::to_record(const Sprite& sprite,
                                                       const glm::vec2& texture_size)
    {
        const SpriteTransform transform = sprite_transform(sprite);
        const glm::vec4 region = normalized_region(sprite.region, texture_size);

        return InstanceRecord{
            .axes = glm::vec4{transform.x_axis.x, transform.x_axis.y,
                              transform.y_axis.x, transform.y_axis.y},
            .region = region,
            .translation = transform.translation,
            .color = {to_channel(sprite.color.r), to_channel(sprite.color.g),
                      to_channel(sprite.color.b), to_channel(sprite.color.a)},
            .padding = 0,
        };
    }

    void SpriteBatch::begin(const glm::mat4& projection)
    {
        if (this->open)
        {
            log::error(log::Category::RENDER,
                       "sprite batch: begin() was called while a batch was already open; "
                       "the {} sprite(s) collected so far are drawn before it reopens",
                       this->pending.size());
            this->end();
        }

        this->open = true;
        this->issued_draw_calls = 0;
        this->submitted_sprites = 0;
        this->pending.clear();
        this->current_texture = nullptr;

        // Written once per batch rather than once per flush: both uniforms live in
        // the program object, so they survive the binds and unbinds each flush does.
        this->shader.bind();
        this->shader.set_uniform("projection", projection);
        this->shader.set_uniform("source", static_cast<int>(SPRITE_TEXTURE_UNIT));
        Shader::unbind();

        set_blend(BlendMode::ALPHA);
    }

    void SpriteBatch::draw(const Texture& texture, const Sprite& sprite)
    {
        if (!this->open)
        {
            if (!this->reported_misuse)
            {
                this->reported_misuse = true;
                log::error(log::Category::RENDER,
                           "sprite batch: draw() was called outside begin()/end(); the "
                           "sprite is dropped, and further ones will be dropped silently");
            }
            return;
        }

        // A different texture cannot join the pending run: one draw call samples
        // from one binding.
        if (this->current_texture != nullptr &&
            this->current_texture->id() != texture.id())
        {
            this->flush();
        }

        if (this->pending.size() >= this->batch_capacity)
        {
            this->flush();
        }

        this->current_texture = &texture;

        this->pending.push_back(to_record(
            sprite, glm::vec2{static_cast<float>(texture.width()),
                              static_cast<float>(texture.height())}));

        ++this->submitted_sprites;
    }

    void SpriteBatch::flush()
    {
        if (this->pending.empty() || this->current_texture == nullptr)
        {
            return;
        }

        // Orphan before writing. The driver is very likely still reading this store
        // for a draw issued earlier, and overwriting it in place would make it
        // either block or copy; see Buffer::orphan. Every flush writes from offset
        // zero, so nothing of the old contents is wanted.
        this->instances.orphan();
        this->instances.update(this->pending);

        this->shader.bind();
        this->current_texture->bind(SPRITE_TEXTURE_UNIT);

        draw_elements_instanced(this->array, Primitive::TRIANGLES, QUAD_INDEX_COUNT,
                                this->pending.size());

        Texture::unbind(SPRITE_TEXTURE_UNIT);
        Shader::unbind();

        this->pending.clear();
        ++this->issued_draw_calls;
    }

    void SpriteBatch::end()
    {
        if (!this->open)
        {
            if (!this->reported_misuse)
            {
                this->reported_misuse = true;
                log::error(log::Category::RENDER,
                           "sprite batch: end() was called without a matching begin()");
            }
            return;
        }

        this->flush();

        this->current_texture = nullptr;
        this->open = false;

        // Put back rather than left on. Nothing else in the engine enables blending
        // yet, so off is what the batch found and off is what it leaves; when
        // Renderer owns the frame's state this belongs to it instead.
        set_blend(BlendMode::NONE);
    }
}
