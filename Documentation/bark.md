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
| `:current` | the `page` showing in its group, and any element whose `to` points at a page that's showing or a modal that's open (for tabs) |
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
| functions | `rgb()`, `rgba()`, `hsl()`, `hsla()` for colors; `calc()`, `min()`, `max()` and `clamp()` for lengths (see below); anything else is kept for the property to interpret |

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

### Percentages and math

A `%` length is a share of the space the element is in: `width: 50%` is half the width it has, and `height: 50%` is half its parent's height when that height is known (otherwise it's ignored, like `auto`). `font-size: 120%` is relative to the parent's font size.

Lengths can be worked out with `calc()`, `min()`, `max()` and `clamp()`, mixing any units:

| | |
|---|---|
| `calc(100% - 40px)` | `+`, `-`, `*` and `/`, with `*` and `/` first and `( )` to group: `calc((100% - 2em) / 3)` |
| `min(100%, 600px)` | the smallest, so never wider than 600px |
| `max(50%, 200px)` | the largest |
| `clamp(200px, 40vw, 500px)` | the middle value, kept between the first and last |

They work in `width`, `height`, `min-`/`max-` sizes, `padding`, `margin` and `gap`, and a `%` inside one means the same as it would on its own. A formula that can't be worked out, like `calc(100% +)` or `clamp(1px, 2px)`, is an error when the stylesheet loads.

`!important` after a value wins over anything without it. `inherit` copies the parent's value.

## Cascade

For each property the winner is decided by, in order: `!important`, then `style="..."` attributes, then selector specificity (ids, then classes, then tags), then whichever came last. `color`, `font-*`, `line-height`, `text-align`, `letter-spacing`, `white-space`, `math-color`, `accent-color`, `cursor` and the `code-*` colors inherit; nothing else does. `em` and `%` font sizes resolve against the parent.

