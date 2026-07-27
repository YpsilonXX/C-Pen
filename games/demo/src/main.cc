#include <cpen/app/application.hh>
#include <cpen/core/log.hh>
#include <cpen/runtime/game_state.hh>
#include <cpen/runtime/state_stack.hh>

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

using namespace cpen;

namespace
{
    /// Positions arrive already in clip space, so no transform is applied: the
    /// point of this shader pair is to prove that the GL 3.3 core pipeline works
    /// end to end, not to establish a coordinate system. The projection matrix
    /// and the virtual 1920x1080 reference resolution belong to the render layer.
    ///
    /// The per-vertex colour is passed through and interpolated by the rasteriser,
    /// which makes a successful draw unmistakable: a uniform-coloured triangle
    /// could still come from a shader that ignores its inputs.
    constexpr const char* TRIANGLE_VERTEX_SHADER = R"(#version 330 core
layout (location = 0) in vec2 in_position;
layout (location = 1) in vec3 in_color;

out vec3 vertex_color;

void main()
{
    vertex_color = in_color;
    gl_Position = vec4(in_position, 0.0, 1.0);
}
)";

    constexpr const char* TRIANGLE_FRAGMENT_SHADER = R"(#version 330 core
in vec3 vertex_color;

out vec4 fragment_color;

