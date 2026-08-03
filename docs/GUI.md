# GUI System

The Gusanos GUI framework (`OmfgGUI` namespace) is a widget-based UI system for
in-game menus, HUDs, and editor interfaces. It is modelled loosely after CSS +
HTML: **XML** files define the widget hierarchy and **GSS** files style them.

## Directory Structure

```
GUI/
├── SConscript              # Build: Parser generation + static library
├── omfggui.h               # Umbrella header (Context + Renderer)
├── omfggui_windows.h        # Umbrella header (all widget types)
├── detail/
│   ├── context.h/cpp        # Context — root object, event dispatch, input routing
│   ├── renderer.h/cpp       # Renderer interface + RGB, BaseFont, BaseSpriteSet
│   ├── wnd.h/cpp            # Wnd — base widget class
│   ├── button.h/cpp         # Button widget
│   ├── check.h/cpp          # Checkbox widget
│   ├── edit.h/cpp           # Text edit widget
│   ├── group.h/cpp          # Container widget (collapsible group)
│   ├── label.h/cpp          # Static text label
│   ├── list.h/cpp           # List/tree widget with multi-column
│   ├── llist.h              # Intrusive doubly-linked list (LList, LNodeImp)
│   ├── gss.cpp              # GSS parser implementation (GSSImpl)
│   ├── gss-grammar.h        # Generated parser (re2c) — do not edit
│   ├── gss-grammar.h.re     # re2c parser template
│   ├── gss-grammar.pg       # PEG grammar (legacy)
│   ├── xml.cpp              # XML layout parser
│   ├── xml-grammar.h        # XML pull-parser templates
│   └── xml.h                # (stub)
└── lua/
    ├── bindings-gui.h/cpp   # Lua bindings for GUI subsystem
    └── ...
```

## Architecture

### Context (`context.h/cpp`)

`Context` is the root controller. One instance typically exists per UI screen.

**Owned state:**
- `m_rootWnd` — root widget of the tree
- `m_gss` — `GSSselectors` list (loaded GSS rules)
- `m_namedWindows` — `std::map<std::string, Wnd*>` for ID-based lookup
- `m_keyboardFocusWnd` — currently focused widget
- `m_mouseCaptureWnd` — widget capturing mouse input
- `m_mouseFocusWnd` — widget with mouse hover focus
- `m_activeWnd` — currently active (pressed) widget
- `m_cursorX/Y` — last known cursor position
- `m_renderer` — abstract `Renderer*`

**Event routing** — all input flows through Context:
| Method | Action |
|---|---|
| `mouseMove(x, y)` | Route to capture Wnd or root tree traversal |
| `mouseDown/Up(x, y, btn)` | Route to capture Wnd or root tree traversal |
| `charPressed(c)` | (stub) |
| `keyDown/Up(k, shift, alt, ctrl)` | (stub) |
| `render()` | Render root → children with clip rect |
| `process()` | Call `doProcess()` on root → children |
| `setFocus(wnd)` | Keyboard focus with last-child-focus chain |
| `setActive(wnd)` | Activation state tracking |

**GSS integration:**
- `loadGSS(istream, filename)` — parses a GSS source into `m_gss`
- `loadGSSFile(name, passive)` — abstract, defined by subclass
- `updateGSS()` — triggers full re-styling of widget tree
- `registerNamedWindow(id, wnd)` / `findNamedWindow(id)` — named window registry

**Abstract methods** (must be implemented by subclass):
- `keyState(int key)` — keyboard state query
- `loadGSSFile(string, bool)` — load GSS from file
- `loadXMLFile(string, Wnd*)` — load XML from file
- `loadFont(string)` — font resource loading
- `loadSpriteSet(string)` — sprite resource loading

### Renderer Interface (`renderer.h`)

`Renderer` is an abstract base class that the game engine implements
(typically in the game code rendering on top of an SDL3 surface). Methods:

