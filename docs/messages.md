# Messages

How the checkout's dialogue text is read, encoded as the build encodes it, laid out and drawn
as the game draws a text box, edited, and saved. Dialogue chains, which actor code picks and
follows by ID, are out of scope.

## Reading them

The build compiles every message from `assets/text/message_data.h`, a table of
`DEFINE_MESSAGE(id, type, position, MSG(japanese), MSG(english), MSG(german), MSG(french))`
rows. In a checkout Toki has not saved, it includes the vanilla rows from
`extracted/<version>/text/message_data.h` and adds the checkout's own after them.

`readMessages` reads that table with `cparser::readTable`, so its `#if`s are answered as the
build answers them ([build-configuration.md](build-configuration.md)): a row under a switch
that is off is not read. A row's text is kept as pieces, never as bytes: strings, new lines,
and control codes (`COLOR(RED)`, `ITEM_ICON(ITEM_POCKET_EGG)`) with their arguments as written.

Besides `DEFINE_MESSAGE` the table has three other row macros, and they decide which
language's table a row is compiled into:

| Macro | In the tables of |
|---|---|
| `DEFINE_MESSAGE` | every language |
| `DEFINE_MESSAGE_JPN` | Japanese only |
| `DEFINE_MESSAGE_NES` | English, German and French only |
| `DEFINE_MESSAGE_FFFC` | the font message: Japanese on NTSC builds, English on PAL ones (`OOT_NTSC`) |

So one ID may be two rows, each with its own box. The game searches a language's table in
order and finds the first row of an ID only, which is why a row defined again is a warning,
and why a checkout cannot change a vanilla message by adding a row of its ID after it.

## Encoding

`MessageEncoder` turns a text into the bytes the build puts into `nes_message_data_static`
as the build does it: each string through the checkout's `charmap.nes.txt`, first match wins,
a character it does not map as its UTF-8 bytes; each control code through the macros of
`message_data_fmt.h` under `MESSAGE_DATA_STATIC`, its arguments evaluated against the
system headers; `END` after the last. Every message of a vanilla checkout encodes byte for
byte as the build's `.rodata` (the checkout tests check all 2126 rows of `ntsc-1.2`).

