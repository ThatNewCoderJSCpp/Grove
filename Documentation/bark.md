# Bark

Bark is Grove's style language. It reads like CSS, with variables and nesting built in.

```
@import "colors.bark";

$accent = #89b4fa;
$gap = 16px;

window {
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
| `window p` | `p` anywhere inside a `window` |
| `window > p` | `p` directly inside a `window` |
| `a, b` | either |

Inside a rule, a nested rule is a descendant of its parent. Start it with `>` for a direct child, or `&` to add to the parent itself (`&.wide` is `panel.card.wide`).

## Values

| Kind | Examples |
|---|---|
| numbers with units | `16px`, `1.5em`, `12pt`, `50%`, `0` |
| colors | `#fff`, `#ffffff`, `#ffffff80`, `rgb(1, 2, 3)`, `rgba(1, 2, 3, 50%)`, `hsl(210, 80%, 60%)`, names like `white` or `walnut` |
| strings | `"Hello"`, `'Hello'` |
| identifiers | `bold`, `center`, `none` |
| functions | `name(args)`; anything other than the color functions is kept for the property to interpret |

`!important` after a value wins over anything without it. `inherit` copies the parent's value.

## Cascade

For each property the winner is decided by, in order: `!important`, then `style="..."` attributes, then selector specificity (ids, then classes, then tags), then whichever came last. `color`, `font-*`, `line-height`, `text-align`, `letter-spacing`, `math-color` and `cursor` inherit; nothing else does. `em` and `%` font sizes resolve against the parent.

Grove ships a small default sheet first (heading sizes and weights, `display: none` for `head`/`style`/`script`).

## Properties the runtime reads today

| Property | On | |
|---|---|---|
| `size` | `window` | `width height` |
| `title` | `window` | window title (the `title` attribute wins) |
| `background` | any | fill color |
| `color`, `font-size`, `font-weight` | text | |
| `math-color` | `math` | defaults to `color` |
| `margin`, `padding` | blocks | 1 to 4 lengths, CSS order |
| `border-color`, `border-width` | blocks | |
| `gap` | blocks | extra space after the block |
| `text-align` | `math` | `left`, `center`, `right` |
| `display` | any | `none` hides it |

Everything else is parsed, cascaded and visible in `grove computed`, ready for the runtime to grow into.
