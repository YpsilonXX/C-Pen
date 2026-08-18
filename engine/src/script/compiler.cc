#include "cpen/script/compiler.hh"

#include "cpen/script/lexer.hh"
#include "cpen/script/parser.hh"

#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

namespace cpen::script
{
    namespace
    {
        template <typename... Handlers>
        struct Overloaded : Handlers...
        {
            using Handlers::operator()...;
        };

        class Compiler
        {
        public:
            explicit Compiler(const SyntaxTree& parsed)
                : tree(parsed)
            {
            }

            std::expected<Chunk, std::vector<Diagnostic>> run()
            {
                for (const StatementId id : this->tree.roots())
                {
                    if (const auto* label = std::get_if<LabelStatement>(&this->tree.statement(id)))
                    {
                        this->compile_label(*label);
                    }
                    else
                    {
                        this->compile_statement(id);
                    }
                }

                // Whatever ran at the outermost level has run out.
                this->code.emit(OpCode::HALT, SourceSpan{});

                this->resolve_jumps();

                if (has_errors(this->diagnostics))
                {
                    return std::unexpected(std::move(this->diagnostics));
                }

                return std::move(this->code);
            }

        private:
            /// A jump or a call whose target was not compiled yet.
            ///
            /// Every label reference is deferred rather than resolved on sight: a
            /// script jumps forward as often as back, and a compiler that demanded
            /// otherwise would make the order of a file part of its meaning.
            struct LabelReference
            {
                std::uint32_t instruction = 0;
                std::string name{};
                SourceSpan span{};
            };

            template <typename... Arguments>
            void error(const SourceSpan span, const std::format_string<Arguments...> format,
                       Arguments&&... arguments)
            {
                this->diagnostics.push_back(make_diagnostic(Severity::ERROR, span, format,
                                                            std::forward<Arguments>(arguments)...));
            }

            std::uint32_t here() const { return this->code.size(); }

            void compile_label(const LabelStatement& node)
            {
                // Stepped over rather than fallen into: a label's body is entered
                // by `jump` or `call` and by nothing else, which is what keeps the
                // order of labels in a file from meaning anything.
                const std::uint32_t skip = this->code.emit(OpCode::JUMP, 0, node.span);

                if (!this->code.add_label(node.name, this->here()))
                {
                    this->error(node.span, "there is already a label called '{}'", node.name);
                }

                this->compile_block(node.body);

                // The end of a body returns: to whoever called it, or out of the
                // script when nobody did.
                this->code.emit(OpCode::RETURN, node.span);
                this->code.patch_address(skip, this->here());
            }

            void compile_block(const std::vector<StatementId>& body)
            {
                for (const StatementId id : body)
                {
                    this->compile_statement(id);
                }
            }

            void compile_statement(const StatementId id)
            {
                std::visit(Overloaded{
                    [&](const LabelStatement& node)
                    {
                        // The parser accepts this; the meaning is what cannot be
                        // settled. Indentation reads as a sub-block, habit reads as
                        // a bookmark, and the two run the script differently --
                        // while the rule that a body ends in a return only holds
                        // for the first of them.
                        this->error(node.span,
                                    "the label '{}' is inside another label; a label may only "
                                    "appear at the outermost level. Write it as a label of its own "
                                    "and jump to it",
                                    node.name);
                    },
                    [&](const SayStatement& node)
                    {
                        const std::uint32_t speaker = this->code.add_constant(
                            node.speaker.has_value() ? core::Value(*node.speaker) : core::Value());

                        this->compile_expression(node.text);
                        this->code.emit(OpCode::SAY, speaker, node.span);
                    },
                    [&](const AssignmentStatement& node)
                    {
                        this->compile_expression(node.value);
                        this->code.emit(OpCode::STORE_GLOBAL,
                                        this->code.add_global(node.variable), node.span);
                    },
                    [&](const SceneStatement& node)
                    {
                        PresentationRecord record{
                            .target = node.background,
                            .transition = node.transition.has_value() ? node.transition->name
                                                                      : std::string{},
                        };

                        this->code.emit(OpCode::SCENE, this->code.add_command(std::move(record)),
                                        node.span);
                    },
                    [&](const ShowStatement& node)
                    {
                        this->compile_show(node);
                    },
                    [&](const HideStatement& node)
                    {
                        PresentationRecord record{
                            .target = node.name,
                            .transition = node.transition.has_value() ? node.transition->name
                                                                      : std::string{},
                        };

                        this->code.emit(OpCode::HIDE, this->code.add_command(std::move(record)),
                                        node.span);
                    },
                    [&](const JumpStatement& node)
                    {
                        this->reference_label(OpCode::JUMP, node.label, node.span);
                    },
                    [&](const CallStatement& node)
                    {
                        this->reference_label(OpCode::CALL, node.label, node.span);
                    },
                    [&](const ReturnStatement& node)
                    {
                        this->code.emit(OpCode::RETURN, node.span);
                    },
                    [&](const PauseStatement& node)
                    {
                        this->compile_expression(node.duration);
                        this->code.emit(OpCode::PAUSE, node.span);
                    },
                    [&](const IfStatement& node)
                    {
                        this->compile_if(node);
                    },
                    [&](const MenuStatement& node)
                    {
                        this->compile_menu(node);
                    },
                }, this->tree.statement(id));
            }

