#include "cpen/script/syntax_tree.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <utility>

namespace cpen::script
{
    namespace
    {
        constexpr std::size_t BINARY_OPERATOR_COUNT = 0
#define CPEN_BINARY_OPERATOR(name, token, precedence, spelling) + 1
#include "cpen/script/operators.def"
            ;

        constexpr std::size_t LOGICAL_OPERATOR_COUNT = 0
#define CPEN_LOGICAL_OPERATOR(name, token, precedence, spelling) + 1
#include "cpen/script/operators.def"
            ;

        constexpr std::size_t UNARY_OPERATOR_COUNT = 0
#define CPEN_UNARY_OPERATOR(name, token, spelling) + 1
#include "cpen/script/operators.def"
            ;

        constexpr std::array<std::string_view, BINARY_OPERATOR_COUNT> BINARY_NAMES = {
#define CPEN_BINARY_OPERATOR(name, token, precedence, spelling) #name,
#include "cpen/script/operators.def"
        };

        constexpr std::array<std::string_view, BINARY_OPERATOR_COUNT> BINARY_SPELLINGS = {
#define CPEN_BINARY_OPERATOR(name, token, precedence, spelling) spelling,
#include "cpen/script/operators.def"
        };

        constexpr std::array<Precedence, BINARY_OPERATOR_COUNT> BINARY_PRECEDENCES = {
#define CPEN_BINARY_OPERATOR(name, token, precedence, spelling) Precedence::precedence,
#include "cpen/script/operators.def"
        };

        constexpr std::array<std::string_view, LOGICAL_OPERATOR_COUNT> LOGICAL_NAMES = {
#define CPEN_LOGICAL_OPERATOR(name, token, precedence, spelling) #name,
#include "cpen/script/operators.def"
        };

        constexpr std::array<std::string_view, LOGICAL_OPERATOR_COUNT> LOGICAL_SPELLINGS = {
#define CPEN_LOGICAL_OPERATOR(name, token, precedence, spelling) spelling,
#include "cpen/script/operators.def"
        };

        constexpr std::array<Precedence, LOGICAL_OPERATOR_COUNT> LOGICAL_PRECEDENCES = {
#define CPEN_LOGICAL_OPERATOR(name, token, precedence, spelling) Precedence::precedence,
#include "cpen/script/operators.def"
        };

        constexpr std::array<std::string_view, UNARY_OPERATOR_COUNT> UNARY_NAMES = {
#define CPEN_UNARY_OPERATOR(name, token, spelling) #name,
#include "cpen/script/operators.def"
        };

        constexpr std::array<std::string_view, UNARY_OPERATOR_COUNT> UNARY_SPELLINGS = {
#define CPEN_UNARY_OPERATOR(name, token, spelling) spelling,
#include "cpen/script/operators.def"
        };

        template <typename OperatorType>
        struct OperatorEntry
        {
            TokenKind token;
            OperatorType operation;
        };

        constexpr auto BINARY_TOKENS = std::to_array<OperatorEntry<BinaryOperator>>({
#define CPEN_BINARY_OPERATOR(name, token, precedence, spelling) {TokenKind::token, BinaryOperator::name},
#include "cpen/script/operators.def"
        });

        constexpr auto LOGICAL_TOKENS = std::to_array<OperatorEntry<LogicalOperator>>({
#define CPEN_LOGICAL_OPERATOR(name, token, precedence, spelling) {TokenKind::token, LogicalOperator::name},
#include "cpen/script/operators.def"
        });

        constexpr auto UNARY_TOKENS = std::to_array<OperatorEntry<UnaryOperator>>({
#define CPEN_UNARY_OPERATOR(name, token, spelling) {TokenKind::token, UnaryOperator::name},
#include "cpen/script/operators.def"
        });

        template <typename OperatorType, std::size_t Count>
        std::string_view look_up(const std::array<std::string_view, Count>& table,
                                 const OperatorType operation) noexcept
        {
            const auto index = static_cast<std::size_t>(operation);
            return index < Count ? table[index] : "unknown";
        }

        template <typename OperatorType, std::size_t Count>
        std::optional<OperatorType> match(const std::array<OperatorEntry<OperatorType>, Count>& table,
                                          const TokenKind kind) noexcept
        {
            const auto found = std::ranges::find(table, kind, &OperatorEntry<OperatorType>::token);
            return found != table.end() ? std::optional(found->operation) : std::nullopt;
        }

        /// Lets a std::visit be written as a list of lambdas, one per alternative.
        ///
        /// Deliberately without a generic fallback anywhere it is used: a new node
        /// kind must fail to compile at every place that walks the tree, which is
        /// the whole reason the tree is a variant.
        template <typename... Handlers>
        struct Overloaded : Handlers...
        {
            using Handlers::operator()...;
        };

