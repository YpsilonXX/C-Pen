#ifndef CPEN_RENDER_BUFFER_HH
#define CPEN_RENDER_BUFFER_HH

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>

namespace cpen::render
{
    /// Which binding point a buffer is meant to be attached to.
    ///
    /// The engine's own names rather than the GL enumerators, so that this header
    /// stays free of the GL headers exactly as render/shader.hh does; the mapping
    /// lives in buffer.cc and is the only place the GL constants appear.
    ///
    /// GL itself does not type a buffer object — the same name may be bound to any
    /// target. Recording a target per buffer is therefore the engine's own rule
    /// about what a buffer is *for*, and it is what lets VertexArray refuse an
    /// index buffer where vertex data was meant.
    enum class BufferTarget : std::uint8_t
    {
        /// Per-vertex attribute data. GL_ARRAY_BUFFER.
        VERTEX,
        /// Element indices for indexed drawing. GL_ELEMENT_ARRAY_BUFFER.
        INDEX,
    };

    constexpr std::string_view to_string(const BufferTarget target) noexcept
    {
        switch (target)
        {
            case BufferTarget::VERTEX: return "vertex";
            case BufferTarget::INDEX:  return "index";
        }
        return "unknown";
    }

    /// A hint about how often the store will be rewritten, which the driver uses
    /// to decide where to keep it. A wrong hint costs performance, never
    /// correctness.
    enum class BufferUsage : std::uint8_t
    {
        /// Filled once, drawn from many times: geometry that never changes.
        STATIC,
        /// Rewritten now and then, drawn from many times between rewrites.
        DYNAMIC,
        /// Rewritten every frame — what the sprite batch's instance data will be.
        STREAM,
    };

    namespace detail
    {
        /// Reinterprets any contiguous range as the bytes the GL entry points take.
        ///
        /// A range rather than a std::span so that a std::array, a std::vector or a
        /// C array can be passed as it is: a fixed-extent span does not deduce to a
        /// dynamic one, so a span parameter would force every call site to build one.
        template <std::ranges::contiguous_range Range>
        std::span<const std::byte> bytes_of(const Range& data)
        {
            return std::as_bytes(std::span{std::ranges::data(data), std::ranges::size(data)});
        }
    }

    /// A GL buffer object: a block of driver-managed memory plus the size and
    /// intent it was created with.
    ///
    /// Ownership is move-only, the same shape as render::Shader, and the same one
    /// rule applies: a Buffer must not outlive the GL context, since its destructor
    /// calls into GL. That is arranged structurally — buffers are owned by whatever
    /// owns the mesh, which the application declares after the window — rather than
    /// checked at runtime.
    class Buffer
    {
    public:
        /// Creates a vertex buffer filled with `data`.
        template <std::ranges::contiguous_range Range>
        static Buffer vertex(const Range& data, const BufferUsage usage = BufferUsage::STATIC)
        {
            const std::span<const std::byte> bytes = detail::bytes_of(data);
            return Buffer{BufferTarget::VERTEX, bytes.data(), bytes.size(), usage};
        }

        /// Creates an index buffer filled with `data`.
        template <std::ranges::contiguous_range Range>
        static Buffer index(const Range& data, const BufferUsage usage = BufferUsage::STATIC)
        {
            const std::span<const std::byte> bytes = detail::bytes_of(data);
            return Buffer{BufferTarget::INDEX, bytes.data(), bytes.size(), usage};
        }

        /// Allocates a vertex buffer of `size_in_bytes` without filling it, for
        /// contents written later with update(). Drawing from it before it is
        /// written is not an error in GL — it simply reads whatever the driver
        /// left there.
        static Buffer vertex_storage(const std::size_t size_in_bytes, const BufferUsage usage)
        {
            return Buffer{BufferTarget::VERTEX, nullptr, size_in_bytes, usage};
        }

        /// Allocates an index buffer of `size_in_bytes` without filling it.
        static Buffer index_storage(const std::size_t size_in_bytes, const BufferUsage usage)
        {
            return Buffer{BufferTarget::INDEX, nullptr, size_in_bytes, usage};
        }

        ~Buffer();

        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        /// Const for the same reason Shader::bind() is: binding changes GL's global
        /// state, not this buffer's contents.
        void bind() const;
        static void unbind(BufferTarget target);

        /// Overwrites part of the store, leaving its size unchanged.
        ///
        /// A write that would run past the end is reported and dropped rather than
        /// truncated: a partial upload draws plausible-looking wrong geometry,
        /// which is considerably harder to diagnose than nothing at all.
        template <std::ranges::contiguous_range Range>
        void update(const Range& data, const std::size_t offset_in_bytes = 0)
        {
            this->update_bytes(detail::bytes_of(data), offset_in_bytes);
        }

        BufferTarget target() const noexcept { return this->buffer_target; }
        BufferUsage usage() const noexcept { return this->buffer_usage; }
        std::size_t size_in_bytes() const noexcept { return this->store_size; }

        /// The raw buffer name, for the few places that must talk to GL directly.
        /// GLuint is unsigned int on every platform the engine targets, which is
        /// what lets this header stay free of the GL headers.
        unsigned int id() const noexcept { return this->handle; }

    private:
        /// The one real constructor. A null `data` allocates the store without
        /// filling it, which is glBufferData's own convention.
        Buffer(BufferTarget target, const std::byte* data, std::size_t size_in_bytes,
               BufferUsage usage);

        void update_bytes(std::span<const std::byte> data, std::size_t offset_in_bytes);

        void destroy() noexcept;

        unsigned int handle = 0;
        std::size_t store_size = 0;
        BufferTarget buffer_target = BufferTarget::VERTEX;
        BufferUsage buffer_usage = BufferUsage::STATIC;
    };
}

#endif //CPEN_RENDER_BUFFER_HH
