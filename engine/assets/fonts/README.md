# Typefaces shipped with the engine

## default.ttf

An unmodified copy of **DejaVu Sans** (the `DejaVuSans.ttf` face), renamed so that
a game can replace it simply by shipping its own `assets/fonts/default.ttf` — the
game's root is mounted first, so its file wins with no configuration anywhere.
The typeface's own internal name is untouched and still reads "DejaVu Sans", so
nothing about what this file *is* has been obscured.

Chosen for its coverage: Latin, Cyrillic and Greek in one face, which is the
minimum for an engine whose first game is written in Russian. It is not chosen
for beauty — it is what is there when a game has not said what to use, and what
the engine's own diagnostics are drawn with.

Licence: see `LICENSE-DejaVu.txt` (Bitstream Vera Fonts Copyright and the DejaVu
changes notice). It permits redistribution, including as part of a larger
package, and requires the copyright notice to travel with the font — which is why
that file is here rather than only in a list of credits.

Identifier: `default` (kind FONT), i.e. `AssetManager::default_font(pixel_size)`.
