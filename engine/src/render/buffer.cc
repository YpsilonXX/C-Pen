#include "cpen/render/buffer.hh"

#include "cpen/core/log.hh"

#include <glad/glad.h>

#include <type_traits>
#include <utility>

namespace cpen::render
{
    namespace
    {
        // The public header describes buffer names as plain integers so that
        // consumers need no GL headers. These assertions are what make that
        // description true rather than merely usual.
        static_assert(std::is_same_v<GLuint, unsigned int>);
        static_assert(std::is_same_v<GLenum, unsigned int>);

        /// Every store creation and every update goes through GL_ARRAY_BUFFER,
        /// whatever the buffer's own target is.
        ///
        /// Both reasons concern GL_ELEMENT_ARRAY_BUFFER specifically, whose binding
        /// is part of vertex array object state rather than context state. Filling
        /// an index buffer through it would silently rewrite whichever vertex array
        /// happened to be bound, and restoring the binding to zero afterwards would
        /// clear that array's index source outright. Worse, the core profile has no
        /// default vertex array object, so with none bound the binding has nowhere
        /// to go at all.
        ///
        /// This is sound because a GL buffer object has no type: the target selects
        /// which slot the name fills, it does not classify the object. Only the
        /// engine treats a buffer's target as fixed, and only so that VertexArray
        /// can reject an obvious mix-up.
        constexpr GLenum UPLOAD_TARGET = GL_ARRAY_BUFFER;

        GLenum to_gl_target(const BufferTarget target) noexcept
        {
            switch (target)
            {
                case BufferTarget::VERTEX: return GL_ARRAY_BUFFER;
                case BufferTarget::INDEX:  return GL_ELEMENT_ARRAY_BUFFER;
            }
            return GL_ARRAY_BUFFER;
        }

        GLenum to_gl_usage(const BufferUsage usage) noexcept
        {
            switch (usage)
            {
                case BufferUsage::STATIC:  return GL_STATIC_DRAW;
                case BufferUsage::DYNAMIC: return GL_DYNAMIC_DRAW;
                case BufferUsage::STREAM:  return GL_STREAM_DRAW;
            }
            return GL_STATIC_DRAW;
        }
    }

    Buffer::Buffer(const BufferTarget target, const std::byte* const data,
                   const std::size_t size_in_bytes, const BufferUsage usage)
        : store_size(size_in_bytes),
          buffer_target(target),
          buffer_usage(usage)
    {
        glGenBuffers(1, &this->handle);

        glBindBuffer(UPLOAD_TARGET, this->handle);
        glBufferData(UPLOAD_TARGET, static_cast<GLsizeiptr>(size_in_bytes), data,
                     to_gl_usage(usage));
        glBindBuffer(UPLOAD_TARGET, 0);

        log::debug(log::Category::RENDER, "{} buffer {} created, {} byte(s)",
                   to_string(target), this->handle, size_in_bytes);
    }

    Buffer::~Buffer()
    {
        this->destroy();
    }

    Buffer::Buffer(Buffer&& other) noexcept
        : handle(std::exchange(other.handle, 0)),
          store_size(std::exchange(other.store_size, 0)),
          buffer_target(other.buffer_target),
          buffer_usage(other.buffer_usage)
    {
    }

    Buffer& Buffer::operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            this->destroy();

            this->handle = std::exchange(other.handle, 0);
            this->store_size = std::exchange(other.store_size, 0);
            this->buffer_target = other.buffer_target;
            this->buffer_usage = other.buffer_usage;
        }
        return *this;
    }

    void Buffer::destroy() noexcept
    {
        // Deleting buffer name 0 is defined as a no-op, so a moved-from Buffer
        // needs no separate handling here.
        glDeleteBuffers(1, &this->handle);
        this->handle = 0;
        this->store_size = 0;
    }

    void Buffer::bind() const
    {
        glBindBuffer(to_gl_target(this->buffer_target), this->handle);
    }

    void Buffer::unbind(const BufferTarget target)
    {
        glBindBuffer(to_gl_target(target), 0);
    }

    void Buffer::update_bytes(const std::span<const std::byte> data,
                              const std::size_t offset_in_bytes)
    {
        if (data.empty())
        {
            return;
        }

        // Subtraction rather than addition on the left: offset + size could wrap
        // round on a caller's arithmetic mistake and let an overlong write through.
        if (offset_in_bytes > this->store_size ||
            data.size() > this->store_size - offset_in_bytes)
        {
            log::error(log::Category::RENDER,
                       "buffer {}: an update of {} byte(s) at offset {} does not fit a "
                       "{}-byte store; the write is ignored",
                       this->handle, data.size(), offset_in_bytes, this->store_size);
            return;
        }

        glBindBuffer(UPLOAD_TARGET, this->handle);
        glBufferSubData(UPLOAD_TARGET, static_cast<GLintptr>(offset_in_bytes),
                        static_cast<GLsizeiptr>(data.size()), data.data());
        glBindBuffer(UPLOAD_TARGET, 0);
    }
}
