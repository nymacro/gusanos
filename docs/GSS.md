# GSS — GUI Style Sheets

GSS (GUI Style Sheets) is a CSS-inspired declarative styling language for the
Gusanos GUI framework. It controls the appearance and layout of windows, buttons,
labels, text edit boxes, checkboxes, lists, and groups.

## Implementation

| File | Purpose |
|---|---|
| `GUI/detail/gss-grammar.pg` | PEG grammar written in the custom parser generator format (old-style `property`/`clause`/`document` rules). The `.re` file and generated `.h` supersede this. |
| `GUI/detail/gss-grammar.h.re` | re2c-based parser template (hand-written replacement for the PEG approach). Defines tokenization and the `TGrammar<T>` template class providing `rule_clause()`, `rule_property()`, `rule_document()`, `Location` support, error reporting, and sync/recovery. |
| `GUI/detail/gss-grammar.h` | Generated output from re2c 4.5.1. Do not edit — regenerate from `.h.re`. |
| `GUI/detail/gss.cpp` | `GSSImpl` — concrete instantiation of `TGrammar<GSSImpl>` that wires the parser to a `std::istream` input and builds a `Context::GSSselectors` list. Entry point: `Context::loadGSS()`. |
| `GUI/detail/context.h` | `Context::GSSselector` (list of `Condition` structs + a `GSSpropertyMap`), `Context::GSSselectors` (list of selectors), and `matchesWindow()`. |
| `GUI/detail/context.cpp` | `matchesWindow()` implementation — iterates conditions against the window's tag/class/id/state/group fields. |
| `GUI/detail/wnd.h` | `Wnd::Formatting` struct — stores the resolved visual properties (colors, dimensions, font, background, skin, borders, alpha/blender). Also `Wnd::m_state`, `m_tagLabel`, `m_className`, `m_id`, `m_group`. |
| `GUI/detail/wnd.cpp` | `Wnd::applyGSS()`, `Wnd::applyGSSreally()`, `Wnd::applyFormatting()` — the cascade application logic. |

## Syntax

A GSS document is a sequence of **clauses**. Each clause is:

```
selector { property: value1 value2 ... ; property: value ; ... }
```

### Selectors

A selector is a sequence of **conditions** that a window must match. Multiple
conditions within one selector are ANDed (all must match).

| Syntax | Condition Type | What it matches |
|---|---|---|
| `tagname` | Tag | `Wnd::m_tagLabel` — set by the widget class at construction (e.g. `"button"`, `"edit"`, `"list"`, `"window"`, `"label"`, `"group"`, `"check"`) |
| `.classname` | Class | `Wnd::m_className` — set via the `class` XML attribute |
| `#id` | ID | `Wnd::m_id` — set via the `id` XML attribute |
| `:state` | State | `Wnd::m_state` — one of `"active"`, `"focused"`, or `""` |
| `>groupname` | Group | `Wnd::m_group` — set via the `group` XML attribute |

**Selector examples:**

```
button { ... }            /* matches all buttons */
.button { ... }            /* matches all windows with class="button" (confusing but valid) */
#mainMenu { ... }          /* matches window with id="mainMenu" */
:focused { ... }           /* matches the window that currently has keyboard focus */
:active { ... }            /* matches the active window (e.g. pressed button) */
>topLevel { ... }          /* matches windows in group "topLevel" */
button#ok:active { ... }   /* matches tag=button AND id=ok AND state=active */
```

### Specificity

Selectors are ranked by specificity and applied lowest-first so that more
specific rules override less specific ones. The `matchesWindow()` function in
`GUI/detail/context.cpp:219` computes a numeric score:

| Condition | Points |
|---|---|
| Tag match | +1 |
| Class match | +2 |
| ID match | +4 |
| State match | +8 |
| Group match | +16 |

If a selector has no conditions at all, it matches every window with score 1.
If any condition fails, the selector produces score 0 and is skipped.

## Properties

Properties are applied inside `Wnd::applyFormatting()` (`GUI/detail/wnd.cpp:237`).

