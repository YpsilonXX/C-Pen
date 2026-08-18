#ifndef CPEN_SCRIPT_SYNTAX_TREE_HH
#define CPEN_SCRIPT_SYNTAX_TREE_HH

#include "cpen/core/value.hh"
#include "cpen/script/source_span.hh"
#include "cpen/script/token.hh"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cpen::script
{
    /// How tightly an operator binds. Higher wins; NONE means "not an operator".
    enum class Precedence : std::uint8_t
    {
        NONE,
        OR,
        AND,
        EQUALITY,
        COMPARISON,
        TERM,
        FACTOR,
        UNARY,
        PRIMARY,
    };

    /// One level tighter, which is what a left-associative operator parses its
    /// right-hand side at.
    constexpr Precedence tighter(const Precedence precedence) noexcept
    {
        return static_cast<Precedence>(static_cast<std::uint8_t>(precedence) + 1);
    }

    enum class BinaryOperator : std::uint8_t
    {
#define CPEN_BINARY_OPERATOR(name, token, precedence, spelling) name,
#include "cpen/script/operators.def"
    };

    /// Kept apart from the arithmetic operators because it compiles differently:
    /// short-circuiting is a jump around the second operand, not an instruction
    /// that takes two values off the stack.
    enum class LogicalOperator : std::uint8_t
    {
#define CPEN_LOGICAL_OPERATOR(name, token, precedence, spelling) name,
#include "cpen/script/operators.def"
    };

    enum class UnaryOperator : std::uint8_t
    {
#define CPEN_UNARY_OPERATOR(name, token, spelling) name,
#include "cpen/script/operators.def"
    };

    /// The enumerator's own name, for a syntax-tree dump.
    std::string_view to_string(BinaryOperator operation) noexcept;
    std::string_view to_string(LogicalOperator operation) noexcept;
    std::string_view to_string(UnaryOperator operation) noexcept;

    /// How the operator is written in a script, for diagnostics.
    std::string_view spelling(BinaryOperator operation) noexcept;
    std::string_view spelling(LogicalOperator operation) noexcept;
    std::string_view spelling(UnaryOperator operation) noexcept;

    std::optional<BinaryOperator> find_binary_operator(TokenKind kind) noexcept;
    std::optional<LogicalOperator> find_logical_operator(TokenKind kind) noexcept;
    std::optional<UnaryOperator> find_unary_operator(TokenKind kind) noexcept;

    Precedence precedence_of(BinaryOperator operation) noexcept;
    Precedence precedence_of(LogicalOperator operation) noexcept;

    /// The precedence of whichever infix operator this token introduces, or NONE
    /// if it introduces none. The Pratt loop's whole condition.
    Precedence infix_precedence(TokenKind kind) noexcept;

    /// Nodes refer to each other by index into the tree's arena rather than by
    /// pointer.
    ///
    /// Two consequences worth the indirection: the arena can grow while a node is
    /// being built, which a pointer into it could not survive; and the whole tree
    /// is one contiguous block with no per-node allocation, which makes dumping,
    /// copying and eventually caching it trivial.
    ///
    /// Separate types for the two arenas so that a statement index cannot be
    /// passed where an expression index belongs.
    struct ExpressionId
    {
        std::uint32_t value = 0;
        bool operator==(const ExpressionId& other) const noexcept = default;
    };

    struct StatementId
    {
        std::uint32_t value = 0;
        bool operator==(const StatementId& other) const noexcept = default;
    };

    /// A number, a boolean or nil, already converted to the value the VM will push.
    struct LiteralExpression
    {
        core::Value value{};
        SourceSpan span{};
    };

    /// A text literal, still in the pieces the lexer split it into. It is a node
    /// of its own rather than a LiteralExpression holding a string because a
    /// literal with interpolation in it is not a constant: it compiles to the
    /// concatenation of a constant, a variable read and another constant.
    struct TextExpression
    {
        std::vector<TextPart> parts{};
        SourceSpan span{};
    };

    struct VariableExpression
    {
        std::string name{};
        SourceSpan span{};
    };

    struct UnaryExpression
    {
        UnaryOperator operation = UnaryOperator::NEGATE;
        ExpressionId operand{};
        SourceSpan span{};
    };

    struct BinaryExpression
    {
        BinaryOperator operation = BinaryOperator::ADD;
        ExpressionId left{};
        ExpressionId right{};
        SourceSpan span{};
    };

    struct LogicalExpression
    {
        LogicalOperator operation = LogicalOperator::AND;
        ExpressionId left{};
        ExpressionId right{};
        SourceSpan span{};
    };

    using Expression = std::variant<
        LiteralExpression,
        TextExpression,
        VariableExpression,
        UnaryExpression,
        BinaryExpression,
        LogicalExpression>;

    /// `with fade`. The name is not checked here: which transitions exist is a
    /// question for the presentation layer, and a parser that knew the list would
    /// have to be edited every time one was added.
    struct Transition
    {
        std::string name{};
        SourceSpan span{};
    };

    /// `at left`.
    struct AnchorPosition
    {
        std::string name{};
        SourceSpan span{};
    };

    /// `at (0.5, 0.8)`, in normalised screen coordinates.
    struct CoordinatePosition
    {
        ExpressionId x{};
        ExpressionId y{};
        SourceSpan span{};
    };

    using Position = std::variant<AnchorPosition, CoordinatePosition>;

    struct LabelStatement
    {
        std::string name{};
        std::vector<StatementId> body{};
        SourceSpan span{};
    };

    /// A line of dialogue. An absent speaker is narration.
    struct SayStatement
    {
        std::optional<std::string> speaker{};
        ExpressionId text{};
        SourceSpan span{};
    };

    struct AssignmentStatement
    {
        std::string variable{};
        ExpressionId value{};
        SourceSpan span{};
    };

    struct SceneStatement
    {
        std::string background{};
        std::optional<Transition> transition{};
        SourceSpan span{};
    };

    /// `show alice happy at left with dissolve`. The attributes are kept as
    /// written; joining them into an asset name is the compiler's business,
    /// because that is where the naming convention lives.
    struct ShowStatement
    {
        std::string name{};
        std::vector<std::string> attributes{};
        std::optional<Position> position{};
        std::optional<Transition> transition{};
        SourceSpan span{};
    };

    struct HideStatement
    {
        std::string name{};
        std::optional<Transition> transition{};
        SourceSpan span{};
    };

    struct JumpStatement
    {
        std::string label{};
        SourceSpan span{};
    };

    /// `call shared_scene`. Unlike a jump, it remembers where to come back to,
    /// which is what lets one scene be entered from several places without every
    /// caller having to be listed at the end of it.
    struct CallStatement
    {
        std::string label{};
        SourceSpan span{};
    };

    /// `return`. Takes no target on purpose: a call always comes back to its
    /// caller, and that invariant is what makes the call stack meaningful in a
    /// saved game. A section that has to continue somewhere else says so through
    /// the caller, after the return.
    struct ReturnStatement
    {
        SourceSpan span{};
    };

    struct PauseStatement
    {
        ExpressionId duration{};
        SourceSpan span{};
    };

    /// One `if` or `elif` and the block it guards.
    struct ConditionalBranch
    {
        ExpressionId condition{};
        std::vector<StatementId> body{};
        SourceSpan span{};
    };

    /// `if`, its `elif`s and its `else` as one node: they are one construct to the
    /// author and one chain of jumps to the compiler, and splitting them into
    /// separate statements would leave the parser to prove they belong together.
    struct IfStatement
    {
        std::vector<ConditionalBranch> branches{};
        std::vector<StatementId> otherwise{};
        SourceSpan span{};
    };

    struct MenuChoice
    {
        ExpressionId prompt{};
        std::vector<StatementId> body{};
        SourceSpan span{};
    };

    struct MenuStatement
    {
        std::vector<MenuChoice> choices{};
        SourceSpan span{};
    };

    using Statement = std::variant<
        LabelStatement,
        SayStatement,
        AssignmentStatement,
        SceneStatement,
        ShowStatement,
        HideStatement,
        JumpStatement,
        CallStatement,
        ReturnStatement,
        PauseStatement,
        IfStatement,
        MenuStatement>;

    SourceSpan span_of(const Expression& expression) noexcept;
    SourceSpan span_of(const Statement& statement) noexcept;

    /// Every node of one parsed script, in two arenas.
    ///
    /// Nothing here is recursive in the C++ sense: a node holds indices, so the
    /// variants are complete types and the whole tree is two vectors. Adding a
    /// node kind is an alternative in one of the variants — and then a compile
    /// error at every std::visit over it, which is the point of the design: the
    /// compiler enumerates the places that have to learn about the new node
    /// instead of leaving them to be found at run time.
    class SyntaxTree
    {
    public:
        ExpressionId add(Expression expression);
        StatementId add(Statement statement);

        const Expression& expression(ExpressionId id) const;
        const Statement& statement(StatementId id) const;

        /// The statements at the outermost level of the file, in order.
        const std::vector<StatementId>& roots() const noexcept { return this->top_level; }

        void add_root(const StatementId id) { this->top_level.push_back(id); }

        std::size_t expression_count() const noexcept { return this->expressions.size(); }
        std::size_t statement_count() const noexcept { return this->statements.size(); }

    private:
        std::vector<Expression> expressions{};
        std::vector<Statement> statements{};
        std::vector<StatementId> top_level{};
    };

    /// Renders the tree as indented text, one node per line.
    ///
    ///     label 'start'
    ///       say speaker='alice'
    ///         text "Привет, " /{name} "!"
    ///
    /// The same tool as dump_tokens and for the same reason: a change to the
    /// grammar should read as a diff over the scripts it affects.
    std::string dump_syntax_tree(const SyntaxTree& tree);
}

#endif //CPEN_SCRIPT_SYNTAX_TREE_HH