| Method | Purpose |
|---|---|
| `drawBox(rect, color, ...borders)` | Filled rectangle with optional per-side border colors |
| `drawFrame(rect, color)` | Rectangle outline |
| `drawVLine(x, y1, y2, color)` | Vertical line |
| `drawText(font, str, flags, x, y, color)` | Text rendering with centering flags |
| `getTextDimensions(font, b, e)` | Measure text extent |
| `getTextCoordToIndex(font, b, e, x)` | Hit-test text position |
| `drawSprite(spriteSet, frame, x, y)` | Sprite blitting (full or cropped) |
| `drawSkinnedBox(skin, rect, bgColor)` | 9-slice skin rendering |
| `setClip(rect)` / `getClip()` / `getViewportRect()` | Clipping |
| `setAddBlender(alpha)` / `setAlphaBlender(alpha)` / `resetBlending()` | Blending modes |

Supporting types:
- `RGB(uchar r, g, b)` — color with `blend()`, `lighten()`, `darken()` helpers
- `BaseFont` — abstract font with `CenterH` / `CenterV` flags
- `BaseSpriteSet` — abstract sprite sheet with frame count and frame dimensions

### Wnd — Base Widget (`wnd.h/cpp`)

Every widget inherits from `Wnd`. Key aspects:

**Construction:** Every widget is allocated from the Lua state via placement new
into Lua userdata. Constructor takes `(Wnd* parent, map<string,string>& attribs,
string tagLabel)`. Attributes `label` (text), `class`, `id`, `group`,
`selectable` are auto-extracted.

**Widget tree:**
- `m_parent`, `m_children` (linked list), `m_namedChildren` (map)
- `addChild()`, `removeChild()` — automatically propagates `m_context` and
  `m_group` from parent to child
- `m_lastChildFocus` — remembers the last focused child for keyboard navigation

**Formatting** (`struct Formatting`):
| Field | Default | Meaning |
|---|---|---|
| `background.color` | `(128,128,128)` | Fill color |
| `background.spriteSet` | 0 | Background tile sprite |
| `background.skin` | 0 | 9-slice skin sprite |
| `background.invisible` | false | Skip background fill |
| `borders[4]` | `(255,255,255)` | Left/Top/Right/Bottom border colors |
| `width` / `height` | 50 | Widget size |
| `spacing` | 5 | Gap between children in auto-layout |
| `padding` | 5 | Inset from widget rect to free area |
| `rect` | `(10,10,0,0)` | Position coordinates (relative to parent) |
| `flags` | 0 | `HasLeft`/`HasRight`/`HasTop`/`HasBottom` |
| `fontColor` | `(255,255,255)` | Text color |
| `alpha` | 255 | Opacity (0–255) |
| `blender` | `Alpha` | `Alpha` / `Add` / `None` |

**Coordinate system:** Positive values offset from parent's top-left corner;
negative values offset from parent's bottom-right corner. A value of -1 means
"flush to the opposite edge".

**Placement:** `updatePlacement()` resolves format values into `m_rect`
(absolute pixel rect), applies `padding` to produce `m_freeRect`, and sets up
the free-space allocator for child widgets.

**Event virtuals** (return `false` = event consumed):
- `render()` — draw the widget
- `process()` — frame update
- `mouseMove/Down/Up(x, y, button)` — mouse events
- `mouseScroll(x, y, offs)` — scroll wheel
- `keyDown/Up(key)` — keyboard events
- `charPressed(c, key)` — text input

**Event dispatchers** (`doXxx` methods) traverse the widget tree:
- `doRender(clip)` — renders only if visible and overlapping clip rect,
  processes children bottom-up
- `doMouseMove/Down/Up` — hit-tests from topmost child down
- `doProcess()` — processes children after self

**Lua integration:**
- Static `metaTable` per widget class
- `luaReference` — Lua reference to this widget
- `m_callbacks[LuaCallbacksMax]` — Lua callbacks for `OnAction`, `OnKeyDown`,
  `OnActivate`
- `pushReference()` — push to Lua stack
- `registerCallback(name, ref)` — wire a Lua function to a callback name

## Widget Types

### Button (`button.h/cpp`)

Tag label: `"button"`

Renders a filled box with optional skin, optional background sprite, and
centered text. Handles mouse down/up for activation state and Enter key to
fire `doAction()`.

### Checkbox (`check.h/cpp`)

Tag label: `"check"`

Renders a square box (height-sized) on the left with text to its right.
Toggles `m_checked` state on click or Enter. Overrides `applyGSS()` to use
custom states: `"checked"`, `"active-checked"`, `"focused-checked"` — enabling
different styling for checked vs unchecked.

### Edit (`edit.h/cpp`)

Tag label: `"edit"`

