# The script language

A game's story is written in `.pen` files: text, in a language built for it. This
is the whole of that language as it stands.

```pen
label start:
	scene bg_room with fade
	show alice happy at left

	"Дверь скрипнула."
	alice "Привет!"

	$ sympathy = 0

	menu:
		"Поздороваться":
			$ sympathy = sympathy + 1
			alice "И тебе привет."
		"Промолчать":
			alice "..."

	if sympathy > 0:
		alice "У тебя /{sympathy} очков симпатии."
	else:
		alice "Ну и ладно."

	hide alice with dissolve
	jump hallway
```

The language is deliberately small. It is an **orchestrator**: it says what
happens and in what order, and leaves how anything looks to the engine. When it
grows, it will grow towards describing behaviour — not towards describing
rendering.

> **The language may still change.** Until version 1.0 a rule here can be
> replaced by a better one. Every such change is listed at the end of this file,
> so a script written against an older version can be brought up to date by
> reading one section rather than by testing everything.

## Indentation: read this one first

Blocks are made with indentation, not with braces or an `end` keyword. There is
one rule, and it is about the file rather than about you:

**The first indented line in a file decides whether that file is written with
tabs or with spaces. Every later indent must use the same character.**

Neither is privileged. Tabs are what the engine recommends and what its own
examples use, but a file indented entirely with spaces is just as correct. Mixing
them is refused, and the message names both the offending line and the line where
the file made its choice:

```
chapter1.pen:14:1: error: this line is indented with spaces, but the file is
indented with tabs (first indent on line 2)
```

The reason for allowing both is practical. Most editors insert spaces when you
press Tab, and text copied from a web page has usually had its tabs converted
already; a tabs-only language would refuse files that look perfectly correct. The
reason for demanding consistency is that without it the width of an indent level
is a guess, and a guess is how a block silently belongs to the wrong `if`.

Depth is otherwise up to you — one tab, four spaces, eight spaces — as long as a
block is deeper than the line that opened it.

Blank lines and lines holding nothing but a comment are ignored entirely: their
indentation is invisible and is not held to the rule, and they never close a
block.

## Comments

`#` begins a comment, which runs to the end of the line. There is no block
comment.

```pen
$ sympathy = 0     # she starts out indifferent
```

## Dialogue

A line of text on its own is narration. A name in front of it is a character
speaking.

```pen
"Дверь скрипнула."
alice "Привет!"
```

Nothing has to be declared first: `alice` is passed through to the engine as
written, and what it draws — a name plate, a colour, an avatar — is the game's
business. Character declarations will arrive with the game manifest.

### Text literals

A literal is written between double quotes and ends on the line it starts on. Four
escape sequences exist:

| Written | Means |
|---|---|
| `\"` | a double quote |
| `\\` | a backslash |
| `\n` | a line break inside the text box |
| `\/` | a forward slash, needed only to write `\/{` |

Anything else after a backslash is an error — and, so that one typo does not eat a
character, the character itself is kept.

### Putting a variable into a line

`/{name}` is replaced by the value of that variable:

```pen
alice "У тебя /{sympathy} очков."
```

The marker is `/{` and not `{` for two reasons. A brace in ordinary prose stays an
ordinary brace and needs no escaping, and the marker is visible while the line is
being written — interpolation is the one place where a line of dialogue stops
being only text, and that should be obvious at a glance. A literal `/{` is written
`\/{`.

Only a **variable name** may go between the braces. Anything to compute belongs on
a `$` line above, where it is visible to anyone skimming the script for logic:

```pen
$ remaining = total - spent
alice "Осталось /{remaining}."
```

Any type of value can be interpolated. A variable that was never assigned reads as
`nil` and appears in the line as `nil` — visible, and therefore noticed and fixed,
rather than stopping the scene halfway through a chapter.

## Variables and values

`$` assigns:

```pen
$ sympathy = 0
$ sympathy = sympathy + 1
$ name = "Алиса"
$ met_her = true
```

Variables need no declaration and are shared with the rest of the engine, so a
mini-game or a piece of game-specific C++ reads and writes exactly what the script
does. Reading one that was never assigned gives `nil`.

There are five kinds of value:

| Kind | Written | Notes |
|---|---|---|
| nothing | `nil` | also what an unassigned variable reads as |
| boolean | `true`, `false` | |
| integer | `0`, `42` | whole numbers |
| number | `0.5`, `2.0` | fractional numbers |
| text | `"..."` | UTF-8 |

**Integers and fractional numbers are different types**, and arithmetic keeps them
apart on purpose:

```pen
$ a = 1 / 2       # 0    -- two integers divide to an integer
$ b = 1.0 / 2     # 0.5  -- a mixed pair promotes
```

That is not an accident to be worked around. Counting things — points, items, days
— is integer work, and a counter that quietly becomes `2.9999999` is a worse
outcome than one that truncates where you asked it to.

Dividing by zero is refused, in both kinds, and stops the script with a message
naming the line. A number printing as `inf` in the middle of a sentence is a much
worse way to find out.

`+` also joins two pieces of text. It will not join a number to a piece of text —
use interpolation for that, which is what it is for.

Comparison with `<`, `<=`, `>`, `>=` works on numbers only. **Text has no order**,
because ordering it would mean answering whether `Ё` comes before `Я`, and the
engine carries no such table; a byte comparison would give an answer that looks
right and is wrong in every language with an alphabet.

`==` and `!=` compare anything, and treat the two numeric types as comparable:
`1 == 1.0` is true, and `1 == "1"` is false.

### What counts as false

**Only `nil` and `false`.** Zero is true. Empty text is true.

```pen
if sympathy:          # true even when sympathy is 0
if sympathy > 0:      # what you probably meant
```

This is deliberate. Making zero false is the oldest trap in scripting: a counter
that legitimately reaches zero, or a name that is legitimately blank, silently
takes the other branch, and the condition reads as though it were asking whether
the variable has been set.

### Names

A name may hold letters, digits and `_`, may not begin with a digit, and may be
written in any alphabet:

```pen
$ симпатия = 0
$ sympathy_with_alice = 0
```

These words are reserved and cannot be used as names: `label`, `jump`, `call`,
`return`, `scene`, `show`, `hide`, `menu`, `if`, `elif`, `else`, `pause`, `at`,
`with`, `and`, `or`, `not`, `true`, `false`, `nil`.

## Conditions

```pen
if sympathy > 3 and not shy:
	alice "Ты милый."
elif sympathy > 0:
	alice "Ну, спасибо."
else:
	alice "Ну и ладно."
```

`and`, `or` and `not` are words rather than symbols: a script file is mostly prose,
and `&&` in the middle of it is noise. `!` is not an operator here, and writing it
says so.

`and` and `or` stop as soon as the answer is known, so the right-hand side of an
`and` is not evaluated when the left is false.

Operators bind in this order, loosest first. Everything at the same level groups to
the left, so `1 - 2 - 3` is `(1 - 2) - 3`.

| Level | Operators |
|---|---|
| 1 | `or` |
| 2 | `and` |
| 3 | `==` `!=` |
| 4 | `<` `<=` `>` `>=` |
| 5 | `+` `-` |
| 6 | `*` `/` `%` |
| 7 | `-x` `not x` |

Parentheses override all of it.

## Choices

```pen
menu:
	"Поздороваться":
		$ sympathy = sympathy + 1
		alice "И тебе привет."
	"Промолчать":
		alice "..."
```

Each choice is a line of text and a block. A menu needs at least one choice. After
whichever block ran, the script continues after the menu.

Prompts are ordinary text literals, so a choice may name a variable:

```pen
menu:
	"Отдать /{coins} монет":
		...
```

Choices that appear only under a condition are not in the language yet.

## Labels, and moving between them

A **label** names a piece of the story:

```pen
label hallway:
	"В коридоре темно."
	jump kitchen
```

Three rules, and they are worth reading together because each one exists to keep
the other two simple.

**A label's body is entered only by `jump` or `call`.** Reaching a label while
running the lines above it does not run its body; execution steps over it.

**The end of a body is a return.** If the label was reached by `call`, the script
continues after that call. If it was reached by `jump`, or started there, the
script ends.

**The order of labels in a file means nothing.** You may jump forward or back, and
moving a label from one end of the file to the other changes nothing at all. This
is the point of the two rules above: in languages where one label falls through
into the next, rearranging two chapters silently rearranges the story.

The cost is one line: consecutive chapters need an explicit `jump` between them.

```pen
label chapter1:
	"Конец первой главы."
	jump chapter2        # ← without this, the story ends here

label chapter2:
	"Вторая глава."
```

A label may only appear at the outermost level — not inside another label, and not
inside an `if` or a `menu` block. Every label in a script needs a name of its own.

### Reusing a scene: `call` and `return`

`jump` goes somewhere. `call` goes somewhere **and remembers where it came from**:

```pen
label kitchen:
	call the_lights_flicker
	"Вернулись на кухню."

label hall:
	call the_lights_flicker
	"Вернулись в холл."

label the_lights_flicker:
	"Свет мигнул."
	# the end of the body returns to whoever called
```

