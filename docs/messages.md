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
English, German and French on PAL. A click previews a message in a tab of the central dock
node, the tab the next one previewed takes, until it is edited or its tab double-clicked
([asset-tabs.md](asset-tabs.md)). A tab draws every box of its message in turn with a meter
for each line, which the header's ruler switch hides, as its pin switch hides the
markers of codes. The meter runs to the box's right
edge, with a tick at its margin, as far from that edge as text starts from its left: green up
to the margin, yellow up to the edge, red past it. The game stops nowhere; past the edge,
text runs over the frame. The colors are read from a vanilla checkout: of its 8,931 English
lines, 316 reach past the margin, 99% end 17 pixels short of the edge, and the longest 4
pixels past it, so the margin alone would mark lines the game ships.

Above each box stands what closes it, named by the mark the game draws at its end: No
Prompt, a Continue Prompt (the triangle, a press for the next box), a Close Prompt (the
square, a press to close) or a Choice Prompt (the arrow).

The boxes are drawn as large as the panel holds, in steps of half a game pixel that keep the
game's pixels whole. Each is drawn moved to the top of the screen and cut out around itself
(`makeMovedBox`): nothing it draws depends on where it stands, and its gbi commands are then
the same at every position, so a box moved is drawn from commands already expanded rather
than a few hundred new ones. The zoom menu in the tab's header, or Ctrl+Scroll over the boxes, sets
the step instead; it is shared by every tab.

The header is the message's form: its ID, whose tooltip says which languages' tables hold it
and where it is written, and its box type and position, set there, with a small screen
beside them showing where the box stands on it. The box is drawn cut out around itself, so
its position shows only there. Nothing of a message is in the Properties panel. Render and
Markup, side by side left of the language, switch between its boxes and the whole message
as markup (Alt+1 and Alt+2).

What is being read or written covers what it changes with a translucent grey that takes the
mouse, and a spinner over it (`core::drawBusyCover`): the whole tab and the Messages table
while the messages are first read, and the tab under its header and the table while they are
read again, while the kanji font is read once Japanese is picked, and while they are saved.
The header stays usable, so another language can be picked meanwhile.

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

The box typed in is framed. A code that draws nothing, a color or a pause, is marked over the
image where it stands (`BoxMarkers`): a color by a swatch of the color it sets, any other code
grey, with an icon saying what kind it is, so no marker reads as text in a color. Hovering
one says in a few words what it does, as `describeCode` words what `z_message.c` does with it
("Red Text", "24 Pixels of Space"); a click selects it, so Delete removes it, and opens
the form of its arguments for a code that has any. Codes that stand at one place, a color
and a pause after one another, are one marker (`groupMarkers`): the last, whose color the
text after it is drawn in, with how many there are. Hovering it lists them all, each with its
head and what it does, and a click on one does what a click on its own marker would.

The rail at the tab's left starts with adding a box after the one typed in, or the last, and
deleting the one typed in. Under that it puts in, where the caret is, the charmap's signs
(the buttons and what lies past the Latin letters, as the charmap lists them), each shown as
the glyph the checkout's font draws it with (`decodeFontGlyph`, and for Japanese
`decodeKanjiGlyph`, since its signs are glyphs of the kanji font); a color (as `TextColor`
lists them), each a swatch of the color `Message_SetTextColor` gives it in the box typed in;
and every other control code but the box breaks (`listInsertableCodes`), each a button with
its name. A code with arguments goes in with a default and opens its form: a number as a
field, a message ID with the start of that message's text beside it, and an item, a high
score or a sound as a filtered dropdown of what the checkout's `ItemID`, `HighScores` and
`SfxId` declare. The rail stands still, greyed out while no box is typed in, so nothing moves
when a box is clicked.

The keys, which the settings can change and the tooltips of the colors and of deleting a
box name: Ctrl+1 to Ctrl+8 the colors in `TextColor`'s order,
Ctrl+Enter splits the box at the caret, Ctrl+Shift+Delete deletes the box typed in, and
Alt+3 and Alt+4 switch the meters and the markers of codes. They
are heard while a box is typed in, as the viewport's are not.

A color picked over selected text draws that text in it (`EditableBox::colorSelection`): the
color goes before it, and after it the color the text after it was drawn in, the last color
before the selection's end or else the box's default, since every box starts in it. Colors
inside the selection and right beside it go, so text colored twice is wrapped once, and a
code that sets the color already drawn in is left out.

The charmap names signs the font has no glyph for: `[D-Pad]` is `0xAB`, and vanilla
`nes_font_static` ends at `0xAA`, the control stick. The game loads such a glyph from whatever
follows the font. The rail leaves them out (`hasFontGlyph`), a message that uses one says so,
and its box draws a red `?` in its place. A glyph added to the font is drawn as it is, since
the font is read from the checkout.

The markup view (`MessageMarkup`) is the whole message as one text: text as it reads, a new
line as a new line, and each control code in braces, named in words. A color is its name
(`{Red}`), an item icon its item (`{Icon Pocket Egg}`), the player's name `{Name}`, any other
code its name and arguments (`{Shift 4}`, `{Box Break Delayed 20}`), a brace doubled. A box
break stands on a line of its own with an empty line after it, as the checkout writes its
`BOX_BREAK`; the line breaks around it are layout and read as nothing, so a new line of the
text beside a box break is written `{New Line}`. Markup that does not read says where, and
the message keeps what it last read as. Every message of a vanilla checkout reads back from
its markup as it was.

The field draws its codes in colors of their own, a color code in the color it sets
(`findMarkupSpans` cuts the text as `parseMarkup` reads it). Dear ImGui's field cannot color
its text, so it draws the caret and the selection with the text made clear, and the runs are
drawn over it at the places the field lays them out at: its child window's position and
scroll, and the width of each run in the monospace font.

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