            void compile_show(const ShowStatement& node)
            {
                PresentationRecord record;

                // The words the author wrote become the asset name here, because
                // this is where the naming convention lives: `show alice happy`
                // means the sprite at "alice/happy".
                record.target = node.name;
                for (const std::string& attribute : node.attributes)
                {
                    record.target += "/";
                    record.target += attribute;
                }

                if (node.position.has_value())
                {
                    if (const auto* anchor = std::get_if<AnchorPosition>(&*node.position))
                    {
                        record.anchor = anchor->name;
                    }
                    else
                    {
                        const auto& coordinates = std::get<CoordinatePosition>(*node.position);

                        // Pushed before the instruction that consumes them, across
                        // first, so the machine finds them in a known order.
                        this->compile_expression(coordinates.x);
                        this->compile_expression(coordinates.y);
                        record.position_on_stack = true;
                    }
                }

                if (node.transition.has_value())
                {
                    record.transition = node.transition->name;
                }

                this->code.emit(OpCode::SHOW, this->code.add_command(std::move(record)), node.span);
            }

            void compile_if(const IfStatement& node)
            {
                std::vector<std::uint32_t> exits;

                for (const ConditionalBranch& branch : node.branches)
                {
                    this->compile_expression(branch.condition);

                    // The jump reads the condition without removing it, so both
                    // paths pop it: the one that runs the body, and the one that
                    // lands on the next branch.
                    const std::uint32_t next = this->code.emit(OpCode::JUMP_IF_FALSE, 0, branch.span);
                    this->code.emit(OpCode::POP, branch.span);
                    this->compile_block(branch.body);
                    exits.push_back(this->code.emit(OpCode::JUMP, 0, branch.span));

                    this->code.patch_address(next, this->here());
                    this->code.emit(OpCode::POP, branch.span);
                }

                this->compile_block(node.otherwise);

                for (const std::uint32_t exit : exits)
                {
                    this->code.patch_address(exit, this->here());
                }
            }

            void compile_menu(const MenuStatement& node)
            {
                // Prompts first, in the order they are written, so the machine can
                // read them off the stack in that order.
                for (const MenuChoice& choice : node.choices)
                {
                    this->compile_expression(choice.prompt);
                }

                // Reserved now, filled once the blocks exist: the instruction has
                // to come before the code its choices lead to.
                const std::uint32_t table = this->code.add_menu_table(MenuTable{});
                this->code.emit(OpCode::MENU, table, node.span);

                std::vector<std::uint32_t> targets;
                std::vector<std::uint32_t> exits;

                for (const MenuChoice& choice : node.choices)
                {
                    targets.push_back(this->here());
                    this->compile_block(choice.body);
                    exits.push_back(this->code.emit(OpCode::JUMP, 0, choice.span));
                }

                for (const std::uint32_t exit : exits)
                {
                    this->code.patch_address(exit, this->here());
                }

                this->code.set_menu_table(table, MenuTable{.targets = std::move(targets)});
            }

            void reference_label(const OpCode instruction, const std::string& name,
                                 const SourceSpan span)
            {
                this->references.push_back(LabelReference{
                    .instruction = this->code.emit(instruction, 0, span),
                    .name = name,
                    .span = span,
                });
            }

            void resolve_jumps()
            {
                for (const LabelReference& reference : this->references)
                {
                    const std::optional<std::uint32_t> address = this->code.find_label(reference.name);
                    if (!address.has_value())
                    {
                        this->error(reference.span, "there is no label called '{}'", reference.name);
                        continue;
                    }

                    this->code.patch_address(reference.instruction, *address);
                }
            }

