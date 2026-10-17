# Bark

Bark is Grove's style language. It reads like CSS, with variables and nesting built in.

```
@import "colors.bark";

$accent = #89b4fa;
$gap = 16px;

body {
    size: 900px 640px;
    background: #1e1e2e;
    padding: 32px;
}

panel.card {
    background: rgba(49, 50, 68, 90%);
    margin: 0 0 $gap 0;

    h3 {
        color: $accent;
    }

    > math {
        text-align: center;
    }

    &.wide {
        padding: 24px;
    }
}
```

## Files

Stylesheets come from `<style src="file.bark" />` or inline `<style>` blocks, in document order. `@import "path";` loads another file relative to the current one; each file is loaded once.

Comments are `// line` and `/* block */`.

## Variables

`$name = value;` defines a variable (`$name: value;` works too). Variables can hold several values (`$pad = 8px 16px;`), are visible to every later rule and later stylesheet, and work inside `style="..."` attributes. An unknown variable is an error.

## Selectors

| Selector | Matches |
|---|---|
| `panel` | tag |
| `#main` | id |
| `.card` | class |
| `panel.card#x` | all of them on one element |
| `*` | any element |
| `body p` | `p` anywhere inside `body` |
| `body > p` | `p` directly inside `body` |
| `a, b` | either |
| `button:hover` | a pseudo-class, see below |

| Pseudo-class | Matches |
|---|---|
| `:hover` | the element under the mouse, and its parents |
| `:active` | an element while the mouse is pressed on it |
| `:focus` | the element that has the keyboard |
| `:checked` | a checked `input_bool` or `input_radio` |
| `:disabled`, `:enabled` | with or without the `disabled` attribute |
| `:first-child`, `:last-child`, `:only-child` | by position among its parent's elements |
| `:empty` | an element with no children |

An unknown pseudo-class is a warning and never matches. Scripts use the same selectors with `query` and `query_all`.

Inside a rule, a nested rule is a descendant of its parent. Start it with `>` for a direct child, or `&` to add to the parent itself (`&.wide` is `panel.card.wide`).

## Values

| Kind | Examples |
|---|---|
| numbers with units | `16px`, `1.5em`, `12pt`, `50%`, `0`, and the viewport units below |
| colors | `#fff`, `#ffffff`, `#ffffff80`, `rgb(1, 2, 3)`, `rgba(1, 2, 3, 50%)`, `hsl(210, 80%, 60%)`, names like `white` or `walnut` |
| strings | `"Hello"`, `'Hello'` |
| identifiers | `bold`, `center`, `none` |
| functions | `name(args)`; anything other than the color functions is kept for the property to interpret |

### Viewport units

These are percentages of the window, so `50vwidth` is half the window's width:

| Unit | 1 unit is 1% of |
|---|---|
| `vwidth` (or `vw`) | the window's width |
| `vheight` (or `vh`) | the window's height |
| `vmin` | the smaller of width and height |
| `vmax` | the larger of width and height |
| `vdiag` | the window's diagonal, √(width² + height²) |

They update as the window is resized. For `body { size: ... }` they're measured against the screen instead, since the window doesn't exist yet: `size: 60vwidth 80vheight` opens a window 60% of the screen wide and 80% tall. Font sizes in viewport units resolve before `em` children inherit them. `grove computed --viewport 1280x720` shows the result for a given window size.

`!important` after a value wins over anything without it. `inherit` copies the parent's value.

## Cascade

For each property the winner is decided by, in order: `!important`, then `style="..."` attributes, then selector specificity (ids, then classes, then tags), then whichever came last. `color`, `font-*`, `line-height`, `text-align`, `letter-spacing`, `white-space`, `math-color`, `accent-color` and `cursor` inherit; nothing else does. `em` and `%` font sizes resolve against the parent.

Grove ships a small default sheet first: heading sizes and weights, `display: none` for `head`/`style`/`script` and for a `<window>` inside `<body>`, a `button` look with `:hover`, `:active`, `:focus` and `:disabled` states (shared by `input_file`), the `input_code` look, `pre { white-space: pre; font-family: monospace; }`, `code` in monospace, `link` in blue, `u` underlined, `row { display: row; gap: 8px; }`, and the popup look (`window.popup`, `.popup-icon`, `.popup-buttons`, `.popup-ok` and friends), which your own rules can override.