        std::string quote(const std::string_view text)
        {
            return std::format("'{}'", text);
        }

        std::string describe_transition(const std::optional<Transition>& transition)
        {
            return transition.has_value() ? std::format(" with {}", quote(transition->name)) : "";
        }

        class TreeDumper
        {
        public:
            explicit TreeDumper(const SyntaxTree& parsed)
                : tree(parsed)
            {
            }

            std::string run()
            {
                for (const StatementId id : this->tree.roots())
                {
                    this->write_statement(id, 0);
                }

                return std::move(this->output);
            }

        private:
            void write(const std::size_t depth, const std::string_view text)
            {
                this->output.append(depth * 2, ' ');
                this->output.append(text);
                this->output.push_back('\n');
            }

            void write_block(const std::vector<StatementId>& body, const std::size_t depth)
            {
                for (const StatementId id : body)
                {
                    this->write_statement(id, depth);
                }
            }

            void write_statement(const StatementId id, const std::size_t depth)
            {
                std::visit(Overloaded{
                    [&](const LabelStatement& node)
                    {
                        this->write(depth, std::format("label {}", quote(node.name)));
                        this->write_block(node.body, depth + 1);
                    },
                    [&](const SayStatement& node)
                    {
                        this->write(depth, node.speaker.has_value()
                                               ? std::format("say speaker={}", quote(*node.speaker))
                                               : "say");
                        this->write_expression(node.text, depth + 1);
                    },
                    [&](const AssignmentStatement& node)
                    {
                        this->write(depth, std::format("assign {}", quote(node.variable)));
                        this->write_expression(node.value, depth + 1);
                    },
                    [&](const SceneStatement& node)
                    {
                        this->write(depth, std::format("scene {}{}", quote(node.background),
                                                       describe_transition(node.transition)));
                    },
                    [&](const ShowStatement& node)
                    {
                        std::string line = std::format("show {}", quote(node.name));
                        for (const std::string& attribute : node.attributes)
                        {
                            line += " " + quote(attribute);
                        }

                        const CoordinatePosition* coordinates = nullptr;
                        if (node.position.has_value())
                        {
                            if (const auto* anchor = std::get_if<AnchorPosition>(&*node.position))
                            {
                                line += std::format(" at anchor {}", quote(anchor->name));
                            }
                            else
                            {
                                coordinates = &std::get<CoordinatePosition>(*node.position);
                                line += " at coordinates";
                            }
                        }

                        this->write(depth, line + describe_transition(node.transition));

                        if (coordinates != nullptr)
                        {
                            this->write_expression(coordinates->x, depth + 1);
                            this->write_expression(coordinates->y, depth + 1);
                        }
                    },
                    [&](const HideStatement& node)
                    {
                        this->write(depth, std::format("hide {}{}", quote(node.name),
                                                       describe_transition(node.transition)));
                    },
                    [&](const JumpStatement& node)
                    {
                        this->write(depth, std::format("jump {}", quote(node.label)));
                    },
                    [&](const CallStatement& node)
                    {
                        this->write(depth, std::format("call {}", quote(node.label)));
                    },
                    [&](const ReturnStatement&)
                    {
                        this->write(depth, "return");
                    },
                    [&](const PauseStatement& node)
                    {
                        this->write(depth, "pause");
                        this->write_expression(node.duration, depth + 1);
                    },
                    [&](const IfStatement& node)
                    {
                        this->write(depth, "if");
                        for (const ConditionalBranch& branch : node.branches)
                        {
                            this->write(depth + 1, "branch");
                            this->write(depth + 2, "condition");
                            this->write_expression(branch.condition, depth + 3);
                            this->write(depth + 2, "body");
                            this->write_block(branch.body, depth + 3);
                        }

                        if (!node.otherwise.empty())
                        {
                            this->write(depth + 1, "else");
                            this->write_block(node.otherwise, depth + 2);
                        }
                    },
                    [&](const MenuStatement& node)
                    {
                        this->write(depth, "menu");
                        for (const MenuChoice& choice : node.choices)
                        {
                            this->write(depth + 1, "choice");
                            this->write(depth + 2, "prompt");
                            this->write_expression(choice.prompt, depth + 3);
                            this->write(depth + 2, "body");
                            this->write_block(choice.body, depth + 3);
                        }
                    },
                }, this->tree.statement(id));
            }

