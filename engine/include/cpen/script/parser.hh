#ifndef CPEN_SCRIPT_PARSER_HH
#define CPEN_SCRIPT_PARSER_HH

#include "cpen/script/diagnostic.hh"
#include "cpen/script/syntax_tree.hh"
#include "cpen/script/token.hh"

#include <vector>

namespace cpen::script
{
    /// What one run of the parser produced. Shaped like TokenizeResult and for the
    /// same reason: a script with one bad statement in it still has a tree worth
    /// reporting the rest of the problems against.
    struct ParseResult
    {
        SyntaxTree tree;
        std::vector<Diagnostic> diagnostics;

        bool failed() const noexcept { return has_errors(this->diagnostics); }
    };

    /// Builds a syntax tree from a token stream.
    ///
    /// The stream is borrowed, not kept: the tree copies the names and literals it
    /// needs, so it outlives the tokens it was built from.
    ///
    /// A statement that cannot be parsed is reported and skipped — along with the
    /// block it opened, so that the statements inside a mistyped `if` are not
    /// reported one by one as though each were wrong on its own. Parsing then
    /// continues, and one run reports every statement the file got wrong.
    ParseResult parse(const TokenStream& stream);
}

#endif //CPEN_SCRIPT_PARSER_HH
