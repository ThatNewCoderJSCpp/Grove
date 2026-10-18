# Grove for Visual Studio Code

Coloring, color pickers and error underlines for Grove (`.grove`) and Bark (`.bark`) files, plus comment toggling, bracket matching and auto-closing quotes.

`.grove` files get a grove icon and `.bark` files a bark icon in the explorer and on tabs, whenever your file icon theme doesn't have its own icon for them.

## Color pickers

Every color gets a small swatch in front of it, like CSS in VS Code. Click the swatch to open the color picker: a saturation square, a hue strip, an alpha strip, and a header that switches between `rgb`, `hsl` and hex when clicked.

Swatches show up on:

- hex colors (`#fff`, `#ffffff`, `#ffffff80`), `rgb()`, `rgba()`, `hsl()`, `hsla()` and named colors such as `red` or `walnut` in Bark values
- the same inside `<style>` blocks and `style="..."` attributes in `.grove` files
- `<input_color value="#...">`

A picked color is written back in the same form it was in: an `rgba(...)` stays `rgb`/`rgba`, an `hsl(...)` stays `hsl`, and hex and named colors become hex. `<input_color value>` only ever gets hex, since that's the only form its type check accepts. The other forms are in the picker's header.

Swatches come from `grove highlight` (see below), so they're exact: `#main` in a selector is an id, not a color. Without `grove`, the extension still shows swatches on hex and color functions that follow a `:` or `=`.

## Coloring

The coloring works in two layers, like the Walnut extension:

- **The grammars** color the text as soon as a file opens: tags, attributes, entities and comments in `.grove`; selectors, properties, values, units, `$variables`, `@import` and `!important` in Bark. Inside a `.grove` file, `<style>` blocks and `style="..."` attributes are colored as Bark, `<math>` as MathText, and `<script>` by its `lang`: Walnut (needs the Walnut extension), C++ or C.
- **`grove highlight`** fills in what only the parser knows. A moment after you stop typing, the extension runs it on the file (unsaved changes included). It marks tags and properties Grove doesn't know, and `$variables` that were never defined, with the `unknown names` color, and tells input tags and custom tags apart. The same run underlines `grove check`'s errors and warnings and lists them in the **Problems** panel.

Without `grove`, the grammars still color everything; only the unknown-name marks and the underlines are missing.

`grove` only runs when VS Code asks for semantic coloring, which some color themes turn off. If there are no underlines, add `"editor.semanticHighlighting.enabled": true` to your settings.

## Installing

1. In VS Code, press `Ctrl+Shift+X` to open the Extensions panel.
2. Click the `...` at the top of the panel and choose **Install from VSIX...**.
3. Pick the `.vsix` file.

To build the `.vsix` yourself from this folder (it needs [Node.js](https://nodejs.org)):

```
npx @vscode/vsce package --skip-license
```

### The grove program

The extension runs `grove` from your PATH. If it isn't on your PATH, run **Grove: Choose grove Program...** from the command palette (`Ctrl+Shift+P`) and pick `grove` in Grove's build folder; that sets `grove.path`.

## Colors

The colors are in `colors.json`, a list of names and colors, in the same format as Walnut's:

```json
{
    "tags": "#AF00FF",
    "properties": "#00FFFF",
    "unknown names": { "color": "#FFFFFF", "underline": true }
}
```

A color is `"#RRGGBB"`, or an object with `"color"` and any of `"bold"`, `"italic"` and `"underline"`. An empty string `""` leaves that kind of code in the color theme's own color.

| Name | Colors |
|---|---|
| `tags` | Element names, in markup and in Bark selectors |
| `input tags` | `input_integer`, `input_bool` and the other inputs |
| `custom tags` | Names with a `-`, such as `<my-card>` |
| `unknown names` | Tags, properties and `$variables` Grove doesn't know, such as a typo |
| `attributes`, `attribute values` | `title` and `"Demo"` in `title="Demo"` |
| `ids and classes` | `#main`, `.card` and `:hover` in Bark selectors |
| `properties` | `color` in `color: red` |
| `variables` | `$accent` where it's used |
| `variable declarations` | `$accent` in `$accent = #89b4fa;` |
| `functions` | `rgb`, `hsl` and other value functions |
| `values` | Words like `center`, `bold`, `switch` |
| `constants` | Hex colors, named colors and `!important` |
| `numbers`, `units` | `16` and `px` in `16px` |
| `strings`, `escapes`, `comments` | What they say; `escapes` also covers `&amp;` and other entities |
| `at-rules` | `@import`, and `<!grove>` |
| `math commands`, `math functions`, `math names` | `\pi\`, `sqrt` and `x` inside `<math>` |

The colors only change Grove and Bark code and work with any color theme. The extension writes them into your settings (`editor.tokenColorCustomizations` and `editor.semanticTokenColorCustomizations`, as rules named `Grove: ...` and keys ending in `:grove` or `:bark`) and leaves everything else alone, including the Walnut extension's rules. **Grove: Remove Colors From Settings** takes them out again.

### Editing the colors

Run **Grove: Edit Colors**. It opens the colors file in use, and every save shows the new colors at once. The extension uses the first of these that exists:

1. The file in the `grove.colorsFile` setting. **Grove: Choose Colors File...** sets it.
2. `Tools/vscode-grove/colors.json` in the Grove folder `grove` was built in.
3. The copy that came with the extension. **Grove: Edit Colors** copies it to `~/.grove-colors.json` first, so your changes survive reinstalling.

## Settings

| Setting | Does |
|---|---|
| `grove.path` | The `grove` program; empty means `grove` on your PATH |
| `grove.colorsFile` | The colors file, as described above |
| `grove.colors.enabled` | Whether to apply the colors file (default on) |
| `grove.colorPickers` | Whether to show color swatches and pickers (default on) |
| `grove.semanticHighlighting` | Whether to mark unknown names from `grove highlight` (default on) |
| `grove.diagnostics` | Whether to underline errors and warnings (default on) |
| `grove.highlightDelay` | How long to wait after typing stops before asking `grove`, in milliseconds (default 250) |

## The highlight command

`grove highlight file.grove` (or `file.bark`) prints JSON; with `--stdin` it reads the file's text from standard input and uses the path only to find `src` files.

```json
{
    "tokens": [[0, 1, 4, "tag", []], [3, 5, 4, "unresolved", []]],
    "colors": [{"line": 2, "col": 22, "endLine": 2, "endCol": 29, "r": 0, "g": 255, "b": 128, "a": 255}],
    "diagnostics": [{"line": 3, "col": 4, "endLine": 3, "endCol": 14, "severity": "warning", "message": "..."}]
}
```

Lines and columns start at 0, and columns count characters (code points). A token is `[line, column, length, type, modifiers]` with types `tag`, `property`, `variable` and `unresolved`, and modifiers `declaration`, `input` and `custom`.