            void write_expression(const ExpressionId id, const std::size_t depth)
            {
                std::visit(Overloaded{
                    [&](const LiteralExpression& node)
                    {
                        this->write(depth, std::format("literal {}", node.value.to_string()));
                    },
                    [&](const TextExpression& node)
                    {
                        this->write(depth, std::format("text {}", dump_text_parts(node.parts)));
                    },
                    [&](const VariableExpression& node)
                    {
                        this->write(depth, std::format("variable {}", quote(node.name)));
                    },
                    [&](const UnaryExpression& node)
                    {
                        this->write(depth, std::format("unary {}", to_string(node.operation)));
                        this->write_expression(node.operand, depth + 1);
                    },
                    [&](const BinaryExpression& node)
                    {
                        this->write(depth, std::format("binary {}", to_string(node.operation)));
                        this->write_expression(node.left, depth + 1);
                        this->write_expression(node.right, depth + 1);
                    },
                    [&](const LogicalExpression& node)
                    {
                        this->write(depth, std::format("logical {}", to_string(node.operation)));
                        this->write_expression(node.left, depth + 1);
                        this->write_expression(node.right, depth + 1);
                    },
                }, this->tree.expression(id));
            }

            const SyntaxTree& tree;
            std::string output;
        };
    }

    std::string_view to_string(const BinaryOperator operation) noexcept
    {
        return look_up(BINARY_NAMES, operation);
    }

    std::string_view to_string(const LogicalOperator operation) noexcept
    {
        return look_up(LOGICAL_NAMES, operation);
    }

    std::string_view to_string(const UnaryOperator operation) noexcept
    {
        return look_up(UNARY_NAMES, operation);
    }

    std::string_view spelling(const BinaryOperator operation) noexcept
    {
        return look_up(BINARY_SPELLINGS, operation);
    }

    std::string_view spelling(const LogicalOperator operation) noexcept
    {
        return look_up(LOGICAL_SPELLINGS, operation);
    }

    std::string_view spelling(const UnaryOperator operation) noexcept
    {
        return look_up(UNARY_SPELLINGS, operation);
    }

    std::optional<BinaryOperator> find_binary_operator(const TokenKind kind) noexcept
    {
        return match(BINARY_TOKENS, kind);
    }

    std::optional<LogicalOperator> find_logical_operator(const TokenKind kind) noexcept
    {
        return match(LOGICAL_TOKENS, kind);
    }

    std::optional<UnaryOperator> find_unary_operator(const TokenKind kind) noexcept
    {
        return match(UNARY_TOKENS, kind);
    }

    Precedence precedence_of(const BinaryOperator operation) noexcept
    {
        const auto index = static_cast<std::size_t>(operation);
        return index < BINARY_OPERATOR_COUNT ? BINARY_PRECEDENCES[index] : Precedence::NONE;
    }

    Precedence precedence_of(const LogicalOperator operation) noexcept
    {
        const auto index = static_cast<std::size_t>(operation);
        return index < LOGICAL_OPERATOR_COUNT ? LOGICAL_PRECEDENCES[index] : Precedence::NONE;
    }

    Precedence infix_precedence(const TokenKind kind) noexcept
    {
        if (const std::optional<BinaryOperator> binary = find_binary_operator(kind))
        {
            return precedence_of(*binary);
        }

        if (const std::optional<LogicalOperator> logical = find_logical_operator(kind))
        {
            return precedence_of(*logical);
        }

        return Precedence::NONE;
    }

    SourceSpan span_of(const Expression& expression) noexcept
    {
        // Generic on purpose, unlike the dumper: every node carries a span by
        // construction, and a new node kind that did not would fail to compile
        // here for the right reason.
        return std::visit([](const auto& node) { return node.span; }, expression);
    }

    SourceSpan span_of(const Statement& statement) noexcept
    {
        return std::visit([](const auto& node) { return node.span; }, statement);
    }

    ExpressionId SyntaxTree::add(Expression expression)
    {
        this->expressions.push_back(std::move(expression));
        return ExpressionId{static_cast<std::uint32_t>(this->expressions.size() - 1)};
    }

    StatementId SyntaxTree::add(Statement statement)
    {
        this->statements.push_back(std::move(statement));
        return StatementId{static_cast<std::uint32_t>(this->statements.size() - 1)};
    }

    const Expression& SyntaxTree::expression(const ExpressionId id) const
    {
        if (id.value >= this->expressions.size())
        {
            throw std::out_of_range("script: expression index out of range");
        }

        return this->expressions[id.value];
    }

    const Statement& SyntaxTree::statement(const StatementId id) const
    {
        if (id.value >= this->statements.size())
        {
            throw std::out_of_range("script: statement index out of range");
        }

        return this->statements[id.value];
    }

    std::string dump_syntax_tree(const SyntaxTree& tree)
    {
        return TreeDumper(tree).run();
    }
}