Grove ships a small default sheet first: heading sizes and weights, `display: none` for `head`/`style`/`script` and for a `<window>` inside `<body>`, a `button` look with `:hover`, `:active`, `:focus` and `:disabled` states (shared by `input_file`), the `input_code` look, `pre { white-space: pre; font-family: monospace; }`, `code` in monospace, `link` in blue, `u` underlined, `row { display: row; gap: 8px; }`, the popup look (`window.popup`, `.popup-icon`, `.popup-buttons`, `.popup-ok` and friends) and the `modal` look, which your own rules can override. A `page` that isn't showing and a closed `modal` are always hidden, whatever `display` says.

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
| `width` | blocks, inputs | a length or a `%` of the space it's in; text inputs and sliders default to 220px; never wider than the space it's in, except inside a block with `overflow-x: auto` |
| `min-width`, `max-width` | blocks, row items | lengths or `%`; in a row they bound how much of the free space an item takes |
| `height` | blocks, `input_code` | a length or a `%` of the parent's height (when the parent's height is known); with `overflow` other than `visible` the block is exactly this tall, otherwise it's the smallest height |
| `min-height`, `max-height` | blocks | lengths or `%`; `max-height` only applies when `overflow` clips |
| `flex` | children of a column, row items | a number: how much of the leftover space the item gets. In a column (any block that isn't a row) this needs the column's height to be known: from `height`, from being an item in a row, or from being `body` |
| `overflow` | blocks | `visible` (default), `hidden` clips the content to the block, `auto` and `scroll` clip it and let the mouse wheel scroll it up and down. A bar on the right shows where you are; drag it to scroll, or click above or below it to jump there (code editors' bars work the same way) |
| `overflow-x`, `overflow-y` | blocks | the same for one direction. `overflow-y` defaults to `overflow`; `overflow-x` scrolls only when it's set to `auto` or `scroll`. Inside a block that scrolls sideways, images keep their own size and a `width` can be wider than the block instead of being shrunk to fit; Shift+wheel or a sideways wheel scrolls it, and a bar along the bottom shows where you are and can be dragged too |
| `zoom` | `body`, `window` | scales every `px` and `pt` length, font size and image in the window: `zoom: 1.25` (or `125%`) makes everything a quarter bigger. Viewport units are already relative to the window, so they don't change |
| `appearance` | `input_bool` | `checkbox` (default) or `switch` |
| `accent-color` | inputs | checked boxes and radios, slider fill and thumb, focused border, the caret and selection of `input_code` |
| `gap` | blocks, `row` | extra space after a block; space between the items of a row |
| `align` | blocks, `row` | the easy way to place everything inside: `center`, or a side and/or a corner like `top`, `bottom right`, `center left`. See [Centering and alignment](#centering-and-alignment) |
| `justify-content` | blocks, `row` | along the direction things are laid out (down a column, across a row): `start` (default), `center`, `end`, `space-between`, `space-around`, `space-evenly` |
| `align-items` | blocks, `row` | across it: `stretch` (default), `start`, `center`, `end` |
| `align-self` | a child | its own `align-items` |
| `line-height` | `input_code` | a multiple of the font size (`1.4`) or a length |
| `code-keyword`, `code-type`, `code-function`, `code-namespace`, `code-variable`, `code-constant`, `code-string`, `code-escape`, `code-number`, `code-comment`, `code-preprocessor`, `code-operator`, `code-tag`, `code-attribute`, `code-property` | `input_code` | how that kind of token looks: a color, then any of `bold`, `italic`, `underline` and `strikethrough` (`line-through` works too) (`code-comment: #6a737d italic;`). Leaving one out keeps Grove's own color |
| `code-control-keyword`, `code-builtin-type`, `code-declaration`, `code-self`, `code-this`, `code-cast`, `code-builtin-operator`, `code-overloaded-operator`, `code-enum-member`, `code-template-scope`, `code-variable-template`, `code-unresolved` | `input_code` | the finer kinds C, C++ and Walnut split out; one you leave out looks like the kind it came from (`code-keyword`, `code-type`, `code-variable`, `code-operator`, `code-function`, `code-constant` or `code-plain`) |
| `code-error-underline`, `code-warning-underline` | `input_code` | the squiggle under the `problems` an editor is given (default red and amber) |
| `code-error-line` | `input_code` | a color and any of `bold`, `italic`, `underline`, `strikethrough` for the text of a line with an error; off unless set |
| `code-hover` | `input_code` | the background of its hover popup (default a little lighter than `background`) |
| `code-plain` | `input_code` | text that isn't any kind of token (defaults to `color`) |
| `backdrop` | `modal` | the color drawn over the window behind an open modal (default a see-through black) |
| `code-mark` | `input_code` | the box drawn behind tokens named in the `mark` attribute (default a see-through orange) |
| `opacity` | any | 0 to 1; fades the element and everything inside it |
| `cursor` | any | `pointer`, `text`, `default`, `crosshair`, `move`, `not-allowed`, ... while the mouse is over it |
| `text-align` | text, `math` | `left`, `center`, `right` (`start` and `end` mean left and right) |
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

### Centering and alignment

There are two ways to place things, and they're the same machinery underneath.

**The easy way** is one property, `align`, on the box whose contents you want to place. It moves the children and the text together:

```css
.dialog { height: 300px; align: center; }        /* dead center, both ways */
.toolbar { align: right; }                        /* everything to the right */
.badge-holder { height: 80px; align: bottom right; }
```

| `align:` | puts the contents |
|---|---|
| `center` | in the middle, both ways |
| `top`, `bottom`, `left`, `right` | against that side; the other direction is left alone |
| `top left`, `top center`, `top right` | along the top |
| `center left`, `center right` (or `middle left`) | halfway down, against a side |
| `bottom left`, `bottom center`, `bottom right` | along the bottom |

Markup has the same shortcuts: `<div center>` is `align: center`, `<p align="right">` and `<div align="bottom right">` set `align`, and `<center>...</center>` is a box that fills the space left over (`flex: 1`) and centers what's in it, so `<body><center>...</center></body>` puts something in the middle of the window. A stylesheet rule wins over the attributes.

**The full-control way** is the CSS flexbox set, on any block or row:

| Property | |
|---|---|
| `justify-content` | spacing along the direction children are laid out (down a column, across a `row`): `start`, `center`, `end`, `space-between` (first and last at the edges), `space-around` (equal space around each), `space-evenly` (equal gaps, edges included) |
| `align-items` | where children sit across that direction: `stretch` (full width in a column, the row's height in a row), `start`, `center`, `end`. Anything not stretched is only as wide as its content |
| `align-self` | the same for one child |
| `margin: auto` | an `auto` margin takes the free space on its side: `margin: 0 auto` centers a box across, `margin: auto` centers it both ways, `margin: 0 0 0 auto` pushes it to the right (or to the end of a row) |
| `text-align` | lines of text inside their box |

`align` is a shortcut for these: in a column, its vertical part sets `justify-content` and its horizontal part sets `align-items` and `text-align`; in a row it's the other way round. If you also set them yourself, yours win.

Up-and-down placement needs to know how tall the box is: give it a `height` (or `%`, `calc()`), make it a `flex` child of something with a known height, or use `body`, which is the window. Without that a box is exactly as tall as its contents and there's nothing to center in. Across is always known.

Everything else is parsed, cascaded and visible in `grove computed`, ready for the runtime to grow into. Bark also knows `font`, `letter-spacing` and `math-syntax`; any other property name is a warning (`unknown property 'colr'`), and so is a selector naming an element that doesn't exist (custom elements need a `-` in their name).
