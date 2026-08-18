#include "cpen/script/script.hh"

#include "cpen/script/compiler.hh"

#include <utility>

namespace cpen::script
{
    std::expected<Script, std::vector<Diagnostic>> make_script(std::string name,
                                                               std::string source)
    {
        // The text is copied for the compiler rather than moved into it, because
        // the Script keeps the original: a compiler that consumed it would leave
        // nothing to render a run-time fault against.
        std::expected<Chunk, std::vector<Diagnostic>> compiled = compile_script(name, source);

        if (!compiled.has_value())
        {
            return std::unexpected(std::move(compiled.error()));
        }

        return Script{
            .name = std::move(name),
            .source = std::move(source),
            .chunk = std::move(*compiled),
        };
    }
}
