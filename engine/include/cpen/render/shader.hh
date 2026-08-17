#ifndef CPEN_RENDER_SHADER_HH
#define CPEN_RENDER_SHADER_HH

#include "cpen/core/error.hh"

#include <glm/glm.hpp>

#include <cstddef>
#include <expected>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cpen::render
{
    namespace detail
    {
        /// Enables heterogeneous lookup in the uniform table: with this hash and a
        /// transparent comparator, find() accepts a std::string_view without
        /// materialising a std::string, so setting a uniform allocates nothing.
        struct StringHash
        {
            using is_transparent = void;

            std::size_t operator()(const std::string_view text) const noexcept
            {
                return std::hash<std::string_view>{}(text);
            }
        };
    }

    /// A linked GL program: a vertex and a fragment stage plus the table of
    /// uniforms the driver kept after linking.
    ///
    /// Sources are passed in as text, never as file paths. The shaders the engine
    /// needs — sprite batch, text — are engine assets rather than game assets:
    /// the engine has to work whether or not the game shipped any .glsl files, so
    /// they belong in the binary. Loading a shader from disk is a separate
    /// concern that can be layered on later without touching this interface.
    ///
    /// Ownership is move-only, the same shape as core::Subscription. The one rule
    /// that matters: a Shader must not outlive the GL context, since its
    /// destructor calls into GL. That is arranged structurally — shaders are owned
    /// by the renderer, which the application declares after the window — rather
    /// than checked at runtime.
    class Shader
    {
    public:
        /// Compiles both stages and links them.
        ///
        /// `name` is not decoration: it is echoed in the log and in every error
        /// message, and a diagnostic that does not say which shader failed is
        /// most of the way to useless.
        static std::expected<Shader, core::Error> create(std::string_view name,
                                                         std::string_view vertex_source,
                                                         std::string_view fragment_source);

        ~Shader();

        Shader(Shader&& other) noexcept;
        Shader& operator=(Shader&& other) noexcept;

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        /// Const because binding changes GL's global state, not this program's.
        void bind() const;
        static void unbind();

        /// Non-const, in contrast: uniform values are stored in the program
        /// object itself, so writing one really does mutate what this handle
        /// owns. The distinction is not pedantry — it is why a renderer holding
        /// shaders by const reference can draw with them but not reconfigure them.
        ///
        /// Setting a uniform the program does not have is reported once and then
        /// ignored, and so is a type that disagrees with what the driver reported
        /// at link time. Both are programming errors, but neither is worth
        /// aborting a frame over, and both are otherwise invisible: GL answers a
        /// bad location with silence and draws something plausible.
        void set_uniform(std::string_view name, int value);
        void set_uniform(std::string_view name, float value);
        void set_uniform(std::string_view name, const glm::vec2& value);
        void set_uniform(std::string_view name, const glm::vec3& value);
        void set_uniform(std::string_view name, const glm::vec4& value);
        void set_uniform(std::string_view name, const glm::mat4& value);

        bool has_uniform(std::string_view name) const;

        /// Number of uniforms the driver kept after linking. Note that this counts
        /// what survived: a uniform no instruction reads is optimised away and
        /// will not appear here even though it is written in the source.
        std::size_t uniform_count() const noexcept { return this->uniforms.size(); }

        std::string_view name() const noexcept { return this->shader_name; }

        /// The raw program name, for the few places that must talk to GL directly.
        /// GLuint is unsigned int on every platform the engine targets, which is
        /// what lets this header stay free of the GL headers.
        unsigned int id() const noexcept { return this->handle; }

    private:
        struct Uniform
        {
            int location = -1;
            unsigned int type = 0;
        };

        using UniformTable =
            std::unordered_map<std::string, Uniform, detail::StringHash, std::equal_to<>>;

        Shader(std::string name, unsigned int program, UniformTable table) noexcept;

        /// Enumerates the uniforms the driver kept after linking.
        ///
        /// Done once, at construction, rather than lazily on first use. The table
        /// is then immutable for the program's lifetime: no map insertion ever
        /// happens while drawing, a lookup miss is conclusive rather than merely
        /// "not asked for yet", and the declared types come along for free, which
        /// is what makes the check in resolve() possible at all.
        static UniformTable collect_uniforms(unsigned int program);

        void destroy() noexcept;

        /// Resolves a uniform and checks it against the type the driver reported
        /// at link time, returning nullptr if the name is unknown or the type
        /// disagrees.
        ///
        /// Several GL types share one setter — a sampler is written with
        /// glUniform1i exactly like an int — so the check takes the set of types
        /// the caller's overload can legitimately write, not a single one.
        ///
        /// Not const, and not only because it records what it has complained
        /// about: a rejected name is a fixed property of the program, so without
        /// that record the same mistake would be logged every frame and drown the
        /// console at sixty lines a second.
        const Uniform* resolve(std::string_view name,
                               std::initializer_list<unsigned int> accepted_types);

        std::string shader_name;
        unsigned int handle = 0;
        UniformTable uniforms;

        /// Names already reported as missing or mistyped; diagnostics only.
        std::unordered_set<std::string, detail::StringHash, std::equal_to<>> reported_misses;
    };
}

#endif //CPEN_RENDER_SHADER_HH
