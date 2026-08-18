#ifndef CPEN_SCRIPT_SCRIPT_HH
#define CPEN_SCRIPT_SCRIPT_HH

#include "cpen/script/chunk.hh"
#include "cpen/script/diagnostic.hh"

#include <expected>
#include <string>
#include <vector>

namespace cpen::script
{
    /// One compiled script file, and the text it was compiled from.
    ///
    /// The source is kept rather than dropped after compilation because a fault
    /// happens at run time and is reported in terms of the file: the machine's
    /// RuntimeFault carries a span, and turning a span into the line an author
    /// can read requires the text the span indexes into. A chunk on its own can
    /// only say that something went wrong at instruction 214.
    ///
    /// The name is the file the text came from, spelt the way the engine found it,
    /// so that the heading of a diagnostic is something a person can look up.
    struct Script
    {
        std::string name{};
        std::string source{};
        Chunk chunk{};
    };

    /// Compiles source text into a Script, keeping the text beside the bytecode.
    ///
    /// The failure is the compiler's own diagnostic list rather than a rendered
    /// string: whoever asked for the script decides whether that becomes a log
    /// line, a message on screen or a list in an editor.
    std::expected<Script, std::vector<Diagnostic>> make_script(std::string name,
                                                               std::string source);
}

#endif //CPEN_SCRIPT_SCRIPT_HH