## Properties the runtime reads today

| Property | On | |
|---|---|---|
| `size` | `body`, `window` | `width height`; a `<window>` with no height fits its content |
| `title` | `body`, `window` | window title (the `title` attribute wins) |
| `background` | any | fill color |
| `color`, `font-size`, `font-weight` | text | |
| `font-style` | text | `italic` |
| `font-family` | text | a font name like `"JetBrains Mono"`, or `monospace`, `serif`, `sans-serif`; a font that isn't installed falls back to one of the same kind |
| `text-decoration` | text | `underline`, `line-through` |
| `white-space` | blocks | `normal` (default) wraps text and collapses spaces; `nowrap` collapses but doesn't wrap; `pre` keeps spaces and line breaks and doesn't wrap; `pre-wrap` keeps them and wraps |
| `math-color` | `math` | defaults to `color` |
| `margin`, `padding` | blocks, inputs | 1 to 4 lengths, CSS order |
| `border-color`, `border-width` | blocks, inputs | |
| `border-radius` | blocks, inputs | rounded corners |
| `width` | blocks, inputs | a length or a `%` of the space it's in; text inputs and sliders default to 220px; never wider than the space it's in |
| `min-width`, `max-width` | blocks, row items | lengths or `%`; in a row they bound how much of the free space an item takes |
| `height` | blocks, `input_code` | a length or a `%` of the parent's height (when the parent's height is known); with `overflow` other than `visible` the block is exactly this tall, otherwise it's the smallest height |
| `min-height`, `max-height` | blocks | lengths or `%`; `max-height` only applies when `overflow` clips |
| `flex` | children of a column, row items | a number: how much of the leftover space the item gets. In a column (any block that isn't a row) this needs the column's height to be known: from `height`, from being an item in a row, or from being `body` |
| `overflow` | blocks | `visible` (default), `hidden` clips the content to the block, `auto` and `scroll` clip it and let the mouse wheel scroll it |
| `appearance` | `input_bool` | `checkbox` (default) or `switch` |
| `accent-color` | inputs | checked boxes and radios, slider fill and thumb, focused border, the caret and selection of `input_code` |
| `gap` | blocks, `row` | extra space after a block; space between the items of a row |
| `justify-content` | `row` | `start` (default), `center`, `end` or `space-between`, when nothing in the row stretches |
| `line-height` | `input_code` | a multiple of the font size (`1.4`) or a length |
| `opacity` | any | 0 to 1; fades the element and everything inside it |
| `cursor` | any | `pointer`, `text`, `default`, `crosshair`, `move`, `not-allowed`, ... while the mouse is over it |
| `text-align` | text, `math` | `left`, `center`, `right` |
| `display` | any | `none` hides it, `row` lays its children out side by side, `inline-block` makes it only as wide as its content, `inline` puts it in the flow of the text around it |

### Layout

Text wraps at word boundaries to the width it has, and a word too long for a line is broken where it has to be. Inline elements (`text`, `span`, `b`, `i`, `u`, `code`, `link`, `br`, or anything with `display: inline`) flow on the same lines as the text around them, keep their own color, font and background, and get their own clicks and hover.

A row gives every item with a `width` (or that's only as wide as its content: inputs, buttons, inline elements, and rows made only of those) its size, then shares what's left between the rest by `flex` (1 when not set), within each one's `min-width` and `max-width`. When a row's height is known, its items stretch to fill it. A column with a known height gives its `flex` children the space its other children don't use, so

```
body { size: 1000px 700px; padding: 0; }
#app { flex: 1; }
#sidebar { width: 240px; overflow: auto; }
#editor { flex: 1; }
#terminal { height: 30%; }
```

fills the window: `#app` takes all of `body` that's left, the sidebar and the main column stretch to its height, and the editor gets whatever the terminal doesn't.

Everything else is parsed, cascaded and visible in `grove computed`, ready for the runtime to grow into. Bark also knows `font`, `letter-spacing` and `math-syntax`; any other property name is a warning (`unknown property 'colr'`), and so is a selector naming an element that doesn't exist (custom elements need a `-` in their name).
