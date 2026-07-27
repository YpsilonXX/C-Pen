#ifndef CPEN_RENDER_VERTEX_ARRAY_HH
#define CPEN_RENDER_VERTEX_ARRAY_HH

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cpen::render
{
    class Buffer;

    /// The scalar type of one attribute component.
    enum class AttributeType : std::uint8_t
    {
        FLOAT,
        /// 8-bit unsigned, normally read normalised as one channel of a packed
        /// colour: four of these cost four bytes where four floats cost sixteen.
        UNSIGNED_BYTE,
        INTEGER,
        UNSIGNED_INTEGER,
    };

    /// The size in bytes of one component of `type`.
    constexpr std::size_t component_size(const AttributeType type) noexcept
    {
        switch (type)
        {
            case AttributeType::FLOAT:            return sizeof(float);
            case AttributeType::UNSIGNED_BYTE:    return sizeof(std::uint8_t);
            case AttributeType::INTEGER:          return sizeof(std::int32_t);
            case AttributeType::UNSIGNED_INTEGER: return sizeof(std::uint32_t);
        }
        return 0;
    }

    constexpr std::string_view to_string(const AttributeType type) noexcept
    {
        switch (type)
        {
            case AttributeType::FLOAT:            return "float";
            case AttributeType::UNSIGNED_BYTE:    return "unsigned byte";
            case AttributeType::INTEGER:          return "integer";
            case AttributeType::UNSIGNED_INTEGER: return "unsigned integer";
        }
        return "unknown";
    }

    /// One attribute: a run of one to four components of the same scalar type,
    /// filling one attribute slot of the vertex shader.
    struct VertexAttribute
    {
        AttributeType type = AttributeType::FLOAT;
        unsigned int component_count = 1;

        /// Maps an integer component onto 0..1 (unsigned) or -1..1 (signed) on the
        /// way into the shader, where it arrives as a float regardless. Ignored for
        /// FLOAT, and the whole reason a packed RGBA8 colour is worth storing as
        /// four bytes rather than four floats.
        bool normalized = false;

        /// The size in bytes this attribute occupies in a vertex.
        constexpr std::size_t size_in_bytes() const noexcept
        {
            return component_size(this->type) * this->component_count;
        }
    };

    /// Describes how one buffer feeds a contiguous run of attribute slots.
    ///
    /// The attributes are taken to be tightly interleaved in the order given: the
    /// stride is their total size and each offset follows the one before it. That
    /// is the layout every buffer in this engine uses — the alternative, one buffer
    /// per attribute, costs a second binding per attribute and buys nothing for 2D
    /// geometry that is always written and read whole.
    struct VertexLayout
    {
        std::vector<VertexAttribute> attributes;

        /// The slot the first attribute binds to; the rest follow in order. Matches
        /// `layout (location = N)` in the vertex shader, which is why an explicit
        /// starting slot is needed at all: a second buffer attached to the same
        /// array continues the numbering rather than restarting it.
        unsigned int first_location = 0;

        /// The distance in bytes between consecutive vertices.
        std::size_t stride() const;
    };

    /// A GL vertex array object: the record of which buffer feeds which attribute
    /// slot, in what format, and which buffer supplies element indices.
    ///
    /// It describes where vertices come from, and nothing else. In particular it
    /// holds no vertex count and issues no draw calls — see render/draw.hh for why.
    ///
    /// Ownership is move-only, as with Shader and Buffer, and carries the same
    /// requirement not to outlive the GL context.
    class VertexArray
    {
    public:
        VertexArray();
        ~VertexArray();

        VertexArray(VertexArray&& other) noexcept;
        VertexArray& operator=(VertexArray&& other) noexcept;

        VertexArray(const VertexArray&) = delete;
        VertexArray& operator=(const VertexArray&) = delete;

        void bind() const;
        static void unbind();

        /// Records the attribute pointers that read from `buffer`.
        ///
        /// The buffer is neither owned nor kept: what the GL object stores is the
        /// buffer's name, captured per attribute at this moment. The caller must
        /// therefore keep the buffer alive for as long as this array is drawn from.
        /// That is arranged structurally — buffers and the array that reads them are
        /// declared in the same owner, the array last so that it is destroyed first —
        /// which is the same discipline that keeps a Shader inside its GL context.
        ///
        /// Non-owning rather than owning because GL's own model is non-owning: one
        /// buffer may feed several arrays, and an owning attach() would make that
        /// inexpressible. It also keeps a streamed buffer reachable by the code that
        /// rewrites it every frame, instead of behind an accessor on the array.
        void attach(const Buffer& buffer, const VertexLayout& layout);

        /// Records `buffer` as the source of element indices.
        ///
        /// Unlike the attribute buffers, this really is part of vertex array object
        /// state in GL: the element array binding is captured by the bound array
        /// rather than by the context.
        void set_index_buffer(const Buffer& buffer);

        bool has_index_buffer() const noexcept { return this->index_buffer != 0; }

        /// The name of the index buffer recorded by set_index_buffer(), or 0.
        unsigned int index_buffer_id() const noexcept { return this->index_buffer; }

        /// Number of attribute slots enabled so far, across every attach().
        std::size_t attribute_count() const noexcept { return this->enabled_attributes; }

        unsigned int id() const noexcept { return this->handle; }

    private:
        void destroy() noexcept;

        unsigned int handle = 0;
        unsigned int index_buffer = 0;
        std::size_t enabled_attributes = 0;
    };
}

#endif //CPEN_RENDER_VERTEX_ARRAY_HH
