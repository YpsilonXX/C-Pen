# Assets: paths and names

Every file a game loads — a background, a sprite, a typeface, a script, a sound —
is asked for by a **virtual path**:

```
sprites/alice/happy.png
fonts/ui.ttf
script/chapter_01.pen
```

Nothing above the asset layer knows where that file really is. The engine mounts
one or more **roots** and searches them in order; the first one that has the file
answers. Today a root is a directory; later it may be an archive, and no code
that asks for `fonts/ui.ttf` will change.

## Roots and shadowing

The engine mounts two directories and searches them in order: the game's own
directory first, and the engine's second.

| Order | Root | Holds |
|---|---|---|
| 1 | the game directory | everything the game ships |
| 2 | the engine directory | the defaults the engine ships |

The first hit wins, so a game that ships its own `fonts/ui.ttf` replaces the
engine's without having to know that a default existed. There is no way to ask
for a specific root: an asset has one name, and which file answers to it is the
mount order's business.

## Identifiers: what a game actually writes

A script never spells a path. It names the asset and says what it is for:

```
scene room                 -> a BACKGROUND called "room"
show alice happy           -> a SPRITE called "alice/happy"
play music theme           -> AUDIO called "theme"
```

The kind comes from the statement, the identifier from the author, and the
engine turns the pair into a path:

| Kind | Directory | Tried extensions, in order | `room` resolves to |
|---|---|---|---|
| background | `assets/bg/` | `png`, `jpg`, `jpeg` | `assets/bg/room.png` |
| sprite | `assets/sprites/` | `png`, `jpg`, `jpeg` | `assets/sprites/room.png` |
| font | `assets/fonts/` | `ttf`, `otf` | `assets/fonts/room.ttf` |
| audio | `assets/audio/` | `ogg`, `wav`, `mp3`, `flac` | `assets/audio/room.ogg` |
| script | `script/` | `pen` | `script/room.pen` |

Scripts sit beside `assets/` rather than inside it: they are the game, not
something the game shows.

Because the kind is part of the question, a background and a character may both
be called `alice` without colliding.

### Identifiers carry no extension

```
show alice happy           good
show alice happy.png       refused
```

Finding the file type is the engine's job — that is what makes `png` → `jpg`
a decision about the file and not an edit to every line that mentions it. An
identifier ending in an extension the kind knows is refused with the spelling it
should have had. A dot that is not an extension (`alice.happy`) is an ordinary
character and is fine.

Otherwise an identifier follows the same rules as a virtual path: `/` groups,
nothing else separates, and everything in the table below applies to it too.

### More than one file for one identifier

`assets/bg/room.png` and `assets/bg/room.jpg` both answer to `room`. The first
extension in the table wins — deterministically, so nothing breaks — but the
engine says so once:

```
[warn] [assets] background 'room' matches more than one file
       (assets/bg/room.png, assets/bg/room.jpg); using 'assets/bg/room.png'.
```

This is worth a line in the log because the failure mode is silent: an author who
converts a picture and leaves the original behind spends an afternoon editing a
file the game never opens. The extra probing this needs happens only while the
path audit is on, so a release build stops at the first file it finds.

### Aliases

An identifier can be pointed at a path of its own, which is how the manifest will
override the convention — for an asset kept somewhere unusual, one shared between
two names, or later an entry inside a packed atlas. An alias is checked when it
is registered (a hand-typed path with a typo is refused there and then) and again
when it is used (a well-formed path to a file that does not exist reports that it
came from an alias, so the author knows to look in the manifest and not in the
script).

## What a virtual path may contain

A virtual path is UTF-8 text, uses `/` as its only separator, and is always
relative to a root. The engine refuses anything else **before** touching the
disk, and says why.

| Refused | Example | Why |
|---|---|---|
| empty path or empty component | `bg//room.png`, `bg/` | almost always a join that added one separator too many |
| leading `/`, or a drive letter | `/etc/passwd`, `C:/fonts/arial.ttf` | an absolute path escapes the roots and means a different file on every machine |
| backslash | `sprites\alice.png` | a separator on Windows, an ordinary filename character on Linux — the same path names two different things |
| `.` and `..` | `../saves/slot_01.save` | an archive has no parent directory to climb to, and a path built from player-supplied text could otherwise reach out of the game directory |
| `:` | `bg/room:2.png` | names a drive or an alternate data stream on Windows; cannot appear in a file name there |
| control characters | a tab inside a name | not a file name on any system |
| component ending in a space or a dot | `bg/room .png`, `bg/room.` | Windows silently strips both when creating the file, so the name asked for is not the name that appears |
| reserved device names | `nul.png`, `audio/con.ogg`, `COM1`, `lpt9.txt` | Windows cannot create such a file at all; the asset would exist only on Linux |

