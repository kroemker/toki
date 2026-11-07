# Asset tabs

An asset opened for editing is a tab, as an IDE keeps the files it has open: the Message panel
holds one tab per message. Each tab is saved, thrown back and closed on its own. What is
here is generic and lives in `app/core/`; an asset kind supplies only its tab. Scenes are the
next kind to become one.

## The model

`AssetTabs` (`core/domain/`, under tests) is the list of open assets by key: their order,
the active one, and at most one previewed.

- Opening an asset already open only makes it active, and pins it when it is opened pinned.
- A previewed asset takes the place of the preview tab, so looking through assets one after
  another leaves one tab. A pinned one, or a previewed one when there is no preview, is put
  after the active tab.
- A preview is pinned by opening it pinned, by a double click on the tab, or by its first
  edit, so a tab with edits is never replaced.
- Closing the active tab makes the one after it active, or the one before it when it was the
  last.

A key names the asset as its kind finds it again, not as an index: a message is
`message:<ID>:<macro>:<occurrence>` (`MessageKey`), the same message once the table is read
again with rows added before it.

## The tab

`AssetTab` (`core/ui/`) is what a kind implements: its title (`Message 0x1234`), whether it has
edits no save wrote, saving them, throwing them away, drawing itself, and the form the
Properties panel shows of it. A save may take frames, as a reviewed one waits for Write Files;
it calls back with whether it wrote the asset.

`AssetTabPanel` draws the tabs. A tab with edits has an amber `*` after its title, the preview
is drawn faint. The Properties panel shows the active tab's asset.

## Saving

Ctrl+S, or File > Save, saves the active tab when the Message panel had the focus last, or no
scene is open; otherwise the open scene, as before. File > Save names what it saves. Save All
(Ctrl+Shift+S) saves every tab with edits, one after another, and stops at the first that is
not written, then the scene.

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