void main()
{
    fragment_color = vec4(vertex_color, 1.0);
}
)";

    /// Interleaved vertex data: two position components followed by three colour
    /// components, per vertex. Interleaving rather than one buffer per attribute
    /// is what the sprite batch will use as well, so the layout is worth
    /// rehearsing here.
    constexpr std::array<float, 15> TRIANGLE_VERTICES = {
        // position      colour
        -0.6f, -0.5f,    1.0f, 0.0f, 0.0f,
         0.6f, -0.5f,    0.0f, 1.0f, 0.0f,
         0.0f,  0.6f,    0.0f, 0.0f, 1.0f,
    };

    constexpr GLuint POSITION_ATTRIBUTE = 0;
    constexpr GLuint COLOR_ATTRIBUTE = 1;

    constexpr GLsizei VERTEX_STRIDE = static_cast<GLsizei>(5 * sizeof(float));
    constexpr std::uintptr_t POSITION_OFFSET = 0;
    constexpr std::uintptr_t COLOR_OFFSET = 2 * sizeof(float);

    /// Reads back the driver's diagnostic for a shader or program object.
    ///
    /// The shader and program variants differ only in the entry points used, which
    /// is why the retrieval is parameterised rather than duplicated. The glad
    /// typedefs are used verbatim: they carry the platform calling convention,
    /// which a hand-written function-pointer type would drop on Windows. The
    /// program-side typedefs name the same function types, so passing
    /// glGetProgramiv and glGetProgramInfoLog here is exact, not a conversion.
    std::string_view read_info_log(
        const GLuint object,
        const PFNGLGETSHADERIVPROC get_parameter,
        const PFNGLGETSHADERINFOLOGPROC get_log,
        const std::span<GLchar> buffer)
    {
        GLint length = 0;
        get_parameter(object, GL_INFO_LOG_LENGTH, &length);
        if (length <= 0)
        {
            return "(no diagnostic)";
        }

        GLsizei written = 0;
        get_log(object, static_cast<GLsizei>(buffer.size()), &written, buffer.data());
        return std::string_view{buffer.data(), static_cast<std::size_t>(written)};
    }

    /// Compiles one shader stage. Returns 0 on failure, having logged the
    /// driver's diagnostic.
    GLuint compile_shader(const GLenum stage, const char* source)
    {
        const GLuint shader = glCreateShader(stage);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_FALSE)
        {
            std::array<GLchar, 1024> buffer{};
            log::error(log::Category::RENDER, "shader compilation failed: {}",
                       read_info_log(shader, glGetShaderiv, glGetShaderInfoLog, buffer));
            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }

    /// Compiles and links the two stages into a program. Returns 0 on failure.
    ///
    /// The shader objects are deleted immediately after linking: the program keeps
    /// them alive through its own reference, so the names may be released as soon
    /// as they are attached and nothing else will refer to them again.
    GLuint link_triangle_program()
    {
        const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, TRIANGLE_VERTEX_SHADER);
        if (vertex_shader == 0)
        {
            return 0;
        }

        const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, TRIANGLE_FRAGMENT_SHADER);
        if (fragment_shader == 0)
        {
            glDeleteShader(vertex_shader);
            return 0;
        }

        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex_shader);
        glAttachShader(program, fragment_shader);
        glLinkProgram(program);

        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            std::array<GLchar, 1024> buffer{};
            log::error(log::Category::RENDER, "program linking failed: {}",
                       read_info_log(program, glGetProgramiv, glGetProgramInfoLog, buffer));
            glDeleteProgram(program);
            return 0;
        }

        return program;
    }

    /// F0/F1 smoke test: an otherwise empty state that clears the window, draws a
    /// single triangle and quits on Escape. Its purposes are to prove that a state
    /// drives the engine through the stack rather than through a loop written in
    /// the game, and that the GL 3.3 core context obtained by the platform layer
    /// can actually compile a shader and rasterise geometry.
    ///
    /// TODO(F1): every GL call below moves into the render layer. A state will ask
    /// for a clear colour and submit sprites instead of owning buffer names, and
    /// the shader plumbing becomes render::Shader with std::expected error paths.
    class DemoState final : public runtime::GameState
    {
    public:
        std::string_view name() const override { return "demo"; }

        void on_enter() override
        {
            log::info(log::Category::APP, "demo state entered");
            this->create_triangle();
        }

        /// GL objects are released here rather than in the destructor because the
        /// stack calls on_exit() while the context is still current: Application
        /// declares the stack after the window, so the stack is destroyed first and
        /// the context outlives every state.
        void on_exit() override
        {
            this->destroy_triangle();
        }

        bool handle_event(const platform::Event& event) override
        {
            if (const auto* key = std::get_if<platform::KeyEvent>(&event))
            {
                if (key->key == platform::Key::ESCAPE &&
                    key->action == platform::InputAction::PRESS)
                {
                    log::info(log::Category::APP, "escape pressed, leaving the demo state");

                    // Popping the last state empties the stack, which ends the
                    // loop: quitting needs no separate channel back to the
                    // application.
                    this->stack().pop();
                    return true;
                }
            }
            else if (const auto* resize = std::get_if<platform::ResizeEvent>(&event))
            {
                glViewport(0, 0,
                           static_cast<GLsizei>(resize->width),
                           static_cast<GLsizei>(resize->height));
                log::debug(log::Category::APP, "framebuffer resized to {}x{}",
                           resize->width, resize->height);
                return true;
            }

            return false;
        }

        void render() override
        {
            glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            if (this->shader_program == 0)
            {
                return;
            }

            glUseProgram(this->shader_program);
            glBindVertexArray(this->vertex_array);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glUseProgram(0);
        }

    private:
        void create_triangle()
        {
            this->shader_program = link_triangle_program();
            if (this->shader_program == 0)
            {
                log::error(log::Category::RENDER, "triangle program unavailable, drawing nothing");
                return;
            }

            // The core profile has no default vertex array object, so one must be
            // bound before any attribute state can be recorded.
            glGenVertexArrays(1, &this->vertex_array);
            glBindVertexArray(this->vertex_array);

            glGenBuffers(1, &this->vertex_buffer);
            glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(sizeof(TRIANGLE_VERTICES)),
                         TRIANGLE_VERTICES.data(),
                         GL_STATIC_DRAW);

            // The attribute pointers are recorded into the bound vertex array,
            // together with the buffer they read from; the binding above is
            // therefore part of the state being captured, not a prerequisite of
            // the draw call.
            glVertexAttribPointer(POSITION_ATTRIBUTE, 2, GL_FLOAT, GL_FALSE, VERTEX_STRIDE,
                                  reinterpret_cast<const void*>(POSITION_OFFSET));
            glEnableVertexAttribArray(POSITION_ATTRIBUTE);

            glVertexAttribPointer(COLOR_ATTRIBUTE, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE,
                                  reinterpret_cast<const void*>(COLOR_OFFSET));
            glEnableVertexAttribArray(COLOR_ATTRIBUTE);

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            log::info(log::Category::RENDER, "triangle ready: program {}, array {}, buffer {}",
                      this->shader_program, this->vertex_array, this->vertex_buffer);
        }

        void destroy_triangle()
        {
            // Deleting name 0 is defined as a no-op, so the partially constructed
            // case needs no separate handling.
            glDeleteBuffers(1, &this->vertex_buffer);
            glDeleteVertexArrays(1, &this->vertex_array);
            glDeleteProgram(this->shader_program);

            this->vertex_buffer = 0;
            this->vertex_array = 0;
            this->shader_program = 0;
        }

        GLuint shader_program = 0;
        GLuint vertex_array = 0;
        GLuint vertex_buffer = 0;
    };
}

/// Entry point for the C-Pen demo game executable.
int main()
{
    log::initialize_console();
    log::info(log::Category::APP, "C-Pen demo starting");

    app::Application application;
    application.states().push(std::make_unique<DemoState>());
    application.run();

    log::info(log::Category::APP, "C-Pen demo shutting down");
    return 0;
}