Single-line text editor with:
- Caret navigation (Left/Right/Home/End)
- Shift-selection (`m_selTo`)
- Backspace/Delete with selection-aware deletion
- Text input via `charPressed()` with lock support (`m_lock`)
- Horizontal scrolling (`m_hscroll`) to keep caret visible
- Activate/deactivate on Enter

### Label (`label.h/cpp`)

Tag label: `"label"`

Static text display. Blocks all input events (returns `false`). No background
rendering by default. Sprite and text overlay only.

### Group (`group.h/cpp`)

Tag label: `"group"`

Container widget. Renders background/skin/sprite but suppresses text rendering
(commented out). Reports `classID()` as `Context::Group`.

### List (`list.h/cpp`)

Tag label: `"list"

Multi-column, tree-capable list widget backed by an intrusive linked list
(`LList<ListNode>`).

**ListNode** — tree node with:
- `columns` — vector of text per column
- `selected`, `expanded` — state flags
- `children`, `parent`, `level`, `visibleChildren` — tree structure
- `render(renderer, y)` — draws selection highlight and column text
- `push_back(node)` — inserts a child node

**List** — the widget itself:
- Column headers with width factors (`ColumnHeader` struct)
- `m_RootNode` — sentinel root of the tree
- `m_Base` — scroll position base index
- `m_MainSel` — current primary selection
- `m_visibleChildren` — count of visible (non-collapsed) nodes
- Lua sort comparator via `LuaLT`
- `m_listFormatting` — `selectionColor`, `selectionFrameColor`, `indent`

### LList (`llist.h`)

Intrusive doubly-linked list template used by `List` / `ListNode`. Supports
insert, erase, unlink, merge sort, and iterator pattern.

## XML Layout (`xml.cpp`)

XML documents define the widget hierarchy. A simple pull-parser in
`xml-grammar.h` provides tokenization via `xmlDocument(s, handler)`.

**Supported tags** and their C++ types:

| XML Tag | Class |
|---|---|
| `<window>` | `Wnd` |
| `<button>` | `Button` |
| `<check>` | `Check` |
| `<edit>` | `Edit` |
| `<group>` | `Group` |
| `<label>` | `Label` |
| `<list>` | `List` |

**Attributes** on any tag become entries in the `attributes` map passed to the
widget constructor. Standard attributes: `label` (text), `class`, `id`, `group`,
`selectable`.

```xml
<window id="mainMenu">
    <button label="Start Game" id="btnStart"/>
    <button label="Options" id="btnOptions"/>
    <button label="Quit" id="btnQuit"/>
</window>
```

The parser uses `buildFromXML(istream, dest)` in `Context` — if `dest` is null,
the first window becomes the context root.

## GSS Styling

See [`GSS.md`](GSS.md) for complete documentation of the GSS syntax, selectors,
properties, specificity scoring, and lifecycle.

## Lua Bindings (`GUI/lua/bindings-gui.cpp`)

The GUI subsystem is exposed to Lua scripts via `LuaBindings::initGUI()`.

| Lua function | C++ binding | Purpose |
|---|---|---|
| `gui_load_xml(name[, loadTo])` | `l_gui_loadxml` | Load an XML layout; returns the root or loaded-to window |
| `gui_load_gss(name[, passive])` | `l_gui_loadgss` | Load a GSS sheet; optionally skip restyling existing widgets |
| `gui_find(name)` | `l_gui_find` | Look up a window by ID; returns `Wnd` object or nil |
| `gui_root()` | `l_gui_root` | Returns the root `Wnd` object |

Widget creation from Lua uses `lua_new_keep` — a Lua table of attributes
creates the widget, and an optional second table argument provides child
widgets.

See `LUA_API.md` for the full Lua API reference.

## Build

```python
# GUI/SConscript
env.StaticLibrary(env.getLibName('omfggui'), env.getObjects(['detail', 'lua']))
```

- If `NO_PARSERS` is not set, the GSS PEG grammar (`gss-grammar.pg`) is
  processed to generate the parser
- All `.cpp` files in `detail/` and `lua/` are auto-discovered

## DEDSERV Guards

All GUI code is wrapped in `#ifndef DEDSERV` in the engine's integration layer
(the `bindings-gui.cpp` file includes guards). The core GUI library itself has
no guards — it builds independently of rendering.