Everything else is allowed, including non-ASCII names (`фоны/комната.png`) and
spaces inside a name (`bg/room 2.png`).

## Case sensitivity — read this one

This is the most expensive naming mistake a game can make, because the machine
that makes it is never the machine that suffers from it.

| File system | Typical on | `sprites/Alice.png` vs `sprites/alice.png` |
|---|---|---|
| case-insensitive | Windows (NTFS), macOS (APFS by default) | the same file — either spelling opens it |
| case-sensitive | Linux (ext4, btrfs, xfs), NTFS volumes with case sensitivity enabled, macOS volumes formatted case-sensitive | two different files — the wrong spelling finds nothing |

So a game authored on Windows can spell an asset one way in the script and
another way on disk, run perfectly for years, and be broken for every Linux
player the day it ships. Nothing in a diff shows it. Nothing in a build log shows
it. The author cannot reproduce it.

### What the engine does about it

The engine checks both sides of that split, and the check is not the same check
in the two places:

**When an asset loads** (the case-insensitive machine, where nothing looks
wrong), the engine reads the real name out of the directory and compares it with
the name that was asked for. A mismatch is logged as an error:

```
!!! ==================================================================== !!!
!!!  ASSET NAME CASE MISMATCH - loads here, WILL NOT LOAD on Linux
!!! ==================================================================== !!!
  asked for : sprites/Alice.png
  on disk   : sprites/alice.png
  differs   : 'Alice.png' is spelled 'alice.png'
  This file system matches names without regard to case, so the asset
  loaded and nothing looks wrong. Linux matches exactly: there the file
  is not found at all and the asset is missing.
  Fix one side so both spell it the same way - rename the file, or
  correct the identifier that asks for it. Nothing is renamed for you:
  the engine cannot see what else refers to this file.
!!! ==================================================================== !!!
```

**When an asset is missing** (the case-sensitive machine, where the load has
already failed), the engine looks for a file whose name differs only in case and,
finding one, says so in the error itself — `asset not found` becomes `found,
spelled differently`.

Each path is reported once however many times it is loaded, and **every mismatch
is repeated in a summary when the game exits**, so a report that scrolled past
during a long run cannot be missed at the end:

```
!!! ==================================================================== !!!
!!!  2 ASSET NAME(S) DIFFER FROM THE FILES ON DISK ONLY IN CASE
!!!  The same game does not run on Windows and on Linux until they agree.
!!! ==================================================================== !!!
  asked for 'sprites/alice.png' (loaded here, missing on a case-sensitive system)
      'alice.png' is spelled 'Alice.png' under '/home/author/game'
...
```

### When the check runs

| Situation | Debug build | Release build |
|---|---|---|
| asset loaded successfully | checked | not checked |
| asset missing | checked | checked |

The successful path costs one directory scan per path component, which is why it
is a development-time setting (`VirtualFileSystem::set_path_audit`) that defaults
to on in a debug build and off in a release one. The failing path costs nothing
that matters: the load has already failed, and a player's log saying exactly
which file is misspelled is worth far more than the scan.

### Limits of the check

- **Only ASCII letters are folded.** Matching `Комната` against `комната` needs
  the full Unicode case tables, which the engine does not carry. A mismatch in
  non-ASCII names is reported as a missing file, not as a case problem.
- **Nothing is renamed for you.** The engine sees the files, not the script lines
  and manifest entries that refer to them, so an automatic rename would fix one
  side and break the other. A tool that can see both — and therefore rename
  safely — is planned for the phase that introduces the script layer.

### The rule that avoids all of this

**Name every asset in lower case ASCII**, with `_` or `-` between words:

```
sprites/alice/happy_blush.png     good
sprites/Alice/HappyBlush.png      works until it does not
```

Non-ASCII names are supported and are not a mistake — but they are the names the
case check cannot help with, so a typo in one is found by a player rather than by
the engine.
