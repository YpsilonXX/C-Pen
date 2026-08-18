# The game manifest

`game.toml`, at the root of the game directory beside `assets/` and `script/`,
is where a game says what it is called, what window it wants, what resolution it
was drawn for, and where its story begins.

```toml
name = "A Game"
version = "0.1.0"

[window]
title = "A Game"
width = 1280
height = 720

[screen]
width = 1920
height = 1080
scale = "letterbox"

[story]
script = "intro"
label = "start"
```

Every field has a default, so a manifest may say as little as it likes. Nothing
in it needs a rebuild: retitling a game or moving its opening chapter is one line
and the same binary.

The file is [TOML 1.0](https://toml.io/en/v1.0.0), read through the same virtual
file system as everything else — see [assets.md](assets.md).

## What it holds

| Key | Default | Means |
|---|---|---|
| `name` | empty | What the game is called. |
| `version` | empty | The game's own version, for the game's own use. |
| `window.title` | `name` | What the title bar says. |
| `window.width`, `window.height` | 1280 × 720 | The size the window **opens** at. The player may change it at any moment. |
| `screen.width`, `screen.height` | 1920 × 1080 | The resolution the game was **drawn** for. |
| `screen.scale` | `letterbox` | `letterbox` keeps the proportions and adds bars; `stretch` fills the window and distorts. |
| `story.script` | empty | The script to play, by name: `intro`, not a path. |
| `story.label` | empty | The label to begin at. Empty starts at the top of the file. |

## The window and the screen are different things

`window` is where the game opens; `screen` is what the artwork was made for and
never changes. Everything a script positions — a sprite at `(0.5, 0.8)`, the text
box, a menu — is measured against `screen`, and the engine fits that rectangle
into whatever window the player ends up with.

So a sprite 400 pixels wide covers the same fraction of the picture on a laptop
and on a television. What a larger window buys is not more room but more
detail — and since one texel of a sprite is one pixel of the reference screen,
artwork drawn for a different resolution than `screen` will come out the wrong
size. Draw for `screen`.

## Mistakes in it

A manifest is edited by hand and never compiled, so the engine is loud about it:

- A **key nothing reads** — `strat` for `script` — is reported by name. It is the
  one mistake that would otherwise be perfectly silent: the game starts, the
  setting does nothing, and the author goes looking in the engine.
- A **value of the wrong type**, a zero resolution, or a `scale` nobody
  implements is reported, and the default is used. A game that will not start is
  a worse answer to a mistyped window height than a game that starts at 1280×720
  and says so.
- A **file that is not TOML at all** is refused outright, because then nothing in
  it can be trusted to mean what it appears to.

## Not in the manifest yet

Everything below is deliberate: it is not forgotten, it is not yet decided.

- Characters — the name plate, colour or avatar behind `alice "..."`.
- The look of the text box and the menus.
- Assets: aliases, per-asset filtering, a scale for artwork drawn at another
  resolution.
- Audio, save slots, localisation.