| Property | Values | Effect |
|---|---|---|
| `background` | `#RRGGBB` | Fill color for the widget rectangle |
| `color` / `colour` | `#RRGGBB` | Text color |
| `background-image` | sprite set name | Tile a sprite set as the background |
| `background-invisible` | `0` or `1` | Make background transparent |
| `skin` | sprite set name | 9-slice skin (requires 8 equal-sized frames) |
| `border` | `#RRGGBB` | Uniform border color on all 4 sides |
| `border-left` | `#RRGGBB` | Left border color |
| `border-top` | `#RRGGBB` | Top border color |
| `border-right` | `#RRGGBB` | Right border color |
| `border-bottom` | `#RRGGBB` | Bottom border color |
| `left` | integer | Distance from parent's left edge (positive = from left, negative = from right) |
| `right` | integer | Distance from parent's right edge |
| `top` | integer | Distance from parent's top edge (positive = from top, negative = from bottom) |
| `bottom` | integer | Distance from parent's bottom edge |
| `width` | integer | Widget width in pixels |
| `height` | integer | Widget height in pixels |
| `spacing` | integer | Gap between child widgets during auto-layout |
| `padding` | integer | Inset from the widget rect to the free rect for children |
| `font-family` | font name | Font for text rendering |
| `alpha` | 0–255 | Transparency (0 = invisible, 255 = opaque) |
| `blender` | `alpha`, `add`, `none` | Alpha blending mode |

### Color format

All color values use 6-digit hex: `#RRGGBB`. Example: `#FF0000` = red,
`#00FF00` = green, `#000080` = navy.

### Value lists

Properties can take multiple values. The parser collects all tokens after the
property name up to the next semicolon. For most properties the last or first
valid value wins:

```
background: #000080 #00AF00;   /* all values are tried; last #00AF00 applied */
```

For `background-image`, `font-family`, and `skin`, each value is tried as a
resource name until a successful load.

## Lifecycle

1. **Loading:** A GSS source file is loaded via `Context::loadGSS(istream&,
   filename)` (`gss.cpp:52`). This creates a `GSSImpl` and calls
   `rule_document()`, which populates `m_gss` (a `GSSselectors` list).

2. **Application:** When `Context::updateGSS()` is called, the root window's
   `doUpdateGSS()` traverses the widget tree and calls `applyGSS()` on every
   window.

3. **Matching:** `Wnd::applyGSS()` (`wnd.cpp:221`) sets the window's `m_state`
   (based on active/focused status), then calls `applyGSSreally()`. That
   function (`wnd.cpp:195`) iterates all selectors, calls `matchesWindow()` on
   each, collects those that match, sorts by specificity (lowest first), and
   calls `applyFormatting()` for each.

4. **Formatting:** `Wnd::applyFormatting()` (`wnd.cpp:237`) walks the property
   map and sets fields on the `m_formatting` struct (colors, dimensions, font,
   background, skin, borders, alpha, blender). Each window type can override
   `applyGSS()` to handle additional widget-specific properties (e.g. `Check`
   inherits it to manage checked/unchecked state).

5. **Placement:** After formatting, `updatePlacement()` (`wnd.cpp:417`)
   resolves coordinate values (positive = from top/left, negative = from
   bottom/right) against the parent rect, applies padding, and populates the
   free-space allocator for child widgets.

## Example

```gss
/* Root window — fills entire viewport */
window {
    background: #000080;
    left: 0;
    top: 0;
    right: -1;
    bottom: -1;
    padding: 29;
    spacing: 20
}

/* Buttons with id="ok" and state="focused" */
button {
    background: #00AF00;
}

button#ok:focused {
    border: #FFFF00;
}

/* All edit fields */
edit {
    background: #FFFFFF;
    color: #000000;
    font-family: "default"
}

/* All checkboxes */
check:focused {
    border: #FFFF00;
}

check:active {
    background: #0000FF;
}
```

## Related

GSS is loaded in conjunction with XML layout files (see `GUI/detail/xml.cpp`).
The XML document defines the widget tree hierarchy and binds window IDs/classes;
GSS provides the visual properties. Both are parsed at runtime from data files
(typically `.xml` and `.gss` extensions loaded by the application's `Context`
subclass).
