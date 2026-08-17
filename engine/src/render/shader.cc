#include "cpen/render/shader.hh"

#include "cpen/core/log.hh"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

namespace cpen::render
{
    namespace
    {
        // The public header describes program names and uniform types as plain
        // integers so that consumers need no GL headers. These assertions are what
        // make that description true rather than merely usual.
        static_assert(std::is_same_v<GLuint, unsigned int>);
        static_assert(std::is_same_v<GLenum, unsigned int>);
        static_assert(std::is_same_v<GLint, int>);

        /// Reads back the driver's diagnostic for a shader or program object.
        ///
        /// The shader and program variants differ only in the entry points used.
        /// The glad typedefs are used verbatim because they carry the platform
        /// calling convention, which a hand-written function-pointer type would
        /// drop on Windows; the program-side typedefs name the same function
        /// types, so passing glGetProgramiv here is exact, not a conversion.
        std::string read_info_log(const GLuint object,
                                  const PFNGLGETSHADERIVPROC get_parameter,
                                  const PFNGLGETSHADERINFOLOGPROC get_log)
        {
            GLint length = 0;
            get_parameter(object, GL_INFO_LOG_LENGTH, &length);
            if (length <= 0)
            {
                return "no diagnostic supplied by the driver";
            }

            std::vector<GLchar> buffer(static_cast<std::size_t>(length));

            GLsizei written = 0;
            get_log(object, length, &written, buffer.data());

            std::string message(buffer.data(), static_cast<std::size_t>(written));

            // Drivers habitually terminate the log with a newline, which would
            // break the one-line-per-record shape of the log.
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            {
                message.pop_back();
            }

            return message;
        }

        std::string_view stage_name(const GLenum stage) noexcept
        {
            return stage == GL_VERTEX_SHADER ? "vertex" : "fragment";
        }

        /// Compiles one stage. The shader name is returned on success; on failure
        /// the name is released and the driver's diagnostic is returned instead.
        std::expected<GLuint, core::Error> compile_stage(const std::string_view name,
                                                         const GLenum stage,
                                                         const std::string_view source)
        {
            const GLuint shader = glCreateShader(stage);

            // std::string_view is not guaranteed to be null-terminated, so the
            // length is passed explicitly rather than relying on the terminator.
            const auto* const text = source.data();
            const auto length = static_cast<GLint>(source.size());
            glShaderSource(shader, 1, &text, &length);
            glCompileShader(shader);

            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

            if (compiled == GL_FALSE)
            {
                core::Error failure = core::make_error(
                    core::ErrorCode::COMPILATION_FAILED,
                    "shader '{}': {} stage failed to compile: {}",
                    name, stage_name(stage),
                    read_info_log(shader, glGetShaderiv, glGetShaderInfoLog));

                glDeleteShader(shader);
                return std::unexpected(std::move(failure));
            }

            return shader;
        }
    }

    Shader::UniformTable Shader::collect_uniforms(const unsigned int program)
    {
        GLint count = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);

