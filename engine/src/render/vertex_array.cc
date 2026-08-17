#include "cpen/render/vertex_array.hh"

#include "cpen/core/log.hh"
#include "cpen/render/buffer.hh"

#include <glad/glad.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace cpen::render
{
    namespace
    {
        static_assert(std::is_same_v<GLuint, unsigned int>);
        static_assert(std::is_same_v<GLenum, unsigned int>);

        /// The largest number of components one attribute slot can hold. A vec4 is
        /// the widest thing GL will feed a single slot; a mat4 attribute is four
        /// slots, which is a layout the caller spells out rather than a case here.
        constexpr unsigned int MAXIMUM_COMPONENT_COUNT = 4;

        GLenum to_gl_type(const AttributeType type) noexcept
        {
            switch (type)
            {
                case AttributeType::FLOAT:            return GL_FLOAT;
                case AttributeType::UNSIGNED_BYTE:    return GL_UNSIGNED_BYTE;
                case AttributeType::INTEGER:          return GL_INT;
                case AttributeType::UNSIGNED_INTEGER: return GL_UNSIGNED_INT;
            }
            return GL_FLOAT;
        }

        bool is_integer(const AttributeType type) noexcept
        {
            return type != AttributeType::FLOAT;
        }
    }

    std::size_t VertexLayout::stride() const
    {
        std::size_t total = this->trailing_padding;
        for (const VertexAttribute& attribute : this->attributes)
        {
            total += attribute.size_in_bytes();
        }
        return total;
    }

    VertexArray::VertexArray()
    {
        // glGenVertexArrays only reserves the name; the object itself does not
        // exist until the name is first bound, which is also why glIsVertexArray
        // answers false in between. Binding here gets that out of the way, so a
        // freshly constructed VertexArray is a real object rather than a promise.
        glGenVertexArrays(1, &this->handle);
        glBindVertexArray(this->handle);
        glBindVertexArray(0);

        log::debug(log::Category::RENDER, "vertex array {} created", this->handle);
    }

    VertexArray::~VertexArray()
    {
        this->destroy();
    }

    VertexArray::VertexArray(VertexArray&& other) noexcept
        : handle(std::exchange(other.handle, 0)),
          index_buffer(std::exchange(other.index_buffer, 0)),
          enabled_attributes(std::exchange(other.enabled_attributes, 0))
    {
    }

    VertexArray& VertexArray::operator=(VertexArray&& other) noexcept
    {
        if (this != &other)
        {
            this->destroy();

            this->handle = std::exchange(other.handle, 0);
            this->index_buffer = std::exchange(other.index_buffer, 0);
            this->enabled_attributes = std::exchange(other.enabled_attributes, 0);
        }
        return *this;
    }

    void VertexArray::destroy() noexcept
    {
        // Deleting vertex array name 0 is defined as a no-op, so a moved-from
        // VertexArray needs no separate handling here.
        glDeleteVertexArrays(1, &this->handle);
        this->handle = 0;
        this->index_buffer = 0;
        this->enabled_attributes = 0;
    }

    void VertexArray::bind() const
    {
        glBindVertexArray(this->handle);
    }

    void VertexArray::unbind()
    {
        glBindVertexArray(0);
    }

    void VertexArray::attach(const Buffer& buffer, const VertexLayout& layout)
    {
        if (buffer.target() != BufferTarget::VERTEX)
        {
            log::error(log::Category::RENDER,
                       "vertex array {}: attach() takes a vertex buffer, but buffer {} is "
                       "an {} buffer; the attachment is ignored",
                       this->handle, buffer.id(), to_string(buffer.target()));
            return;
        }

        if (layout.attributes.empty())
        {
            log::error(log::Category::RENDER,
                       "vertex array {}: buffer {} was attached with an empty layout; "
                       "the attachment is ignored",
                       this->handle, buffer.id());
            return;
        }

        const std::size_t stride = layout.stride();

        glBindVertexArray(this->handle);

        // The attribute pointers are recorded into the bound vertex array together
        // with the buffer they read from, so this binding is part of the state being
        // captured rather than a prerequisite of a later draw.
        buffer.bind();

        std::size_t offset = 0;
        unsigned int location = layout.first_location;

        for (const VertexAttribute& attribute : layout.attributes)
        {
            if (attribute.component_count == 0 ||
                attribute.component_count > MAXIMUM_COMPONENT_COUNT)
            {
                log::error(log::Category::RENDER,
                           "vertex array {}: attribute at slot {} asks for {} component(s) "
                           "of {}, which is outside the 1..{} an attribute slot holds; "
                           "the slot is skipped",
                           this->handle, location, attribute.component_count,
                           to_string(attribute.type), MAXIMUM_COMPONENT_COUNT);

                offset += attribute.size_in_bytes();
                ++location;
                continue;
            }

            glEnableVertexAttribArray(location);

            const auto components = static_cast<GLint>(attribute.component_count);
            const auto* const offset_pointer =
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset));

            if (is_integer(attribute.type) && !attribute.normalized)
            {
                // An integer attribute that is not normalised must be declared with
                // glVertexAttribIPointer. Declared the other way round it is
                // converted to float on the way in, and a shader reading it as
                // `in int` gets the bit pattern of a float instead.
                glVertexAttribIPointer(location, components, to_gl_type(attribute.type),
                                       static_cast<GLsizei>(stride), offset_pointer);
            }
            else
            {
                glVertexAttribPointer(location, components, to_gl_type(attribute.type),
                                      attribute.normalized ? GL_TRUE : GL_FALSE,
                                      static_cast<GLsizei>(stride), offset_pointer);
            }

            // Set unconditionally, including for the default of zero. The divisor is
            // per-attribute state of the vertex array, and an attribute slot may
            // well have been left at a non-zero divisor by whatever used it last:
            // writing the value every time is what makes a layout describe the
            // attachment completely rather than only the parts that differ.
            glVertexAttribDivisor(location, layout.instance_divisor);

            offset += attribute.size_in_bytes();
            ++location;
            ++this->enabled_attributes;
        }

        glBindVertexArray(0);
        Buffer::unbind(BufferTarget::VERTEX);

        log::debug(log::Category::RENDER,
                   "vertex array {}: buffer {} feeds slot(s) {}..{}, stride {} byte(s), "
                   "divisor {}",
                   this->handle, buffer.id(), layout.first_location, location - 1, stride,
                   layout.instance_divisor);
    }

    void VertexArray::set_index_buffer(const Buffer& buffer)
    {
        if (buffer.target() != BufferTarget::INDEX)
        {
            log::error(log::Category::RENDER,
                       "vertex array {}: set_index_buffer() takes an index buffer, but "
                       "buffer {} is a {} buffer; the assignment is ignored",
                       this->handle, buffer.id(), to_string(buffer.target()));
            return;
        }

        glBindVertexArray(this->handle);
        buffer.bind();

        // Deliberately no unbind of the element array here: that binding is the
        // state being recorded, and clearing it would undo exactly what this call
        // was for. Unbinding the array object is enough — and is what stops any
        // later element array binding from reaching this one.
        glBindVertexArray(0);

        this->index_buffer = buffer.id();

        log::debug(log::Category::RENDER, "vertex array {}: index buffer {} recorded",
                   this->handle, buffer.id());
    }
}
