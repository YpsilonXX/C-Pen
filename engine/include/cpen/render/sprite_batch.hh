#ifndef CPEN_RENDER_SPRITE_BATCH_HH
#define CPEN_RENDER_SPRITE_BATCH_HH

#include "cpen/core/error.hh"
#include "cpen/render/buffer.hh"
#include "cpen/render/shader.hh"
#include "cpen/render/sprite.hh"
#include "cpen/render/vertex_array.hh"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace cpen::render
{
    class Texture;

    /// Collects sprites and draws them in as few calls as they can be drawn in.
    ///
    /// One quad of four vertices and six indices is uploaded once and never
    /// touched again. Each sprite adds a record to a stream buffer describing where
    /// it goes, and the whole collection is drawn with a single instanced call.
    /// What a thousand sprites cost is therefore one draw call and one upload,
    /// rather than a thousand of each.
    ///
    /// The batch is split by texture and by nothing else. A run of sprites sharing
    /// a texture is one call; a different texture ends the run and begins another,
    /// because a draw call reads from one sampler binding. **Sprites are drawn in
    /// the order they were submitted** and the batch never reorders them to make
    /// the runs longer: with no depth buffer and with alpha blending, submission
    /// order *is* the result, and a batch that sorted by texture would quietly move
    /// a character behind the background it was standing in front of. Reducing the
    /// number of runs is the job of packing sprites into a shared atlas, which
    /// changes what is drawn rather than in what order.
    ///
    /// Move-only, and under the same requirement as everything else in this layer:
    /// it must not outlive the GL context.
    class SpriteBatch
    {
    public:
        /// Sprites held before a flush becomes necessary. A batch this size costs
        /// 96 KB of driver memory, and a scene that submits more of them is not
        /// penalised — it is simply drawn in more than one call.
        static constexpr std::size_t DEFAULT_CAPACITY = 2048;

        /// Compiles the batch's shader and allocates its buffers.
        ///
        /// Returns an error only for the shader: buffer allocation has no
        /// meaningful failure, which is why Buffer's own constructors do not
        /// return one either.
        static std::expected<SpriteBatch, core::Error> create(
            std::size_t capacity = DEFAULT_CAPACITY);

        ~SpriteBatch() = default;

        SpriteBatch(SpriteBatch&&) noexcept = default;
        SpriteBatch& operator=(SpriteBatch&&) noexcept = default;

        SpriteBatch(const SpriteBatch&) = delete;
        SpriteBatch& operator=(const SpriteBatch&) = delete;

        /// Opens a batch against a projection — in practice the one a Viewport
        /// supplies. Resets the counters and enables alpha blending.
        void begin(const glm::mat4& projection);

        /// Adds one sprite, flushing first if the texture differs from the previous
        /// sprite's or if the batch is full.
        ///
        /// The texture is neither owned nor kept past the flush that draws with it,
        /// but it must stay alive until then — which is to say until end() returns,
        /// or until a sprite with a different texture is submitted. Anything with a
        /// shorter life than the frame it is drawn in is a bug elsewhere.
        void draw(const Texture& texture, const Sprite& sprite);

        /// Draws whatever is left and closes the batch, restoring blending to off.
        void end();

        std::size_t capacity() const noexcept { return this->batch_capacity; }

        /// Draw calls issued since begin(). One per texture run, plus one whenever
        /// the capacity was reached mid-run. The number a frame is worth watching.
        std::size_t draw_calls() const noexcept { return this->issued_draw_calls; }

        /// Sprites submitted since begin(), whether or not they have been drawn yet.
        std::size_t sprite_count() const noexcept { return this->submitted_sprites; }

        /// True between begin() and end().
        bool is_open() const noexcept { return this->open; }

    private:
        /// What one sprite becomes on its way to the GPU.
        ///
        /// The layout is the contract with the vertex shader and with the
        /// VertexLayout built in the .cc, and all three have to agree; the static
        /// assertions there hold them to it. Four bytes at the end belong to no
        /// attribute and exist only to round the stride up to a multiple of
        /// sixteen.
        struct InstanceRecord
        {
            /// The sprite's two axes, x then y, as SpriteTransform computed them.
            glm::vec4 axes{0.0f, 0.0f, 0.0f, 0.0f};

            /// The sampled rectangle as (u0, v0, u1, v1), already normalised.
            glm::vec4 region{0.0f, 0.0f, 1.0f, 1.0f};

            glm::vec2 translation{0.0f, 0.0f};

            /// The tint, one byte per channel in R, G, B, A order.
            ///
            /// An array of bytes rather than a packed integer so that the order in
            /// memory is the order written here, whatever the machine's endianness:
            /// GL reads these four bytes in address order, and a packed integer
            /// would put them in an order that depends on the platform.
            std::array<std::uint8_t, 4> color{255, 255, 255, 255};

            std::uint32_t padding = 0;
        };

        SpriteBatch(Shader batch_shader, Buffer corner_buffer, Buffer index_buffer,
                    Buffer instance_buffer, VertexArray batch_array,
                    std::size_t capacity);

        /// Uploads the pending records and issues one instanced draw.
        void flush();

        static InstanceRecord to_record(const Sprite& sprite, const glm::vec2& texture_size);

        Shader shader;

        // Declaration order is the lifetime contract, as everywhere else that owns
        // a vertex array beside the buffers it reads: members are destroyed in
        // reverse, so the array goes before the buffers whose names it holds.
        Buffer corners;
        Buffer indices;
        Buffer instances;
        VertexArray array;

        /// Staged on the CPU and uploaded once per flush, rather than written
        /// straight into mapped driver memory. One upload of a contiguous block is
        /// what the stream buffer is shaped for, and it keeps the submission path
        /// free of any GL call at all.
        std::vector<InstanceRecord> pending;

        /// The texture the pending records are for. Compared by GL name rather than
        /// by address, so the same texture reached through two references does not
        /// split a run.
        const Texture* current_texture = nullptr;

        std::size_t batch_capacity = 0;
        std::size_t issued_draw_calls = 0;
        std::size_t submitted_sprites = 0;
        bool open = false;

        /// Set once a misuse has been reported, so a draw() called every frame from
        /// outside a batch complains once instead of sixty times a second — the
        /// same discipline as Shader's rejected uniform names.
        bool reported_misuse = false;
    };
}

#endif //CPEN_RENDER_SPRITE_BATCH_HH
