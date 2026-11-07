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
text and by chips for box type, with the first box of the one selected drawn under the table.
The languages offered are those the checkout has text in: English and Japanese on NTSC,
English, German and French on PAL. A single click previews a message in a tab of the
central dock node, the tab the next one previewed takes; a double click opens it in a tab of its own
([asset-tabs.md](asset-tabs.md)). A tab draws every box of its message in turn with a meter
for each line: how far it reaches towards the box's right margin, as far from its right edge
as text starts from its left. The game stops nowhere; past the margin, text runs over the
frame.

The boxes are drawn as large as the panel holds, in steps of half a game pixel that keep the
game's pixels whole, with each box's markup beside it when there is room and under it when
there is not. The zoom menu in the tab's header, or Ctrl+Scroll over the boxes, sets the step
instead; it is shared by every tab.

The catalog (`MessageCatalog`: the messages, the format, the layout tables and the art) is
read off the UI thread and again when one of the files it was read from changes.

## Editing

A box is typed in where it is drawn. A click puts a caret where the game's pen stood, and
from there it is a text field: arrows, Home and End, Shift to select, a drag, Backspace,
Delete, Enter for a new line, and Ctrl+A, C, X and V. The window is told it takes text, so an
input method for Japanese composes at the caret. Escape, or a click beside the boxes, ends
typing.

The caret is placed from three records that line up byte for byte:

- the encoder records where in the text each byte was encoded from (`TextPosition`: a piece,
  and in a text piece the byte its character starts at), the closing `END` past the last
  piece;
- the layout records a stop (`LaidStop`) for each code and character it reaches: the byte, and
  where the pen stood before it, so a code that draws nothing has one too, and a sample such
  as `{Name}` one where it starts;
- the box is a list of units (`EditableBox`): a character as the charmap reads it, so `[A]`
  is one, a new line, or a code. `localizeSources` turns positions in the message into
  positions in its box.

A unit's caret place is its stop; one the game drew nothing for stands where the next does
(`findCaretPlaces`). Every unit of every English box of a vanilla checkout has a stop.

A key changes the units, and the box makes its pieces again and reads them back, so a sign
typed key by key, `[`, `A`, `]`, becomes one unit when complete. The pieces go into the draft
as a markup edit does, and the box is laid out and drawn again from there.

A code that draws nothing, a color or a pause, is marked over the image where it stands, a
color in its own color; hovering one names it in markup, and a click selects it, so Delete
removes it. The rail at the tab's left puts in, where the caret is, the charmap's signs (the
buttons and what lies past the Latin letters, as the charmap lists them), each shown as the
glyph the checkout's font draws it with (`decodeFontGlyph`); a color (as `TextColor` lists
them), each a swatch of the color `Message_SetTextColor` gives it in the box typed in; the
player's name; or a code without arguments. A code with arguments is written as markup. The
rail stands still, greyed out while no box is typed in, so nothing moves when a box is
clicked. It also adds a box after the last and removes the one typed in.

Beside or under each box its markup can be edited too (`MessageMarkup`): text as it reads, a new line as a new line,
and each control code in braces, named in words. A color is its name (`{Red}`), an item icon
its item (`{Icon Pocket Egg}`), the player's name `{Name}`, any other code its name and
arguments (`{Shift 4}`, `{Box Break Delayed 20}`), a brace doubled. `{Box Break}` typed into
a box splits it once the field is left. Every message of a vanilla checkout reads back from
its markup as it was.

Edits live in a draft (`MessageDraft`), kept apart message by message, so that one is saved or
thrown away without the others. A table read again keeps every edit, moved onto the same
message found again in it by its `MessageKey`; the edits of a message the table has no more
are dropped and the log names it. A message whose row the checkout changed under its edits
says so. A message edited back to what was read has no edits.

Typing redraws only what changed. Each gbi command of a box is expanded through the gbi's
macros once and its words kept (`GbiCommandCache`); a box is drawn again only when what it
shows changes; and the Messages tab rebuilds only the row of the message edited.

## Saving

A save is of one tab's message, or of every message with edits for Save All
([asset-tabs.md](asset-tabs.md)). It writes `assets/text/message_data.h` whole: the messages
saved as edited, every other one as it was last read, its edits kept in the draft for a save
of its own (`MessageDraft::makeSavedTable`). Toki owns the file from then on, as it owns a
scene it saved ([scene-writing.md](scene-writing.md)):

- every row is written into it, the vanilla ones it included from `extracted/` too, so an
  edit of a vanilla message replaces it where the game searches for it;
- it is marked `// toki: messages`, includes `config.h`, and writes each row as the
  extracted files do, a line of C for each line of the box;
- rows under one of HackerOoT's feature switches are written under its `#if` again; a block
  under a switch that is off was never read, so it is written again as it stands, `#if` to
  `#endif`, before the row it stood before. HackerOoT's own file has one: the two scene
  names `ENABLE_MM_TITLE_CARDS` shows as title cards, off by default. A switch that is off
  with an `#else` or `#elif` cannot be kept as it stands, and the save is refused;
- other `#if`s, `#define`s and includes are not written again, and the plan says so;
- what is written is read back, under the checkout's configuration, before the plan is
  shown, and every row must read as it was written.

The plan is shown before it is written, as every edit of the checkout is
([tools.md](tools.md)), unless Settings > Messages > Review Saves is off. A plan that warns is
shown either way, and the first save of a checkout's messages warns of the include it writes
the vanilla rows in place of. What a message was edited to while its save was planned still
counts as edits once it is written.