            void compile_expression(const ExpressionId id)
            {
                std::visit(Overloaded{
                    [&](const LiteralExpression& node)
                    {
                        this->compile_literal(node);
                    },
                    [&](const TextExpression& node)
                    {
                        this->compile_text(node);
                    },
                    [&](const VariableExpression& node)
                    {
                        this->code.emit(OpCode::LOAD_GLOBAL,
                                        this->code.add_global(node.name), node.span);
                    },
                    [&](const UnaryExpression& node)
                    {
                        this->compile_expression(node.operand);
                        this->code.emit(node.operation == UnaryOperator::NEGATE ? OpCode::NEGATE
                                                                                : OpCode::NOT,
                                        node.span);
                    },
                    [&](const BinaryExpression& node)
                    {
                        this->compile_expression(node.left);
                        this->compile_expression(node.right);
                        this->code.emit(opcode_for(node.operation), node.span);
                    },
                    [&](const LogicalExpression& node)
                    {
                        // Short-circuiting is a jump around the second operand, and
                        // the value that decided it stays on the stack as the
                        // result -- which is why the conditional jumps do not pop.
                        this->compile_expression(node.left);

                        const std::uint32_t skip = this->code.emit(
                            node.operation == LogicalOperator::AND ? OpCode::JUMP_IF_FALSE
                                                                   : OpCode::JUMP_IF_TRUE,
                            0, node.span);

                        this->code.emit(OpCode::POP, node.span);
                        this->compile_expression(node.right);
                        this->code.patch_address(skip, this->here());
                    },
                }, this->tree.expression(id));
            }

            void compile_literal(const LiteralExpression& node)
            {
                switch (node.value.type())
                {
                    case core::Value::Type::NIL:
                        this->code.emit(OpCode::PUSH_NIL, node.span);
                        return;

                    case core::Value::Type::BOOLEAN:
                        // Their own instructions rather than constants: they are the
                        // three values a script writes most often, and each costs
                        // one byte instead of five and a slot in the pool.
                        this->code.emit(node.value.as_boolean().value() ? OpCode::PUSH_TRUE
                                                                        : OpCode::PUSH_FALSE,
                                        node.span);
                        return;

                    default:
                        this->code.emit(OpCode::PUSH_CONSTANT,
                                        this->code.add_constant(node.value), node.span);
                        return;
                }
            }

            void compile_text(const TextExpression& node)
            {
                if (node.parts.size() == 1)
                {
                    if (const auto* chunk = std::get_if<TextChunk>(&node.parts.front()))
                    {
                        // A line with nothing interpolated into it is a constant,
                        // which is the overwhelming majority of the lines in a
                        // novel.
                        this->code.emit(OpCode::PUSH_CONSTANT,
                                        this->code.add_constant(core::Value(chunk->text)),
                                        node.span);
                        return;
                    }
                }

                for (const TextPart& part : node.parts)
                {
                    if (const auto* chunk = std::get_if<TextChunk>(&part))
                    {
                        this->code.emit(OpCode::PUSH_CONSTANT,
                                        this->code.add_constant(core::Value(chunk->text)),
                                        node.span);
                    }
                    else
                    {
                        const auto& interpolation = std::get<TextInterpolation>(part);
                        this->code.emit(OpCode::LOAD_GLOBAL,
                                        this->code.add_global(interpolation.variable),
                                        interpolation.span);
                    }
                }

                this->code.emit(OpCode::CONCATENATE,
                                static_cast<std::uint32_t>(node.parts.size()), node.span);
            }

            static OpCode opcode_for(const BinaryOperator operation) noexcept
            {
                switch (operation)
                {
                    case BinaryOperator::ADD:              return OpCode::ADD;
                    case BinaryOperator::SUBTRACT:         return OpCode::SUBTRACT;
                    case BinaryOperator::MULTIPLY:         return OpCode::MULTIPLY;
                    case BinaryOperator::DIVIDE:           return OpCode::DIVIDE;
                    case BinaryOperator::REMAINDER:        return OpCode::REMAINDER;
                    case BinaryOperator::EQUAL:            return OpCode::EQUAL;
                    case BinaryOperator::NOT_EQUAL:        return OpCode::NOT_EQUAL;
                    case BinaryOperator::LESS:             return OpCode::LESS;
                    case BinaryOperator::LESS_OR_EQUAL:    return OpCode::LESS_OR_EQUAL;
                    case BinaryOperator::GREATER:          return OpCode::GREATER;
                    case BinaryOperator::GREATER_OR_EQUAL: return OpCode::GREATER_OR_EQUAL;
                }

                return OpCode::ADD;
            }

            const SyntaxTree& tree;
            Chunk code;
            std::vector<Diagnostic> diagnostics;
            std::vector<LabelReference> references;
        };
    }

    std::expected<Chunk, std::vector<Diagnostic>> compile(const SyntaxTree& tree)
    {
        return Compiler(tree).run();
    }

    std::expected<Chunk, std::vector<Diagnostic>> compile_script(std::string source_name,
                                                                 std::string source)
    {
        TokenizeResult lexed = tokenize(std::move(source_name), std::move(source));
        if (lexed.failed())
        {
            return std::unexpected(std::move(lexed.diagnostics));
        }

        ParseResult parsed = parse(lexed.stream);
        if (parsed.failed())
        {
            return std::unexpected(std::move(parsed.diagnostics));
        }

        return compile(parsed.tree);
    }
}
