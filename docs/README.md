# C-Pen documentation

- [English](en/) — the source of truth; every other language is a translation of
  these files.

For a game author, the two files to read are
[the script language](en/dsl.md) and [assets: paths and names](en/assets.md).

Translations live in sibling directories named after the language code (`ru/`,
`de/`, ...) and keep the same file names, so a link into one language can be
rewritten into another by changing one path component.

The repository wiki, when it exists, is generated from these files rather than
edited separately: a wiki page that has drifted from the source tree is worse
than no page, because nothing marks it as out of date.
