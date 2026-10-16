# Grove markup

Grove documents end in `.grove`. The syntax is HTML's: elements, attributes, text and comments.

```html
<!grove>
<grove>
    <head>
        <title>My app</title>
        <style src="theme.bark" />
        <script lang="walnut" src="app.wal"></script>
    </head>
    <body>
        <window id="main">
            <h1>Hello</h1>
        </window>
    </body>
</grove>
```

`<!grove>` and the `<grove>` root are optional; a file can start straight at `<body>`.

## Syntax

- Tag and attribute names are case-insensitive and stored lower-case.
- Attributes: `name="value"`, `name='value'`, `name=value` or a bare `name`. A repeated attribute is a warning and the first one wins.
- `<tag />` closes any element. Void elements (`br`, `hr`, `img`, `input`, `checkbox`, `slider`, `color`) never take a closing tag.
- Comments are `<!-- ... -->`.
- Entities: `&lt; &gt; &amp; &quot; &apos; &nbsp;`, `&#65;` and `&#x41;`. Anything else is left as written.
- Text that is only whitespace between elements is dropped.
- `<math>`, `<script>` and `<style>` are raw: everything up to their closing tag is kept as-is, so `x < 3` or `a && b` need no escaping.
- A mismatched closing tag reports every element it skips over. Unknown tags are a warning unless the name contains a `-` (custom elements such as `<my-card>`).

## Elements

| Element | Purpose |
|---|---|
| `grove`, `head`, `body`, `title` | document structure |
| `window` | a native window; `title`, `width`, `height` attributes or Bark `size` / `title` |
| `panel`, `div`, `row`, `column`, `list`, `item` | blocks |
| `h1`–`h4`, `p` | headings and paragraphs |
| `text`, `span`, `b`, `i`, `u`, `code`, `link` | inline text |
| `button`, `label`, `input`, `checkbox`, `slider`, `color` | controls |
| `img`, `br`, `hr` | images and breaks |
| `math` | MathText, see below |
| `style` | Bark, inline or `src="file.bark"` |
| `script` | Walnut, C++ or C, inline or `src="..."` |

`grove run` currently lays out blocks, headings, paragraphs, text and math; controls and images parse and style but don't draw yet.

## Math

`<math>` holds MathText. Two syntaxes:

```html
<math>(x+2)/(3*exp(4)) \approxeq\ 0.054</math>
<math syntax="brace">fraction{add{"x", 2}, multiply{3, exp{4}}}</math>
```

The default `syntax="infix"` is the human-readable form; `syntax="brace"` is the `name{arg, ...}` form. `display="block"` lays it out as display math. `grove math` prints every block in brace form, and math errors show up in `grove check` at their real line and column in the `.grove` file.

## Scripts

```html
<script lang="walnut" src="app.wal"></script>
<script lang="c++" src="helpers.cpp"></script>
<script lang="c">
    int answer(void) { return 42; }
</script>
```

- `lang` is `walnut` (the default), `walh`, `c++`/`cpp`, `hpp`, `c` or `h`. Without `lang`, the language comes from the `src` extension.
- Any other language is an error.
- `entry="name"` names the function to call after loading.

Scripts reach the document through `grove/api/grove_api.h`, a plain C API so Walnut (`extern "C"`), C and C++ can all use it:

```walnut
extern "C" {
    function grove_set_text_by_id(string: id, string: value) -> int;
} end extern

grove_set_text_by_id("greeting", "Hello from Walnut");
```

C and C++ get the full element-handle API (`grove_find_by_id`, `grove_child`, `grove_get_attribute`, `grove_set_text`, ...); the `*_by_id` functions work on the document the runtime has bound with `grove_bind_document`. Every change bumps the document revision and the runtime re-lays-out on the next frame.

Building scripts into the running app (Walnut to C++, then a shared module the runtime loads) is the next step; today `grove scripts` lists and validates them.