This is what to reach for whenever one scene is entered from several places. With
`jump` alone the shared scene has to end by testing a variable to work out where to
go back to, and every new caller means editing the shared scene — the dependency
runs backwards.

`return` ends a body early. It takes no destination: a call always comes back to
its caller. Somewhere else to continue is the caller's decision, after the return.

A call may call further. Calls nested more than 256 deep are reported as a script
calling itself without end.

## The stage

```pen
scene bg_room with fade
show alice happy at left
show alice sad at (0.5, 0.8) with dissolve
hide alice with dissolve
```

`scene` changes the background. `show` puts a sprite on screen, `hide` takes one
off.

**The words after `show` become the asset name.** `show alice happy` is the sprite
at `alice/happy`; see [assets.md](assets.md) for how that name becomes a file. Any
number of words may follow the first.

A position is optional, and comes in two forms. A **named anchor** is a word the
engine interprets, and may be reinterpreted when a layout changes:

```pen
show alice at left
```

**Coordinates** are exact, relative to the reference screen — `(0, 0)` is the top
left, `(1, 1)` the bottom right — so they mean the same thing at every window size.
They are ordinary expressions and may be computed:

```pen
show alice at (0.5, 0.8)
show alice at (position + 0.1, 0.8)
```

A transition is optional and written `with name`. Without one the change is
immediate.

The language does not check which anchors and which transitions exist: those are
the engine's vocabulary, not the language's, and a script does not have to be
re-approved every time one is added. Which places and which transitions this
engine knows, and what it does with them, is in [stage.md](stage.md).

None of the three waits. A transition takes time on screen, but the story goes on
over it — which is what a novel does, and what the next line expects.

## Waiting

```pen
pause 0.5
```

Waits that many seconds. The number may be computed. A negative wait is no wait.

## Where a script starts

Statements written outside any label run first, in the order they appear:

```pen
"Это выполняется сразу."

label start:
	"А это — только если сюда прыгнуть."
```

A file made entirely of labels therefore does nothing on its own; something has to
start it at a named label. Which label a game begins at will be part of the game
manifest.

## Grammar

For anyone who needs the exact shape rather than the description. `indent`,
`dedent` and `newline` are produced by the indentation rules above.

```ebnf
script      = { statement } ;

statement   = label | conditional | menu | assignment | say
            | scene | show | hide | jump | call | return | pause ;

label       = "label" identifier block ;
conditional = "if" expression block
              { "elif" expression block }
              [ "else" block ] ;
menu        = "menu" ":" newline indent choice { choice } dedent ;
choice      = text block ;
assignment  = "$" identifier "=" expression newline ;
say         = [ identifier ] text newline ;
scene       = "scene" identifier [ transition ] newline ;
show        = "show" identifier { identifier } [ position ] [ transition ] newline ;
hide        = "hide" identifier [ transition ] newline ;
jump        = "jump" identifier newline ;
call        = "call" identifier newline ;
return      = "return" newline ;
pause       = "pause" expression newline ;

block       = ":" newline indent statement { statement } dedent ;
position    = "at" ( identifier | "(" expression "," expression ")" ) ;
transition  = "with" identifier ;

expression  = disjunction ;
disjunction = conjunction { "or" conjunction } ;
conjunction = equality { "and" equality } ;
equality    = comparison { ( "==" | "!=" ) comparison } ;
comparison  = term { ( "<" | "<=" | ">" | ">=" ) term } ;
term        = factor { ( "+" | "-" ) factor } ;
factor      = unary { ( "*" | "/" | "%" ) unary } ;
unary       = ( "-" | "not" ) unary | primary ;
primary     = number | text | identifier
            | "true" | "false" | "nil"
            | "(" expression ")" ;
```

## Not in the language yet

Named so that nobody has to guess whether they were forgotten:

- **Functions** — parameters, local variables, a returned value. `call` and
  `return` are subroutines and nothing more. Functions are what the language grows
  towards, for describing the behaviour of characters.
- **Sound** — music and effects.
- **Save points and history** — declaring where a game may be saved.
- **Conditional menu choices** — a choice that appears only when a condition holds.
- **Arrays and collections.**
- **Text spanning more than one line**, and exponent notation for numbers
  (`1e-3`).
- **Anything to do with layout** — layers, z-order, sizes. Deliberately: a script
  says what happens, and the further it goes into how things look the less of it
  survives a change of art.

## Changes to the language

Nothing yet: this is the first version. Every later change that could stop an
existing script from compiling, or change what it does, is recorded here with what
to write instead.