Japanese goes into `jpn_message_data_static` as `msgenc.py --encoding SHIFT-JIS --wchar`
writes it: through `charmap.jpn.txt`, every code it maps two bytes, every other character
its Shift-JIS bytes, and the control macros under `MESSAGE_WCHAR` as well, which makes
each code and each of its arguments a 16-bit word. Shift-JIS is a table Toki ships
(`assets/shift_jis.txt`, made from Python's `shift_jis` codec, which `msgenc.py` uses),
since no dependency converts to it on every platform. It too matches the build byte for
byte.

## Layout

`layoutMessage` lays the bytes out box by box as `Message_Decode` and `Message_DrawText` of
`src/code/z_message.c` do for English, German and French on a 4:3 screen of 320 by 240.

What is data in that file is read from the checkout (`readMessageGeometry`): the font's
widths (`sFontWidths`) and where each box type stands (the position tables of
`Message_Update`). What is code is written down in Toki and is the limit of what it follows:

- text starts at x 65, lines are 12 apart, and a character advances `(s32)(width * 0.75f)`,
  a space 6;
- the first line's y is the box's plus 8, or 26, 20 or 16 for one, two or three lines,
  except for a box without a frame; `BACKGROUND` counts as two lines;
- a line after a new line is indented by 32 where an item icon or choices take the left;
- an item icon is 32 pixels square, a quest item's (from `ITEM_MEDALLION_FOREST`) 24;
- the colors of `COLOR` switch by box type, as `Message_SetTextColor` does;
- drawing stops at a box break, `TEXTID`, `EVENT`, `FADE`, `FADE2`, `PERSISTENT` and `END`,
  and what closes the box decides its end icon;
- the player's name is drawn as `Link`, times as `00"00"`, scores as `0`.

`layoutWideMessage` lays out Japanese as `Message_Decode` and `Message_DrawTextWide` do on
an NTSC build. Its codes are read as words and do what the narrow codes do, with these
differences:

- text starts at x 50, lines are 18 apart, and a character advances 14
  (`(s32)(16 * 0.88f)`); `（` and `「` move back 6 first, `・` 3, `？`, `！`, `＾` and `１` 2;
  after it, `．` advances 3, `）` and `」` 5, `、`, `。` and `”` 7, `＾`, `＃` and `＊` 9,
  `・` 10;
- the first line's y is the box's plus 6, or 22 or 14 for one or two lines;
- a line after a new line is indented by 32 for an item icon or two choices; three choices
  indent every line;
- the item icon stands 54 left of the text, a quest item's 50; the choices are 7, 25 and 43
  below the box's top;
- characters come from the `kanji` font, at the offset `Kanji_OffsetFromShiftJIS` finds:
  kanji by arithmetic, everything below them through `sNonKanjiIndices`, read from the
  checkout's `src/code/kanread.s`;
- the player's name is drawn as `リンク`, times as `００分００秒`.

A change to that code in the checkout is not seen.

## Drawing

A box is drawn the way the game draws it: `writeBoxDisplayList` writes the display list
`Message_DrawTextboxIcon`, `Message_DrawText` and the box background build, as gbi C, and it
is encoded, run on the RSP and RDP of `n64/` and drawn by `gfx/`, with the setup list
`sSetupDL[39]` and the art read from the checkout's `message_static`, `nes_font_static`,
`icon_item_static` and `icon_item_24_static`. The ocarina's staff and the pictures of
`BACKGROUND` are not drawn.

Japanese is drawn from `kanji`, 14 pixels a character. The font is some four thousand
textures, so it is read only once Japanese is first picked; until it is, a Japanese box is
drawn without its text.

## Showing them

The Assets panel's Messages tab lists the messages of the language picked, filtered by ID or
text and by chips for vanilla, own and box type, with the first box of the one selected drawn
under the table. A single click shows a message in the Message panel, every box of it drawn
in turn with a meter for each line: how far it reaches towards the box's right margin, as
far from its right edge as text starts from its left. The game stops nowhere; past the
margin, text runs over the frame.

The catalog (`MessageCatalog`: the messages, the format, the layout tables and the art) is
read off the UI thread and again when one of the files it was read from changes.

## Editing

Each box is edited as markup (`MessageMarkup`): text as it reads, a new line as a new line,
and each control code in braces, named in words. A color is its name (`{Red}`), an item icon
its item (`{Icon Pocket Egg}`), the player's name `{Name}`, any other code its name and
arguments (`{Shift 4}`, `{Box Break Delayed 20}`), a brace doubled. `{Box Break}` typed into
a box splits it once the field is left. Every message of a vanilla checkout reads back from
its markup as it was.

Edits live in a draft (`MessageDraft`). A table read again replaces the draft only while
nothing is edited; otherwise the edits are kept and the panel says the checkout changed.

## Saving

A save writes `assets/text/message_data.h` whole from the draft, and Toki owns it from then
on, as it owns a scene it saved ([scene-writing.md](scene-writing.md)):

- every row is written into it, the vanilla ones it included from `extracted/` too, so an
  edit of a vanilla message replaces it where the game searches for it;
- it is marked `// toki: messages`, includes `config.h`, and writes each row as the
  extracted files do, a line of C for each line of the box;
- rows under one of HackerOoT's feature switches are written under its `#if` again; a file
  with rows under a switch that is off is not saved, since those rows were never read;
- other `#if`s, `#define`s and includes are not written again, and the plan says so;
- what is written is read back, under the checkout's configuration, before the plan is
  shown, and every row must read as it was written.

The plan is shown before it is written, as every edit of the checkout is
([tools.md](tools.md)).
