# Asset tabs

An asset opened for editing is a tab, as an IDE keeps the files it has open: every open
message is a window of its own, docked as a tab among the panels, and is split off, dragged
next to another or out of the main window as any panel is. There is no panel the tabs live in.
Each tab is saved, thrown back and closed on its own. What is here is generic and lives in
`app/core/`; an asset kind supplies only its tab. Scenes are the next kind to become one, the
viewport becoming the tab of the open scene.

## The model

`AssetTabs` (`core/domain/`, under tests) is the list of open assets by key: the order they
were opened in, the active one, and at most one previewed. Where a tab is drawn, and which tab
is next to which, is the docking's; the list knows none of it.

- Opening an asset already open only makes it active, and pins it when it is opened pinned.
- A previewed asset takes the place of the preview tab, so looking through assets one after
  another leaves one tab. A pinned one, or a previewed one when there is no preview, is put
  last, and named as opened beside the active tab.
- A preview is pinned by opening it pinned, by a double click on the tab, or by its first
  edit, so a tab with edits is never replaced.
- The active tab is the one focused last. Closing it makes the one active before it active
  again.

A key names the asset as its kind finds it again, not as an index: a message is
`message:<ID>:<macro>:<occurrence>` (`MessageKey`), the same message once the table is read
again with rows added before it.

## The tab

`AssetTab` (`core/ui/`) is what a kind implements: its title (`Message 0x1234`), whether it has
edits no save wrote, saving them, throwing them away, drawing itself, and the form the
Properties panel shows of it. A save may take frames, as a reviewed one waits for Write Files;
it calls back with whether it wrote the asset.

`AssetTabHost` draws each tab as a window, `<title>###asset:<key>`, so the key and not the
title is its ID. A tab with edits is marked by ImGui's own unsaved-document dot; the preview's
title starts with an eye, since a docked tab cannot be drawn faint or in italics. A double
click on the docked tab, found in the dock node's tab bar, pins the preview. The Properties
panel shows the active tab's asset.

## Docking

A tab is docked once, in the frame its window first appears; after that it stays wherever it
is put.

- A preview taking another's place takes that one's dock node.
- A new tab otherwise joins the node of the tab active when it was opened, so tabs open where
  one is working.
- With no such tab, or one floating on its own, it goes into the central node, where the
  viewport is by default.

The windows are not written to `imgui.ini`: tabs are not opened again at start, and a tab
opened again is placed by the rules above, not where it was last.

## Saving

Ctrl+S, or File > Save, saves the active tab when a tab had the focus last, rather than a
panel, or no scene is open; otherwise the open scene, as before. File > Save names what it
saves. Save All (Ctrl+Shift+S) saves every tab with edits, one after another, and stops at the
first that is not written, then the scene.

## Closing

Nothing with edits is closed without asking. One question serves every kind of asset,
naming each by its tab's title (`UnsavedEditsDialog`, worded by `makeUnsavedEditsQuestion`):

> You have unsaved edits in "Message 0x1234". Do you want to save them before closing?

It is asked when a tab is closed (its button, a middle click, Ctrl+W), when Toki is quit
(the window's close button, Alt+F4, File > Quit), and before another checkout is opened,
then naming every asset with edits, the open scene's among them. Save writes them and goes on
only once every one was written; Don't Save goes on and throws them away; Cancel stays.

The window does not close on its own: `Window::takeCloseRequest` hands the request to the app,
which asks first and closes with `requestClose`.

A checkout read again under the same root keeps its tabs and their edits; the draft carries
them over ([messages.md](messages.md#editing)). One read from another root closes every tab.