        GLint maximum_name_length = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maximum_name_length);

        UniformTable table;
        if (count <= 0 || maximum_name_length <= 0)
        {
            return table;
        }

        table.reserve(static_cast<std::size_t>(count));
        std::vector<GLchar> name_buffer(static_cast<std::size_t>(maximum_name_length));

        for (GLint index = 0; index < count; ++index)
        {
            GLsizei written = 0;
            GLint element_count = 0;
            GLenum type = 0;
            glGetActiveUniform(program, static_cast<GLuint>(index), maximum_name_length,
                               &written, &element_count, &type, name_buffer.data());

            std::string name(name_buffer.data(), static_cast<std::size_t>(written));

            // An array uniform is reported under the name of its first element,
            // "positions[0]"; the base name is what the caller will ask for.
            if (name.ends_with("[0]"))
            {
                name.resize(name.size() - 3);
            }

            const GLint location = glGetUniformLocation(program, name.c_str());
            if (location < 0)
            {
                // Members of a uniform block are active but have no location of
                // their own: they are written through the buffer, not through
                // glUniform, so they do not belong in this table.
                continue;
            }

            table.emplace(std::move(name), Uniform{.location = location, .type = type});
        }

        return table;
    }

    std::expected<Shader, core::Error> Shader::create(const std::string_view name,
                                                      const std::string_view vertex_source,
                                                      const std::string_view fragment_source)
    {
        auto vertex_shader = compile_stage(name, GL_VERTEX_SHADER, vertex_source);
        if (!vertex_shader)
        {
            return std::unexpected(std::move(vertex_shader.error()));
        }

        auto fragment_shader = compile_stage(name, GL_FRAGMENT_SHADER, fragment_source);
        if (!fragment_shader)
        {
            glDeleteShader(*vertex_shader);
            return std::unexpected(std::move(fragment_shader.error()));
        }

        const GLuint program = glCreateProgram();
        glAttachShader(program, *vertex_shader);
        glAttachShader(program, *fragment_shader);
        glLinkProgram(program);

        // Deletion is deferred until after linking but needs no further condition:
        // the program holds its own reference, so these calls only release the
        // names, and the compiled objects live as long as the program does.
        glDeleteShader(*vertex_shader);
        glDeleteShader(*fragment_shader);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);

        if (linked == GL_FALSE)
        {
            core::Error failure = core::make_error(
                core::ErrorCode::COMPILATION_FAILED,
                "shader '{}': linking failed: {}",
                name, read_info_log(program, glGetProgramiv, glGetProgramInfoLog));

            glDeleteProgram(program);
            return std::unexpected(std::move(failure));
        }

        UniformTable table = collect_uniforms(program);

        log::debug(log::Category::RENDER, "shader '{}' linked as program {}, {} uniform(s)",
                   name, program, table.size());

        return Shader{std::string{name}, program, std::move(table)};
    }

    Shader::Shader(std::string name, const unsigned int program, UniformTable table) noexcept
        : shader_name(std::move(name)),
          handle(program),
          uniforms(std::move(table))
    {
    }

    Shader::~Shader()
    {
        this->destroy();
    }

    Shader::Shader(Shader&& other) noexcept
        : shader_name(std::move(other.shader_name)),
          handle(std::exchange(other.handle, 0)),
          uniforms(std::move(other.uniforms)),
          reported_misses(std::move(other.reported_misses))
    {
    }

    Shader& Shader::operator=(Shader&& other) noexcept
    {
        if (this != &other)
        {
            this->destroy();

            this->shader_name = std::move(other.shader_name);
            this->handle = std::exchange(other.handle, 0);
            this->uniforms = std::move(other.uniforms);
            this->reported_misses = std::move(other.reported_misses);
        }
        return *this;
    }

    void Shader::destroy() noexcept
    {
        // Deleting program name 0 is defined as a no-op, so a moved-from Shader
        // needs no separate handling here.
        glDeleteProgram(this->handle);
        this->handle = 0;
    }

    void Shader::bind() const
    {
        glUseProgram(this->handle);
    }

    void Shader::unbind()
    {
        glUseProgram(0);
    }

    bool Shader::has_uniform(const std::string_view name) const
    {
        return this->uniforms.find(name) != this->uniforms.end();
    }

    const Shader::Uniform* Shader::resolve(const std::string_view name,
                                           const std::initializer_list<unsigned int> accepted_types)
    {
        const auto found = this->uniforms.find(name);

        if (found == this->uniforms.end())
        {
            if (this->reported_misses.emplace(name).second)
            {
                log::error(log::Category::RENDER,
                           "shader '{}': no active uniform named '{}'; the write is ignored",
                           this->shader_name, name);
            }
            return nullptr;
        }

        if (std::ranges::find(accepted_types, found->second.type) == accepted_types.end())
        {
            if (this->reported_misses.emplace(name).second)
            {
                log::error(log::Category::RENDER,
                           "shader '{}': uniform '{}' was declared as GL type {:#x}, "
                           "which this setter cannot write; the write is ignored",
                           this->shader_name, name, found->second.type);
            }
            return nullptr;
        }

        return &found->second;
    }

    void Shader::set_uniform(const std::string_view name, const int value)
    {
        // A sampler is written exactly like an int — the value is a texture unit
        // index — and so is a bool, which GL widens on the way in.
        if (const Uniform* uniform = this->resolve(name, {GL_INT, GL_BOOL, GL_SAMPLER_2D}))
        {
            glUniform1i(uniform->location, value);
        }
    }

    void Shader::set_uniform(const std::string_view name, const float value)
    {
        if (const Uniform* uniform = this->resolve(name, {GL_FLOAT}))
        {
            glUniform1f(uniform->location, value);
        }
    }

    void Shader::set_uniform(const std::string_view name, const glm::vec2& value)
    {
        if (const Uniform* uniform = this->resolve(name, {GL_FLOAT_VEC2}))
        {
            glUniform2fv(uniform->location, 1, glm::value_ptr(value));
        }
    }

    void Shader::set_uniform(const std::string_view name, const glm::vec3& value)
    {
        if (const Uniform* uniform = this->resolve(name, {GL_FLOAT_VEC3}))
        {
            glUniform3fv(uniform->location, 1, glm::value_ptr(value));
        }
    }

    void Shader::set_uniform(const std::string_view name, const glm::vec4& value)
    {
        if (const Uniform* uniform = this->resolve(name, {GL_FLOAT_VEC4}))
        {
            glUniform4fv(uniform->location, 1, glm::value_ptr(value));
        }
    }

    void Shader::set_uniform(const std::string_view name, const glm::mat4& value)
    {
        if (const Uniform* uniform = this->resolve(name, {GL_FLOAT_MAT4}))
        {
            glUniformMatrix4fv(uniform->location, 1, GL_FALSE, glm::value_ptr(value));
        }
    }
}
