#ifndef CPEN_SCRIPT_COMPILER_HH
#define CPEN_SCRIPT_COMPILER_HH

#include "cpen/script/chunk.hh"
#include "cpen/script/diagnostic.hh"
#include "cpen/script/syntax_tree.hh"

#include <expected>
#include <string>
#include <vector>

namespace cpen::script
{
    /// Turns a syntax tree into bytecode.
    ///
    /// Unlike the phases before it this returns std::expected, because its caller
    /// is not a tool inspecting a half-built result: it either has a chunk to run
    /// or it has the reasons it does not. Everything wrong with the tree is still
    /// collected in one pass — the list is what the failure carries.
    ///
    /// The checks that live here are the ones a parser cannot make, because they
    /// are about the script as a whole rather than about one statement: a label
    /// defined twice, a jump to a label that does not exist, a label nested inside
    /// another.
    ///
    /// The tree is read, never kept: a chunk owns copies of every name and every
    /// piece of text it needs and outlives the tree it came from.
    std::expected<Chunk, std::vector<Diagnostic>> compile(const SyntaxTree& tree);

    /// Lexes, parses and compiles one script.
    ///
    /// Stops at the first phase that fails rather than reporting all three at
    /// once: a file the lexer could not read produces a tree that is wrong in ways
    /// that say nothing about the script, and burying one real mistake under them
    /// is how a compiler becomes something people stop reading.
    std::expected<Chunk, std::vector<Diagnostic>> compile_script(std::string source_name,
                                                                 std::string source);
}

#endif //CPEN_SCRIPT_COMPILER_HH
