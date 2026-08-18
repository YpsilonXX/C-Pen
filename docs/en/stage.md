# The stage: anchors, layers and the text box

[The script language](dsl.md) says what happens; this file says what the engine
does about it. The two are deliberately separate: `at left` and `with fade` are
words the language passes through untouched, so the engine may learn a new place
or a new transition without the language changing and without a single script
having to be re-approved.

## Where a sprite stands

A sprite's **placement point** is the middle of its bottom edge. A character
stands on the floor line, and characters of different heights stand on the same
one without the script saying so.

```
        ┌──────────┐  ┌──────┐
        │          │  │      │
        │  alice   │  │ bob  │
        └────●─────┘  └──●───┘
             ↑           ↑
        placement points, on the floor line
```

Coordinates put that point where they say: `(0, 0)` is the top left of the
reference screen and `(1, 1)` the bottom right.

```pen
show alice at (0.35, 1.0)
```

## The anchors

An **anchor** is a name for a place. All of them are on the floor line; they
differ only in how far across the screen they are.

| Anchor | Across |
|---|---|
| `offscreen_left` | −0.2 — beyond the edge, out of sight |
| `far_left` | 0.12 |
| `left` | 0.25 |
| `center`, `centre` | 0.5 |
| `right` | 0.75 |
| `far_right` | 0.88 |
| `offscreen_right` | 1.2 — beyond the other edge |

Both spellings of the middle are accepted; the engine writes `center` when it
writes one back.

The two off-screen places exist for the same reason the far ones do: a sprite
that will one day walk in from the side has to be somewhere before it does.
Nothing moves towards them yet.

A `show` with no place at all stands at the centre. A place the engine does not
know is **reported once and treated as the centre** — a misspelt anchor is a
mistake worth hearing about, and it is not worth stopping a scene over.

## Layers

The words after `show` become an asset name — `show alice happy` is
`alice/happy` — and everything before the first `/` is the **layer**.

That one rule decides three things:

- `hide alice` takes the sprite off, whichever face it was wearing.
- `show alice sad` after `show alice happy` **replaces** her rather than standing
  a second Alice beside the first.
- A sprite that changes expression **keeps its place in the drawing order**. A
  character must not step in front of the one beside her merely because she
  smiled.

Sprites are drawn in the order they were first shown, over the background.

Hiding something that is not on the stage is reported and changes nothing. The
usual cause is a misspelt name, which would otherwise leave the sprite on screen
for the rest of the scene.

## Sizes

One texel of a picture is one pixel of the reference screen. Backgrounds are the
exception: a background is stretched over the whole screen, so one exported a few
pixels short leaves no seam down the side.

The reference screen is `[screen]` in [the manifest](manifest.md), 1920 × 1080
unless a game says otherwise. **Draw for it.** A character exported 2160 pixels
tall will stand two screens high, because nothing is scaled to fit: what a
picture measures is what it covers.

## Transitions

`with fade`, `with dissolve` — any word — is carried to the presentation layer
and **has no effect yet**. A change is immediate. The names are accepted today so
that scripts written now do not have to be revisited when transitions arrive.

None of `scene`, `show` or `hide` waits, transition or not: a transition takes
time on screen, but the story goes on over it.

`scene` changes the background and **leaves the sprites where they are**. Take
them off with `hide` when a scene changes under them.

## The text box

The box sits across the bottom of the screen. A speaker's name is written above
the line; narration has no name and no gap where one would have been.

A line **types itself out** rather than appearing at once. The reader's first
click, space or enter finishes the line; the second goes on to the next. Nobody
who reads faster than the typewriter has to wait for it.

The line is wrapped from the whole text, not from the part shown so far, so words
already read never jump between lines as the rest arrives.

## Choices

Choices are stacked and centred in the space above the text box, in the order the
script wrote them. A reader answers with

- a **click** on the choice,
- a **number key**, `1` to `9`, counting from the top,
- the **arrows** to move the highlight and enter or space to take it.

Nothing is highlighted until the reader picks something out: a menu that opened
with an answer already chosen would invite taking it by accident. For the same
reason, space answers nothing until the arrows have moved somewhere, and a click
beside the menu answers nothing at all.

## When something goes wrong

A script that will not compile, a label the manifest names but the file does not
have, and a fault while the story runs all put the same thing on screen: an
opaque page with the file, the line, the column and a caret under what was
running. The stage is emptied under it — a scene left behind would suggest the
story was still going.

## Not decided yet

- The look of the box and the choices — colours, sizes and the typeface — is a
  value the engine holds and nothing in the manifest reaches yet.
- Name plates, colours and avatars per character.
- A sprite scale for artwork drawn at a resolution other than the screen's.
- Transitions, and anything that moves.
