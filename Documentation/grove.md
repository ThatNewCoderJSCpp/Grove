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
    <body title="My app">
        <h1>Hello</h1>
    </body>
</grove>
```

`<!grove>` and the `<grove>` root are optional; a file can start straight at `<body>`. `<!grove>` can also say what the document is built for: `<!GROVE type=android package=com.example.recipes label="Recipe Box">` makes it an Android app (see [Android](#android)).

`<body>` is the app's window. Its `title`, `width` and `height` attributes, or Bark's `size` and `title` on `body`, set up the window; without them the title comes from `<title>` and the size defaults to 800×600. A document with no `<body>` draws everything in one window. Every `<window>` is a separate window of its own; see [Windows](#windows).

## Syntax

- Tag and attribute names are case-insensitive and stored lower-case.
- Attributes: `name="value"`, `name='value'`, `name=value` or a bare `name`. A repeated attribute is a warning and the first one wins.
- `<tag />` closes any element. Void elements (`br`, `hr`, `img`, the inputs except `input_bool` and `input_radio`) never take a closing tag.
- Comments are `<!-- ... -->`.
- Entities: `&lt; &gt; &amp; &quot; &apos; &nbsp;`, `&#65;` and `&#x41;`. Anything else is left as written.
- Text that is only whitespace between elements is dropped.
- `<math>`, `<script>` and `<style>` are raw: everything up to their closing tag is kept as-is, so `x < 3` or `a && b` need no escaping.
- A mismatched closing tag reports every element it skips over. Unknown tags are a warning unless the name contains a `-` (custom elements such as `<my-card>`).

## Elements

| Element | Purpose |
|---|---|
| `grove`, `head`, `body`, `title` | document structure |
| `window` | a separate window, set up like `<body>`; it isn't drawn inside the window it sits in. See [Windows](#windows) |
| `panel`, `div`, `container`, `row`, `column`, `list`, `item`, `pre` | blocks; `container` is the same as `div`, and `pre` keeps its spaces and line breaks |
| `center` | a block that fills the space left over and puts its contents in the middle. See [Centering and alignment](bark.md#centering-and-alignment) |
| `page` | one screen of the app; only one page in a group shows at a time. See [Pages, tabs and modals](#pages-tabs-and-modals) |
| `modal` | a dialog drawn on top of the window. See [Pages, tabs and modals](#pages-tabs-and-modals) |
| `h1`–`h4`, `p` | headings and paragraphs |
| `text`, `span`, `b`, `i`, `u`, `code`, `link` | inline text |
| `input_integer`, `input_float`, `input_string`, `input_math`, `input_bool`, `input_radio`, `input_slider`, `input_color` | typed inputs, see below |
| `input_code` | a code editor, see [Code editor](#code-editor) |
| `input_file` | a button that opens the system's file or folder picker, see [File picker](#file-picker) |
| `input_select`, `option` | a dropdown, see [Dropdowns](#dropdowns) |
| `button`, `label` | controls |
| `img`, `br`, `hr` | images and breaks |
| `canvas` | pixels the app draws itself, see [Canvas](#canvas) |
| `math` | MathText, see below |
| `style` | Bark, inline or `src="file.bark"` |
| `script` | Walnut, C++ or C, inline or `src="..."` |

Any block can also take `center` (`<div center>`) or `align="..."` (`<p align="right">`, `<div align="bottom right">`) to place what's inside it, the same as Bark's `align`.

`grove run` lays out blocks, headings, paragraphs, wrapped text with inline elements, math, buttons, images and every input. Bark's [Layout](bark.md#layout) section covers widths, `flex`, scrolling and wrapping.

### Images and window icons

`<img src="logo.png">` draws a PNG, JPEG, BMP, GIF, TGA, QOI or other image Fizmo can read, at its own size unless `width`/`height` (attributes in pixels, or Bark lengths) say otherwise; with only one of them it keeps its shape. It never gets wider than the space it's in. Paths are relative to the `.grove` file, and `grove build` packs the images in.

`<body>` and `<window>` take an `icon` with one or more images, which becomes the window's icon in the taskbar and title bar. Giving several sizes lets the system pick the sharpest one. A `<window>` without its own uses the `<body>`'s.

```html
<body title="Notes" icon="icons/notes-16.png icons/notes-32.png icons/notes-256.png" app-id="notes">
```

`app-id` names the app to the desktop (on Linux it's the window's `WM_CLASS`, which a `.desktop` file matches with `StartupWMClass=notes`). `grove check` warns about an `icon` or `src` file it can't find.

## Pages, tabs and modals

An app with several screens keeps them all in one document as `<page>` elements. Pages that are next to each other form a group, and only one page in a group shows at a time: the one with the `current` attribute, or the first one if none has it. Any element with `to="id"` switches to that page when it's clicked (or when Enter or Space is pressed on a focused button), so a row of buttons and a few pages make tabs:

```html
<row class="tabs">
    <button class="tab" to="home">Home</button>
    <button class="tab" to="settings">Settings</button>
    <button class="tab" to="back">Back</button>
</row>
<page id="home" title="My app">...</page>
<page id="settings" src="pages/settings.grove" />
```

```css
.tab:current { background: #1e1e2e; color: #cdd6f4; }
```

| | |
|---|---|
| `to="settings"` | show that page (`#settings` works too). Pages around it are shown as well, so a link can go straight to a page inside another page |
| `to="pages/settings.grove"` | show the page whose `src` is that file |
| `to="back"` | go back to the page that was showing before, like a browser's back button |
| `to="about"` on a `<modal>`'s id | open that modal |
| `to="close"` | close the modal the element is in |
| `:current` | matches the shown page in each group, and every element whose `to` points at a page that's showing (or a modal that's open), for highlighting the current tab |
| `title` on a page | the window's title while that page shows |

`<page src="file.grove">` fills the page from another file, so each screen can have its own file the way a website has one HTML file per page. The file is an ordinary Grove document: whatever is in its `<body>` goes into the page, its `<style>` and `<script>` elements come along, its `<title>` becomes the page's `title`, and paths in it (`src`, `icon`) still work relative to that file. Included files can have pages of their own, and a file that ends up including itself is an error. `grove build` bundles them like any other file.

A `<modal>` stays hidden until it's opened. Then it's drawn in the middle of the window with everything behind it dimmed (the `backdrop` color), and only the modal takes clicks and keys. Clicking outside it or pressing Escape closes it, unless it has the `persistent` attribute. Its size comes from `width` (480px by default), `min-width`, `max-width`, `height` and `max-height`, and it's styled like any block: `background`, `padding`, `border-color`, `border-radius`. A link inside a modal that goes to a page also closes the modal.

```html
<button to="confirm-delete">Delete…</button>
<modal id="confirm-delete">
    <h3>Delete this note?</h3>
    <button to="close">Cancel</button>
    <button id="really-delete">Delete</button>
</modal>
```

Pages fire `page_show` and `page_hide`, and modals fire `open` and `close`. Scripts can do the same things:

| Walnut and C++ | C | |
|---|---|---|
| `navigate("settings")` | `grove_navigate(doc, "settings")` | anything `to` accepts |
| `go_back()` | `grove_go_back` | |
| `element.show_page()` | `grove_show_page` | |
| `element.open_modal()`, `element.close_modal()` | `grove_open_modal`, `grove_close_modal` | |
| `element.is_current()` | `grove_is_current` | what `:current` matches |

From C++ outside a script, `grove/pages.hpp` has `grove::navigate`, `show_page`, `go_back`, `open_modal`, `close_modal` and `open_modals`.

## Inputs

Each input is its own element, named after the type of value it holds, so the type is fixed by the markup and can't drift:

| Element | Value type (C++ / C / Walnut) | Typing accepts |
|---|---|---|
| `<input_integer>` | `std::int64_t` / `int64_t` / `long int` | `0`–`9`, and a leading `-` only if `min` allows negatives |
| `<input_float>` | `double` / `double` / `short float` | digits, one `.`, a leading `-` if allowed |
| `<input_string>` | `std::string` / `const char*` / `string` | any printable text, up to `maxlength` characters |
| `<input_math>` | `std::string` / `const char*` / `string` | the same as `input_string`, drawn as math while you type; see [Math input](#math-input) |
| `<input_bool>Label</input_bool>` | `bool` | click to toggle; drawn as a checkbox or a switch |
| `<input_radio name="g" value="v">Label</input_radio>` | `bool` per option, `string` per group | click to pick; one per `name` group |
| `<input_slider>` | `double` | click or drag |
| `<input_color>` | `bark::Rgba` / `uint32_t` `0xRRGGBBAA` | `#` and hex digits |
| `<input_code>` | `std::string` / `const char*` / `string` | everything; see [Code editor](#code-editor) |
| `<input_file>` | `std::string` / `const char*` / `string`, the chosen path (several are separated by `\n`) | nothing; clicking opens the system picker |
| `<input_select>` | `std::string` / `const char*` / `string`, the chosen `<option>`'s value | nothing; clicking opens the list |

```html
<input_integer id="count" value="3" min="0" max="99" />
<input_float id="temperature" value="21.5" step="0.5" />
<input_string id="name" maxlength="24" placeholder="Type your name" />
<input_bool id="dark" checked>Dark mode</input_bool>
<input_radio name="size" value="small">Small</input_radio>
<input_radio name="size" value="large" checked>Large</input_radio>
<input_slider id="volume" min="0" max="1" step="0.05" value="0.6" />
<input_color id="accent" value="#89b4fa" />
```

| Attribute | On | |
|---|---|---|
| `value` | integer, float, string, slider, color, radio | the value; for radio, the option's value in its group |
| `min`, `max` | integer, float, slider | the value is clamped into range; a slider defaults to 0..100 |
| `step` | integer, float, slider | Up/Down step for integer and float; a slider snaps to it |
| `maxlength` | string | longer text is cut |
| `allow` | string | only these kinds of characters can be typed or pasted: `letters`, `digits`, `numbers`, `spaces`, `newlines`, `punctuation`, `symbols`, `special`, `uppercase`, `lowercase`, `emoji`, `ascii`, `hex`, `word`, separated by spaces |
| `block` | string | these kinds can't be typed; `special` (punctuation and symbols) still lets `.` through so decimals work |
| `chars` | string | single characters let through on top of `allow` and `block`, like `chars="'-"` |
| `block-chars` | string | single characters that are always refused |
| `pattern` | string | a regular expression the whole text has to match before it's accepted; until it does, Enter leaves the field red. See [Fizmo's regex guide](https://github.com/ThatNewCoderJSCpp/Fizmo/blob/main/Documentation/regex.md) |
| `placeholder` | integer, float, string | shown while empty |
| `masked` | string | draws each character as a dot, for passwords; the value itself is unchanged |
| `checked` | bool, radio | on |
| `switch` | bool | draw it as a switch instead of a checkbox |
| `name` | radio | the group |
| `disabled` | all | greyed out and not editable |
| `format` | color | how the value is written in the box: `hex` (default), `rgb`, `rgba`, `hsl` or `hsla`. Any of them can be typed whatever the format; a see-through color written as `rgb` or `hsl` shows as `rgba` or `hsla` so the alpha isn't lost |
| `picker` | color | `false` turns off the picker |
| `alpha` | color | `false` hides the picker's alpha slider |

Clicking the swatch at the left of an `input_color` opens a picker under it: a wheel for hue and saturation, a brightness slider, an alpha slider, the new color next to the one it started as, and the color written as hex, RGB, RGBA, HSL and HSLA. Clicking one of those rows sets the box's `format`. Dragging fires `input`, letting go fires `change`. Clicking outside the picker or pressing Enter keeps the color, and Escape puts the old one back.

While one of these has focus, typing goes in at the caret: Left and Right move it (by a word with Ctrl or Alt), Home and End jump to the ends, Backspace and Delete remove the character before or after it, and clicking puts the caret where you click. Text longer than the box scrolls to keep the caret in view, with … at whichever end is cut off. Ctrl+A selects the whole value so the next key replaces it (or Backspace clears it); a color input starts out selected, so clicking it and typing a new hex code replaces the old one.

A bool input looks like a checkbox by default. To draw it as a switch (a pill with a circle that slides left for off and right for on), either add the `switch` attribute or set `appearance: switch` in Bark; `appearance: checkbox` goes back. The attribute wins over Bark. `<input type="toggle">` is a switch too. Only the look changes: the value is the same `bool` either way.

```html
<input_bool id="sound" switch>Sound effects</input_bool>
<input_bool id="notify" class="toggle" checked>Notifications</input_bool>
```

```
.toggle {
    appearance: switch;
}
```

Type safety holds at every layer:
- **Markup.** `grove check` reports a `value`, `min`, `max` or `step` that isn't the input's type (`<input_integer value="1.5">` is an error), `min > max`, a radio with no `name` or `value`, two checked radios in one group, a color that isn't `#rrggbb`, and attributes that don't belong to that input.
- **Typing.** Keys that can't be part of the type are refused, so an integer field never holds a letter. Enter commits; if the text still isn't a valid value (an empty integer, a lone `-`) the field turns red and stays focused. Escape puts back the last valid value; clicking away or Tab commits if the text is valid and otherwise keeps the last valid value. Out-of-range values are clamped when committed. Up/Down step numbers, and Tab moves between fields.
- **C++.** `grove::input_value<T>(node)` only compiles for `std::int64_t`, `double`, `bool`, `std::string` and `bark::Rgba`, and returns empty when `T` isn't that input's type. `set_input_value<T>` refuses the wrong type.
- **C and Walnut.** Every getter and setter is typed (`grove_get_integer_by_id` returns `int64_t`, `grove_set_float_by_id` takes `double`, ...). A setter called on the wrong kind of input returns `false` and changes nothing; a getter returns 0, `false` or `""`. `grove_input_kind_by_id` tells you the kind. In Walnut, using the result as the wrong type is a compile error.

```html
<input_string id="price" block="letters special spaces" placeholder="0.00" />
<input_string id="name" allow="letters spaces" chars="'-" />
<input_string id="code" pattern="[A-Z]{3}-\d{4}" placeholder="ABC-1234" />
```

`allow`, `block`, `chars` and `block-chars` build a Fizmo `CharSet` (`grove::input_charset(node)` gives it to C++), and `pattern` is a Fizmo `Regex` (`grove::input_pattern(node)`); `grove check` reports a pattern that doesn't work and a kind it doesn't know. Scripts get the same classes: C++ through `fizmo::Regex`, `fizmo::Pattern` and `fizmo::CharSet`, and Walnut through `import module.regex`.

`<input type="integer">` (and `float`, `string`, `bool`, `radio`, `slider`, `color`) still works and means the same as the matching `input_*` element; `checkbox`, `slider` and `color` are kept as short names for `input_bool`, `input_slider` and `input_color`.

`grove inputs doc.grove` lists every input with its type and current value.


### Math input

`<input_math>` is a text box whose text is drawn as math as you type, the way a graphing calculator shows it. The value is still the plain text that was typed (`x^2/3`, not a picture), so `value_string()`, `set_value_string()`, the `input` and `change` events and every `input_string` attribute work the same, and the caret, selection and clicks follow the characters:

| Typed | Drawn |
|---|---|
| `x^2`, `x^(n+1)`, `x^{n+1}` | a superscript, smaller and raised; the `^` isn't drawn. Round brackets you type are drawn (`y^(4)` shows as y⁽⁴⁾); curly braces group without being drawn (`x^{n+1}`). Powers nest: `e^x^2` |
| `a_1`, `x_{12}` | a subscript |
| `a/b`, `{x+1}/{x-1}`, `(x+1)/(x-1)` | a stacked fraction: the term before `/` over the term after it. Curly braces around either aren't drawn; round brackets you type are. Fractions nest |
| `sqrt(x)`, `cbrt(x)`, `nthroot(x, 5)` | a radical sign over the argument, with a small 3 or the index for the other roots |
| `*`, `<=`, `>=`, `!=`, `-` | `·`, `≤`, `≥`, `≠`, `−` |
| `alpha` … `omega`, `Gamma` … `Omega`, `varepsilon`, `vartheta`, `varphi`, `pi`, `tau`, `inf` | the Greek letter or symbol |
| `sin`, `cos`, `ln`, `exp`, … and any longer name followed by `(` | upright, like a function name; single letters are italic |

Brackets around something taller than a line (a fraction, say) grow to fit, and the box grows taller to fit fractions and powers. Text that isn't finished yet (`x^` at the end, an unclosed `sqrt(`) is drawn as typed. It's styled like `input_string`: `font-size`, `color`, `padding`, `background`, `border-*`, `width` and `flex` all apply. In a row, a text input with `flex` takes the room that's left (its default 220px width no longer applies), like any other flexible child.

```html
<input_math id="f" value="y = x^2 + sqrt(x+1)/2" placeholder="f(x)" />
```

Typing builds the structure for you, the way a graphing calculator does, by adding hidden curly braces to the text:

| Typed | Text | Caret |
|---|---|---|
| `^`, `_` | `^{}`, `_{}` | inside the braces: everything typed stays in the power or subscript until → (or the mouse) takes the caret out |
| `/` | `{numerator}/{}` | in the denominator. The numerator is the term before the caret, back to the nearest `+ - = < > , ; : \|`, space or opening bracket; with nothing before it, the caret goes to an empty numerator |
| `sqrt`, `cbrt` | `sqrt()`, `cbrt()` | under the radical sign; `)` or → steps out |
| `(` | `(` | brackets you type are always kept and drawn |

An empty pair of braces is drawn as a small box so you can see where the caret goes. Backspace knows the structure too: right after a hidden `}` it moves the caret inside instead of deleting it; at the start of a power or subscript it removes the `^{` `}` and keeps what was in it (or removes it all when empty); at the start of a numerator or denominator it takes the fraction apart, leaving the numerator followed by the denominator; and at the start of a root it removes the root sign and keeps what's under it.

Every one-line text input (`input_string`, `input_math`, the number inputs) selects with Shift+arrows, Shift+Home/End or by dragging, replaces the selection when you type, and has Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) to undo and redo while it has focus.

### Code editor

Any element with a `reveal` attribute is scrolled to the top of the block it scrolls in, once, the next time it's laid out. Change the attribute's value (`reveal="1"`, then `"2"`) to scroll there again; leaving it as it is doesn't fight the user's scrolling.

`<input_code>` is a multi-line editor with line numbers, syntax highlighting, a caret and selection, and its own scrolling. Its text is the element's content (indentation shared by every line is removed, so it can be indented with the markup) until `value` is set; after that, `value_string()` and `set_value_string()` read and replace it like any string input.

```html
<input_code id="source" language="cpp" tab-size="4">
    int main() {
        return 0;
    }
</input_code>
```

| Attribute | |
|---|---|
| `language` | `c`, `cpp`, `walnut`, `grove`, `bark`, `cmake`, `html`, `css`, `javascript`, `json`, `rust`, `zig`, `python`, `lua`, `java`, `csharp`, `asm`, `mathtext`, `markdown`, `text` or `plain` (default); file extensions like `hpp`, `wal`, `htm`, `js`, `mjs`, `rs`, `py`, `cs`, `s`, `nasm`, `math`, `mtb`, `md` or `txt` work too. `mathtext` colors Walnut and Fizmo math text (commands, environments, `$` and brace groups), `markdown` colors headings, emphasis, links, lists, quotes and fenced code, and `text` only picks out links, quoted text and numbers. `asm` covers NASM, GAS (AT&T and Intel), MASM and FASM, and x86, ARM, RISC-V and MIPS registers and instructions. In `html`, `<script>` and `<style>` are highlighted as JavaScript and CSS, and typing `>` after an opening tag adds its closing tag except for void elements like `<br>` and `<img>` |
| `tab-size` | columns per tab (default 4) |
| `indent` | `spaces` (default) or `tabs`: what Tab and Enter insert |
| `tab-backspace` | `true` makes Backspace at a tab stop with a whole level of spaces before it delete that level at once (so with 4-column tabs, 8 spaces take two presses); between tab stops it deletes one space at a time; Ctrl+Backspace still deletes a word |
| `close-blocks` | `true` makes Enter after an opening `{`, `(` or `[` add the closing line when the block isn't closed yet, worked out from the `language`: `} end function name` (and class, struct, union, namespace, `end extern`) in Walnut, `};` after a C or C++ class, struct, enum, union, lambda or `= {`, `} // namespace name` for a C++ namespace, `});` when the line still has an open `(`, and `</tag>` after an opening tag in Grove markup. Braces in strings and comments are ignored, and Enter just before a `}` that is already there completes it (`} end function main`). Enter between `<tag>` and `</tag>` splits them either way |
| `line-comment` | what Ctrl+/ puts in front of each selected line to comment it out (or removes when they all have it); without it, the language's own: `//`, `#` in Python, `--` in Lua and CMake's `#`, and `;`, `#` or `//` in assembly depending on what the file already uses. In Python, Enter indents after a `:` and dedents after `return`, `pass`, `break`, `continue` and `raise`; in Lua it indents after `then`, `do`, `function` and `repeat` and, with `close-blocks`, adds the `end` or `until` |
| `line-numbers` | `false` hides them |
| `readonly` | can be selected and copied but not changed |
| `placeholder` | shown while it's empty |
| `mark` | token kinds like `"keyword string"`: draws a box behind every token of those kinds, to show what a color changes |
| `caret` | `line:column` (1-based) moves the caret there and scrolls to it whenever the attribute changes; anything after a `#` is ignored, so `"12:1#2"` moves there again |
| `semantic` | colors from a compiler, drawn over the built-in highlighting; build it with `grove::code_spans_text()` (see below) |
| `problems` | errors and warnings to underline; build it with `grove::code_problems_text()` |
| `hover` | what to show in the popup the editor asked for with a `hover` event, see [Hovers and Ctrl+click](#hovers-and-ctrlclick) |
| `hover-width` | widest the hover popup gets, in pixels (default 900); code in it wraps only past this |
| `hover-height` | tallest the hover popup gets, in pixels (default 60% of the window); longer hovers scroll with the wheel |
| `autocomplete` | `false` turns the suggestion popup off, see [Autocomplete](#autocomplete) |
| `completions` | the suggestions the app gives for a `complete` event |
| `word-chars` | characters besides letters, digits and `_` that belong to a word being completed, like `-` for CSS-style names |
| `complete-triggers` | space-separated strings that open suggestions right after them, like `. :: ->` |
| `word-completions` | `false` stops Grove suggesting the document's own words when the app gives no list |
| `accept-on-enter` | `true` lets Enter pick a suggestion as well as Tab |
| `complete-rows`, `complete-width` | how many suggestions show at once (default 10) and the popup's widest width in pixels (default 760); names, kinds and details each get their own column, and a name too long for its column ends in "…" instead of running into the next |
| `complete-icons`, `complete-kinds` | `false` hides the colored letter beside each suggestion (F, T, V, ...); `true` writes what each suggestion is (function, struct, enum, keyword, ...) beside its name (default `true` and `false`) |
| `links` | spans that Ctrl+click goes somewhere from (built like `semantic`); they're underlined while Ctrl is held over them |
| `link-words` | `true` treats every word as a possible link, for an app that works out where it goes when it's clicked |
| `color-swatches` | `false` turns off the color swatches in Bark and Grove |
| `find` | text to find: every match gets a box behind it (`code-find` in Bark sets its color, `code-find-current` the current one's). Setting it selects the first match at or after the caret. Empty turns it off |
| `find-case`, `find-word`, `find-regex` | `true` matches case, only matches whole words, or reads `find` as an ECMAScript regular expression (one line at a time) |
| `replace` | what Replace puts in; with `find-regex` on, `$1` and `$&` put in what the pattern caught |
| `find-command` | `next`, `prev`, `select-all` (a caret and selection on every match), `replace` (the selected match, then on to the next; with no match selected it goes to the next one first), `replace-all` (one undo step) or `seed` (fires `find-seed` with the selected text); anything after a `#` is ignored, so `"next#7"` runs it again |

Keys: arrows, Home (to the indent, then the line start), End, Page Up/Down, Ctrl+Home/End, Ctrl+Left/Right by word, Shift with any of them to select; Backspace and Delete (Ctrl for a whole word; Backspace between `()`, `[]`, `{}`, `""` or `''` removes both); Enter keeps the indent and indents after `{`, `(` or `[`; Tab and Shift+Tab indent and outdent (every selected line when the selection spans lines; with just a caret after spaces that follow text, as after a Tab at the end of a line, or on a line that doesn't start with spaces, like `//     name`, Shift+Tab takes the spaces just before the caret back to the previous tab stop, so it undoes that Tab instead of outdenting the line); Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V (with nothing selected, copy and cut take the whole line); Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z undo and redo. Click to move the caret, drag or Shift+click to select, double-click selects a word, and the wheel scrolls (Shift+wheel sideways).

It can have more than one caret. Alt+click adds one (or removes the one you click on). Shift+click straight above or below the caret, in the same column, puts a caret on every line in between; anywhere else Shift+click selects as usual. Shift+Up and Shift+Down do the same from the keyboard while nothing is selected, adding a caret on the next line each time (go back the other way to take them off again). Once something is selected they extend the selection, so Shift+Right then Shift+Down still selects whole lines. With several carets, typing, Backspace, Delete, Enter, Tab, the arrows, Home and End act at every caret and count as one step for undo. Copy and cut take every selection, one per line, and pasting as many lines as there are carets puts one line at each. Ctrl+Alt+drag selects a rectangle: the same columns on every line between where you start and where you let go, and dragging straight up or down puts a caret on each line. Escape goes back to one caret. `input` fires on every edit, `change` when it loses focus with different text, and `cursor` whenever the caret moves, with `x()` as the line and `y()` as the column. Listen for `key_down` and call `prevent_default()` to take a key for yourself; the character it would have typed is dropped too, here and in every input. The `keys` attribute rebinds its commands, as `action=chord` pairs separated by `;`, several chords with `,`, and `none` to turn one off: `keys="copy=Ctrl+Shift+C, Ctrl+Insert; redo=Ctrl+Y; add-caret=Ctrl+Alt+Click"`. A command's old chord stops working once it's moved. The commands are `select-all`, `copy`, `cut`, `paste`, `undo`, `redo`, `indent`, `outdent`, `delete-word-left`, `delete-word-right`, `word-left`, `word-right`, `select-word-left`, `select-word-right`, `file-start`, `file-end`, `scroll-up`, `scroll-down`, `caret-above`, `caret-below` and `single-caret` on the keyboard, and `go-to-definition`, `add-caret`, `column-select` and `box-select` with the mouse; `grove::code_editor_actions()` lists them with their default chords, and `grove::parse_chord()` reads `Ctrl+Shift+M`-style text. Its colors follow `color`, `background`, `border-color` and `accent-color` (caret, selection and focus), and each kind of token takes its color from a `code-*` property, so a theme is a few lines of Bark:

```css
input_code {
    background: #101014;
    code-keyword: #ff4fd8 bold;
    code-string: #4dff9a;
    code-comment: #5c6370 italic;
}
```

The kinds are `keyword`, `type`, `function`, `namespace`, `variable`, `constant`, `string`, `escape`, `number`, `comment`, `preprocessor`, `operator`, `tag`, `attribute` and `property`, plus finer ones split from those: `control-keyword` (`if`, `return`), `builtin-type` (`int`, `bool`), `this`, `cast` (`static_cast`), and for Walnut `declaration`, `self`, `builtin-operator`, `overloaded-operator`, `enum-member`, `template-scope`, `variable-template` and `unresolved`. These are the same groups the Walnut VS Code extension colors. One you don't set looks like the kind it was split from (`code-control-keyword` falls back to `code-keyword`, `code-declaration` to `code-variable`, and so on; `grove::code_token_fallback()` says which), so older themes keep working. Walnut is highlighted the way the extension's grammar does it: `int: x` makes `x` a declaration, `end function name`, `operator(+)`, `traits<T>::` as a template scope, nested `/* */` comments and `` `${...}` `` strings. C and C++ pick out namespaces (`std::`), standard types, escapes in strings, raw strings, `[[attributes]]` and macros; CMake (`CMakeLists.txt` and `.cmake`) colors commands, `${variables}`, `$<generator expressions>`, argument keywords like `PRIVATE`, and `#[[ ]]` comments. Rust, Zig, Python, Lua, Java and C# get keywords, types, builtins, strings (raw strings, f-strings, Lua's `[[ ]]`, C#'s verbatim and interpolated strings), nested block comments and attributes or annotations; assembly colors instructions (jumps as control keywords), registers, directives, size words like `qword`, labels and numbers. From C++, `grove::code_token_kinds()`, `grove::code_languages()`, `grove::code_token_kinds_for()` and `grove::code_token_kind_from_name()` list and look them up.

Some colors need a compiler: whether a name is an enum member, an overloaded operator or something that doesn't exist. Give those to the editor with the `semantic` attribute and it draws them over its own highlighting; `problems` underlines errors (`code-error-underline`) and warnings (`code-warning-underline`), and colors the text of a line with an error with `code-error-line` when that's set. Each line is matched by its text, so when lines are added above, the colors stay on the right line until the next result comes in, and a line that was edited goes back to the built-in colors.

```cpp
std::vector<grove::CodeSpan> spans = { { 3, 4, 3, grove::CodeTokenKind::EnumMember } };
std::vector<grove::CodeProblem> problems = { { 5, 4, 4, grove::CodeProblemKind::Error } };
editor->set_attribute("semantic", grove::code_spans_text(lines, spans));
editor->set_attribute("problems", grove::code_problems_text(lines, problems));
```

A span or problem is a line, a byte column, a length (all from 0) and what it is; `lines` is the text the compiler saw, one string per line. `grove::CodeOverlay` reads those back, and `grove::overlay_code_tokens()` lays one set of tokens over another. `mark` also takes `error-underline`, `warning-underline` and `error-line`.

#### Hovers and Ctrl+click

Resting the mouse on a character for a moment fires `hover` with `x()` as the line and `y()` as the column (both from 1, like `cursor`). The editor clears its `hover` attribute first; set it in the handler and a popup opens over the text, like VS Code's. Its first line is `line column length` (from 0, in bytes) for the stretch of text it's about, and the rest is what to show:

~~~text
12 4 5
```walnut
function scale(T: value, int: times) -> T
```
Scales a value.
**Value:** `6` · [color](color:#ff8800) · [next](overload:next)
---
Defined in [geo.wal:3](open:2:4:/home/me/geo.wal)
~~~

A fence starts a block of code colored like the editor's own (name the language after the backticks to pick another), `---` draws a line, `**bold**` and `` `code` `` work inside text, and `[label](action)` is a link: clicking it fires `hover_action` with the action in `key()`. An action starting with `color:` draws a swatch of that color instead of a label. The popup stays while the mouse is on the text or inside the popup, and goes away when the mouse leaves, a key is pressed or the editor scrolls. `code-hover` in Bark sets its background.

Ctrl+click (Cmd+click on a Mac) fires `goto` with the same `x()` and `y()`, for going to a definition. Holding Ctrl over one of the editor's `links` (or any word, with `link-words="true"`) underlines it and turns the mouse into a pointer.

In Bark and Grove, every color written as `#rrggbb`, `rgb()`, `rgba()`, `hsl()` or `hsla()` after a `:` or `=` gets a small swatch beside it. Clicking the swatch opens the same picker as `input_color`, and picking a color rewrites the text in the format it was written in (or the one clicked in the picker); Escape puts the old text back.

#### Autocomplete

Typing a word in an `input_code` opens a list of suggestions under the caret, like VS Code's. Up and Down (or Page Up and Page Down) pick one, **Tab** puts it in, and **Escape** closes the list; it stays closed until you start typing another word. Ctrl+Space opens it whenever you like, clicking a suggestion puts it in, and any other key (an arrow, a space, punctuation) closes it. Suggestions match what you typed at the start (`vec` → `vector`), at word starts inside the name (`pb` → `push_back`, `gvo` → `getValueOr`) or anywhere in order, best matches first, and the matched letters are drawn in the accent color.

Each time the word changes the editor fires `complete` on itself: `x()` and `y()` are the caret's line and column (from 1), `key()` is the word typed so far, `text()` is what was just typed (empty for Ctrl+Space and Backspace) and `button()` is 1 when the user asked with Ctrl+Space. Answer by setting the `completions` attribute in the handler:

```
line column
label<TAB>kind<TAB>detail<TAB>insert<TAB>documentation<TAB>preview
...
```

The first line is where the text being completed starts (from 0, in bytes; usually the start of the word, or the caret after a trigger like `.`). Each suggestion has a `label` that's shown and matched; a `kind` (`keyword`, `function`, `method`, `variable`, `field`, `property`, `type`, `class`, `namespace`, `constant`, `tag`, `attribute`, `snippet`, `word`, ...) picks its badge and color; `detail` is shown on the right (a type or signature); `insert` is put in instead of the label, with `$0` marking where the caret goes; `documentation` is shown under the list for the highlighted suggestion; and an optional sixth field, `preview`, is drawn bigger in its own column right after the name, for a character or symbol the suggestion stands for (kind `character` gives it an Ω badge). Write `\t`, `\n` and `\\` for tabs, newlines and backslashes inside a field. C++ can build and read the attribute with `grove::completions_text()` and `grove::parse_completions()` from `grove/input/completion.hpp`, which also has the matcher (`grove::filter_completions`).

The answer can also come later, for suggestions that take a while to work out (asking a compiler, say): set `completions` again whenever they're ready and the list updates, or opens if it was empty, as long as the caret is still in the same word.

Without an answer, Grove suggests the language's keywords and the words already in the document (turn that off with `word-completions="false"`). When a suggestion goes in, the editor fires `completion` with `key()` the label and `text()` what was inserted.

### Dropdowns

`<input_select>` shows the chosen `<option>` with a ▾, and clicking it opens a list under it (or over it, near the bottom of the window). A long list scrolls with the wheel.

```html
<input_select id="font" value="mono">
    <option value="mono">Monospace</option>
    <option value="serif">Serif</option>
    <option value="fira" disabled>Fira Code (not installed)</option>
</input_select>
```

Without a `width`, a dropdown is as wide as its longest option (within `min-width` and `max-width`, and never wider than the space it's in), so the chosen text always fits; changing the options resizes it. With a `width`, text that doesn't fit ends in "…" instead of spilling out.

An option's value is its `value` attribute, or its text when it has none. `value` on the `<input_select>` picks the option; without it the first option is chosen, unless there's a `placeholder`, which is shown until something is picked. Picking an option sets `value` and fires `input` and `change`, so `value_string()` (and `grove::input_string()` in C++) reads it like a string input.

Keys: while it's focused, Up and Down choose the previous or next option straight away, and Enter or Space opens the list. In the list, Up, Down, Home, End, Page Up and Page Down move, Enter or Space picks, and Escape closes it. Options with `disabled` are greyed out and skipped.

C++ can read the options with `grove::select_options(node)` (value, label and disabled for each), find the chosen one with `grove::select_index(node)`, and replace them all with `grove::set_select_options(document, node, options)`. `grove check` warns about a `value` that isn't one of the options, a dropdown with no options, and anything but `<option>` inside one.

`grove::runtime::font_families(kind)` lists the font names Grove knows (`"monospace"`, `"sans-serif"`, `"serif"`, `"display"`, `"handwriting"`, or `"all"`), which is handy for filling a font dropdown.

## File picker

`<input_file>` looks like a button. Clicking it (or Enter or Space while it's focused) opens the system's file dialog: the native one on Windows, and zenity or kdialog on Linux (set `FIZMO_DIALOG=zenity` or `kdialog` to choose). The window keeps running while the dialog is open. Picking something sets the value and fires `input` and `change`; closing the dialog fires `cancel`.

```html
<input_file id="project" mode="folder">Open folder...</input_file>
<input_file id="images" filter="Images: *.png *.jpg; All files: *" multiple />
<input_file id="export" mode="save" title="Export as" start="/home/me/exports" />
```

| Attribute | |
|---|---|
| `mode` | `open` (default), `save` or `folder` |
| `multiple` | with `open`: pick several files |
| `filter` | `Name: *.a *.b; Other: *.c`, or just `*.a *.b` |
| `title` | the dialog's title |
| `start` | the folder the dialog starts in (default: the current value) |
| `hide-value` | don't show the chosen file's name next to the label |
| `placeholder` | shown next to the label until something is picked |

With no label, it says "Choose file...", "Choose files...", "Choose folder..." or "Save as...". A script can also ask directly: `grove::choose_file("folder", "Pick a folder", "")` (C: `grove_file_dialog`) waits for the dialog and returns the path, or `""` when cancelled.

## Terminals

`<terminal>` is a terminal emulator: a grid of character cells that understands what programs write to a terminal (xterm's escape sequences), so shells, `vim`, `htop`, `less` and colored output look and behave as they do in any terminal. Grove draws it and handles the keyboard and mouse; the app connects it to a program, usually through a pseudo-terminal.

```grove
<terminal id="shell" rows="24"></terminal>
```

It fills the space Bark gives it (`flex: 1` or a `height`; without either it's `rows` tall, 24 by default) and works out how many columns and rows fit in its font. `color`, `background` and `caret-color` are the default text, background and cursor colors, `accent-color` tints the selection, and `font-family` and `font-size` pick the font (a monospace one by default). Line and block drawing characters are drawn as shapes, so borders join up whatever the font.

From C++, `grove::terminal_of(document, element)` (in `grove/terminal.hpp`) gives the element's `grove::Terminal`:

```cpp
std::shared_ptr<grove::Terminal> term = grove::terminal_of(document, *element);
term->set_input_handler([&](const std::string& bytes) { write(pty, bytes.data(), bytes.size()); });
term->feed(output_from_the_program);
write(pty, term->take_reply());
if (term->columns() != cols || term->rows() != rows) resize_pty(term->columns(), term->rows());
```

| Call | Does |
|---|---|
| `feed(bytes)` | interpret UTF-8 output from the program |
| `take_reply()` | answers to the program's questions (cursor position, device attributes, colors); write them back to it |
| `set_input_handler(fn)` | `fn` gets the bytes to send to the program: keys, pasted text, mouse reports |
| `columns()`, `rows()` | the current size; changes when the element is resized, so tell the program (`TIOCSWINSZ`, `ResizePseudoConsole`) |
| `title()`, `working_directory()` | set by the program with OSC 0/2 and OSC 7 |
| `take_changed()`, `take_title_changed()`, `take_bell()` | whether something happened since the last call |
| `set_palette(i, rgb)`, `set_scrollback_limit(n)`, `clear_scrollback()`, `reset()` | |
| `screen_text()`, `row_text(r)`, `line(r)`, `selected_text()` | read what's on screen |

What it supports: the VT100/VT220 and xterm control sequences (cursor movement, erasing, inserting and deleting characters and lines, scroll regions, origin mode, tab stops, saved cursors, character sets including DEC line drawing, the alternate screen), 16, 256 and 24-bit colors with bold, dim, italic, underline (single, double, curly), blink, inverse, hidden, strikethrough and overline, wide and combining characters, xterm's key encoding (with application cursor and keypad modes and modifier combinations), bracketed paste, focus reports, and mouse reporting (X10, normal, button and any motion, in the default, UTF-8, SGR and urxvt encodings). It answers DSR, DA1, DA2, DA3, DECRQM, DECRQSS, DECREQTPARM, window-size reports and OSC 4/10/11/12 color queries. 132-column mode clears the screen as DEC terminals do but keeps the width.

A tab character in the output moves to the next tab stop, every 8 columns like other terminals; `tab-size="4"` on the `<terminal>` (or `Terminal::set_tab_width` from C++) puts them closer. Programs can still set their own stops.

With the keyboard focused on it, keys go to the program. Ctrl+Shift+C copies the selection (so does Ctrl+C while something is selected, unless the element has `copy-on-ctrl-c="false"`, which sends Ctrl+C to the program like a plain terminal), Ctrl+Shift+V or Shift+Insert pastes, Shift+PageUp and Shift+PageDown scroll back, and Ctrl+Shift+Home and End go to the top and bottom of the scrollback. Drag to select (Alt+drag for a block), double-click for a word, Shift+click to extend; middle-click pastes. When the program asks for the mouse, clicks and the wheel go to it instead; hold Shift to select anyway. The wheel scrolls back through output, or moves through the alternate screen with arrow keys (as `less` and `man` expect).

`key_down` still fires on the element first, so an app can take a key for itself with `prevent_default()`.

## Canvas

`<canvas>` shows pixels that the app hosting the document draws itself: a plot, a game view, an image being edited. It fills the space Bark gives it (`width`, `height`, `flex`); without a height it's `height` pixels tall from its attribute, or 60% of its width.

The host sets its pixels from C++ with `#include <grove/canvas.hpp>`:

```cpp
grove::Node* plot = project.document.find_by_id("plot");
std::vector<std::uint8_t> rgba(width * height * 4);
grove::set_canvas_pixels(project.document, *plot, width, height, std::move(rgba));
```

- `set_canvas_pixels(document, canvas, width, height, rgba)` takes 8-bit RGBA rows, top to bottom, as a `std::vector` (moved in) or a pointer. Grove uploads them once and draws them stretched to the canvas's box, so draw at the size `canvas_size` reports for sharp pixels.
- `canvas_rect(document, canvas)` is where the canvas is in its window after the last frame, in the same coordinates as mouse events (`x`, `y`, `width`, `height` and `shown`), so `e.x - rect.x` is the mouse's position inside it. `canvas_size(document, canvas, w, h)` gives the size rounded to whole pixels.
- `canvas_resize` is sent to the canvas (it doesn't bubble) whenever its size changes, with the new size in `x` and `y`. Draw again then.
- Mouse events (`mouse_down`, `mouse_move`, `mouse_up`, `wheel`, `double_click`, `click`) reach the canvas like any element, and `tabindex="0"` lets it take keys.

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
<script lang="c++" src="stats.cpp"></script>
<script lang="c" entry="start">
    #include <grove/api/grove_api.h>
    void start(void) { grove_set_text_by_id("greeting", "Hello from C"); }
</script>
```

- `lang` is `walnut` (the default), `walh`, `c++`/`cpp`, `hpp`, `c` or `h`. Without `lang`, the language comes from the `src` extension.
- `grove run` and `grove exec` compile every script into a module and run them in document order, once the document has loaded and before the `load` event. Walnut and C++ scripts run their top-level code (C++ through `GROVE_ON_LOAD`); a C or C++ script with `entry="name"` also gets `name()` called.
- Header scripts (`walh`, `hpp`, `h`) aren't run. Their folders are added to every other script's include path.
- Built modules are cached in `.grove-cache/` next to the document, so only changed scripts are compiled again. `--rebuild` ignores the cache, `-v` prints every compiler command, and `--tools` shows where Grove found `walnut`, the compilers and its headers.
- A compile error points at the real line in the `.grove` (or `.wal`, `.cpp`, `.c`) file.

Grove looks for Walnut in `$GROVE_WALNUT`, next to `grove`, then on `PATH`, and for compilers in `$CXX` and `$CC`. Without `$CXX`, Grove uses the compiler Walnut uses (`walnut init --print-compiler`, so a compiler set up with `walnut init` works for every script), and `c++` and `cc` when Walnut doesn't name one. Grove only asks Walnut when a script actually needs compiling, so starting from the cache costs nothing. Set `GROVE_INCLUDE_DIR` if Grove can't find its own headers and `GROVE_CACHE_DIR` to keep the cache somewhere else.

### The document, like JavaScript

Every language gets the same calls. Walnut uses `import module.grove`, C++ uses `#include <grove/dom.hpp>`, and C uses `#include <grove/api/grove_api.h>`.

| Walnut and C++ | C | |
|---|---|---|
| `get_by_id("x")`, also `id_selector` | `grove_find_by_id` | the element with that id |
| `get_by_class("c")`, also `class_selector` | `grove_query_all(doc, NULL, ".c", ...)` | every element with that class |
| `get_by_tag("p")`, also `tag_selector` | `grove_query_all(doc, NULL, "p", ...)` | every element with that tag |
| `query("row.task > button")` | `grove_query` | the first match for any Bark selector |
| `query_all("li:first-child")` | `grove_query_all` | every match |
| `create_element("button")` | `grove_create_element` | a new element, not in the document yet |
| `set_timeout(ms, fn)`, `set_interval(ms, fn)`, `clear_timer(id)` | `grove_set_timeout`, ... | timers |
| `dispatch("my_event")` | `grove_dispatch_event` | fire an event on the document |
| `time_ms()`, `width()`, `height()`, `quit()`, `log(s)` | `grove_time_ms`, ... | |

An `Element` has:

- `tag()`, `id()`, `get_attribute`, `set_attribute`, `has_attribute`, `remove_attribute`
- `inner_text()`, `set_inner_text(s)` (C++ also has `text()` and `set_text(s)`)
- `add_class`, `remove_class`, `toggle_class`, `has_class`
- `style(p)`, `set_style(p, v)`, `remove_style(p)` for its `style="..."`, and `computed_style(p)` for the value after the cascade
- typed reads of any property after the cascade, including your own [custom properties](bark.md#custom-properties) like `--glow`: `computed_number(p)` (or `computed_number(p, fallback)`; lengths in pixels, `true`/`false` as 1 and 0), `computed_number_at(p, i, fallback)` for the `i`th value, `computed_color(p)` as `#rrggbbaa` (or `""`), `computed_bool(p)` and `has_computed_style(p)`. In C: `grove_computed_number`, `grove_computed_number_at`, `grove_computed_color` and `grove_has_computed_style`
- `parent()`, `children()`, `child(i)`, `child_count()`, `next_sibling()`, `previous_sibling()`, `append_child`, `insert_before`, `remove()`, `is_attached()`
- `query` and `query_all` inside it
- `on(type, fn)`, `add_event_listener(type, fn)`, `remove_event_listener(id)`, `dispatch(type)`
- `focus()`, `hovered()`, `focused()`, `active()`
- for inputs: `input_kind()`, `value_integer()`, `value_float()`, `value_bool()`, `value_string()`, `value_color()` and the matching `set_value_*`

A removed element keeps working, so it can be put back later. An `ElementList` has `size()`, `empty()`, `at(i)`, `first()` and `last()`.

```walnut
import module.grove

grove::Element: list = grove::get_by_id("tasks");

grove::get_by_id("add").on("click", [&](grove::Event&: e) -> void {
    grove::Element: item = grove::create_element("p");
    item.set_inner_text("New task");
    list.append_child(item);
});

grove::set_interval(1000.0, [&]() -> void {
    grove::log("still here");
});
```

```cpp
#include <grove/dom.hpp>

GROVE_ON_LOAD(setup) {
    grove::document.on("load", [](grove::Event&) {
        grove::document.get_by_id("stats").set_text("Ready");
    });
}
```

### Calling into the app that hosts the document

An app that embeds Grove (a C++ program that loads a document and calls `grove::runtime::run`) can give its scripts functions of its own. They take and return strings:

```cpp
#include <grove/grove.hpp>
#include <grove/runtime/runtime.hpp>

grove::Project project = grove::load_project("ui.grove");
grove::register_native(project.document, "app.save", [&](const std::vector<std::string>& args) {
    return save(args.at(0)) ? std::string("ok") : std::string();
});
grove::runtime::run(project);
```

```walnut
if (grove::has_native("app.save")) then {
    string: result = grove::call("app.save", "notes.txt");
}
```

The host can also use the clipboard: `grove::set_clipboard_text(document, text)` and `grove::clipboard_text(document)` (both from `grove/native.hpp`) work while the document is running in a window.

C++ scripts call `grove::document.call("app.save", "notes.txt")` with any number of string arguments (or `call_with(name, vector)`), and C scripts call `grove_call_native(doc, "app.save", args, count)`, which returns `NULL` when there's no such function. A function that throws reports a script error and returns `""`. `grove::has_native` and `grove::native_names` list what's there.

Scripts can also read Bark files of their own and run them as steps, with their own properties, prompts and `{placeholders}`: see [Bark beyond styles](bark.md#bark-beyond-styles-values-and-actions).

### Events

Events go to the element under the mouse (or the focused element for keys), then bubble up through its parents to the document. `e.stop_propagation()` stops the bubbling, and `e.prevent_default()` stops Grove's own handling, like scrolling for `wheel` or typing for `key_down`.

| Event | Fired when | Useful fields |
|---|---|---|
| `click`, `double_click` | pressed and released on the same element (not on disabled ones) | `x`, `y`, `button` |
| `mouse_down`, `mouse_up`, `mouse_move` | the mouse is pressed, released or moved | `x`, `y`, `button` |
| `mouse_enter`, `mouse_leave` | the mouse moves onto or off an element (doesn't bubble) | |
| `wheel` | scrolling | `delta_x`, `delta_y` |
| `drop` | files or folders are dragged in from outside the program (a file manager, the desktop) and dropped; it goes to the element they were dropped on | `x`, `y`, `paths` in C++; `text()` is the paths, one per line |
| `key_down`, `key_up` | a key is pressed or released; `key` is a name like `A`, `Enter`, `Delete`, `F2` or `UpArrow`, never a control character. While an `input_string` is being typed in, its value is the text typed so far during `key_down`, and a handler that sets the value replaces what's in the field | `key`, `shift()`, `ctrl()`, `alt()`, `meta()` |
| `text` | text is typed | `text` |
| `focus`, `blur` | an element gains or loses focus (doesn't bubble) | |
| `input` | an input's value changes while editing. A typed field's value stays the last committed one until Enter or leaving it, so in C++ `edit_text` holds the text typed so far (with `has_edit_text` set) for handlers that react as you type, like a search box | `edit_text`, `has_edit_text` |
| `change` | an input's value is committed (Enter, a click, leaving the field, picking a file) | |
| `cursor` | the caret of an `input_code` moves | `x()` is the line, `y()` the column, both from 1 |
| `find` | an `input_code`'s matches for its `find` text or its current match change | `x()` is the current match from 1 (0 when none is selected), `y()` how many there are; `key()` is `bad-pattern` for a regular expression that doesn't parse |
| `replace` | Replace All finished in an `input_code` | `x()` is how many were replaced |
| `find-seed` | an `input_code` answers `find-command="seed"` | `text()` is its selection if that's on one line, otherwise empty |
| `hover` | the mouse rests on a character of an `input_code` | `x()` is the line, `y()` the column; answer by setting its `hover` attribute |
| `goto` | Ctrl+click in an `input_code` | `x()` is the line, `y()` the column |
| `hover_action` | a link in an `input_code`'s hover popup is clicked | `key()` is the link's action |
| `complete` | the word being typed in an `input_code` changes, or Ctrl+Space | `x()`, `y()`, `key()` the word so far; answer by setting its `completions` attribute |
| `completion` | a suggestion is put in | `key()` the label, `text()` the inserted text |
| `resize` | a window changes size; for a `<window>` it goes to that `<window>` and bubbles | `width()`, `height()`, or `window_width()` on the target |
| `load` | after the scripts have run | |
| `open` | a `<window>` opens; the target is the `<window>` | |
| `close` | a window closes; `prevent_default()` keeps it open when the user closed it | |
| `window_focus`, `window_blur` | a window gains or loses the keyboard | |
| `page_show`, `page_hide` | a page starts or stops being the one showing | |
| `open`, `close` | a modal opens or closes | |
| `ok`, `cancel` | a popup is answered; the target is the popup. `cancel` also fires on an `input_file` whose dialog was closed without picking | |
| `menu` | an item of a [right-click menu](#right-click-menus) is picked; the target is the menu's owner (doesn't bubble) | `key` is the item's value |
| `menu_close` | a right-click menu closes without a pick (doesn't bubble) | |

Any other name is a custom event, fired with `dispatch`. Names ignore case, `_` and `-`, so `mouse_down`, `mousedown` and `MouseDown` are the same.

## Windows

```html
<body>
    <button id="open-settings">Settings...</button>
    <window id="settings" title="Settings" width="360">
        <input_bool id="dark" checked>Dark theme</input_bool>
        <button id="done">Done</button>
    </window>
</body>
```

A `<window>` is its own Fizmo window. It isn't drawn inside the window it sits in.

- **Opening and closing.** A `<window>` with the `open` attribute opens when the app starts. `open()` and `close()` from a script open and close it later; they add and remove `open`, so `is_open()` and `has_attribute("open")` always agree.
- **Size and title.** `title`, `width` and `height` attributes, or Bark's `size` and `title`, set it up like `<body>`. Without a height it grows to fit its content. Changing `width` or `height` from a script resizes it.
- **Where it opens.** A little below and to the right of its parent window.
- **Nesting.** A `<window>` inside another `<window>` belongs to that one. Closing a window closes the windows inside it too, and they come back when it opens again.

Everything in a `<window>` is part of the same document, so any script can read and change it with `get_by_id`, `query` and the rest, whichever window it's in.

**Events are told to the parent window only.** An event inside a `<window>` bubbles through its elements to the `<window>` itself, then on to the window it sits in, its parents, and the document. It never reaches a sibling window or anything inside one. So a listener on `body` hears every click in the settings window, and a listener on the settings `<window>` hears none of the clicks in `body`.

```walnut
grove::Element: settings = grove::get_by_id("settings");

grove::get_by_id("open-settings").on("click", [&](grove::Event&: e) -> void {
    settings.open();
});

settings.on("change", [&](grove::Event&: e) -> void {
    grove::log("changed " + e.target().id());
});

settings.on("close", [&](grove::Event&: e) -> void {
    e.prevent_default();
});
```

| Element method | |
|---|---|
| `open()`, `close()`, `is_open()` | show or hide a `<window>`; `close()` on the main window quits |
| `window()` | the window an element is in (`body` for the main window) |
| `window_width()`, `window_height()` | the window's size right now |
| `resize_window(w, h)`, `move_window(x, y)`, `focus_window()` | change it |

`main_window()` returns the main window's element. `close` from the user can be stopped with `prevent_default()`, which is how a window asks "save first?". Closing from a script always closes.

## Popups

Popups are small windows that ask or tell something. While one is open, the window it belongs to ignores the mouse and keyboard. Enter answers OK, Escape cancels, and closing the popup counts as Cancel.

| Call | Shows |
|---|---|
| `alert(message)` | a message with OK |
| `warning(message)`, `error(message)` | a warning or error with OK |
| `popup_info(title, message)`, `popup_warning(...)`, `popup_error(...)` | the same with your own title |
| `confirm(title, message, fn(bool))` | OK and Cancel; `fn` gets `true` for OK |
| `prompt_string(title, message, value, fn(string))` | a text field |
| `prompt_integer(title, message, value, fn(long int))` | a whole-number field |
| `prompt_float(title, message, value, fn(short float))` | a number field |
| `prompt_bool(title, message, value, fn(bool))` | a checkbox |
| `prompt_color(title, message, "#rrggbb", fn(string))` | a color field |
| `prompt_choice(title, message, "a\|b\|c", value, fn(string))` | one choice from a list |
| `prompt_slider(title, message, value, min, max, fn(short float))` | a slider |

The `prompt_*` callbacks only run for OK, and get a value of the right type: the fields use the same typing rules as the inputs, so `prompt_integer` can't come back with `"abc"`. Every call returns the popup's element, so you can also listen for `ok` and `cancel` on it, or answer it from a script with `answer(true)` or `answer(false)`. A message can span lines with `\n`.

```walnut
grove::prompt_integer("Age", "How old are you?", 30l, [&](long int: age) -> void {
    grove::get_by_id("answer").set_inner_text("Next year you'll be " + (age + 1l).to_string());
});

grove::confirm("Delete", "Delete everything?", [&](bool: yes) -> void {
    if (yes) then {
        grove::popup_error("Couldn't delete", "The folder is read-only.");
    }
});
```

A popup belongs to the window of the element whose event is running (or the main window), so its `ok` and `cancel` events go to that window. Popups are ordinary `<window popup>` elements built from `row`, `p`, the inputs and `button`, so Bark can restyle them: `window.popup`, `.popup-warning`, `.popup-icon`, `.popup-message`, `.popup-input`, `.popup-buttons`, `.popup-ok` and `.popup-cancel`.

In C, `grove_popup(document, owner, kind, title, message, input, value, options)` makes any of them (`kind` is `info`, `warning`, `error` or `question`; `input` is empty or `string`, `integer`, `float`, `bool`, `color`, `choice` or `slider`; `options` is `a|b|c` for a choice or `min|max` for numbers), and `grove_popup_answer` answers one.

## Selecting text

Text in a document can be selected with the mouse like on a web page: drag across it, double-click a word, or Shift+click to extend a selection. Ctrl+C (or Ctrl+Insert) copies it while no input has the keyboard, and Escape clears it. A click that selects text doesn't count as a click on what's under it, so a drag across a clickable list selects instead of activating an item.

`user-select: none` (inherited) keeps a part of the page from being selected, which suits toolbars, tabs and lists you drag items around in; buttons, dropdowns, checkboxes and file pickers are `none` already. `selection-color` sets the highlight (by default a translucent `accent-color`). Inputs, code editors and terminals keep their own selection.

From C++, `grove::selected_text(document)` gives the selected text, `grove::select_text(document, node)` selects everything inside an element (handy for a Select All menu item) and `grove::clear_text_selection(document)` clears it.

## Right-click menus

A host app can open a menu at the mouse, like a context menu, from C++ with `grove/windowing.hpp`:

```cpp
std::vector<grove::MenuItem> items{
    grove::menu_item("rename", "Rename\u2026", "F2"),
    grove::menu_separator(),
    grove::menu_item("delete", "Delete", "Delete", !can_delete),
};
grove::open_menu(document, *owner, items, e.x, e.y);
document.events().add_listener(owner, "menu", [](grove::Event& e) { /* e.key is "rename" or "delete" */ });
```

Each item has a `value`, a `label`, an optional `detail` drawn on the right (a shortcut, say) and can be `disabled`; `menu_separator()` draws a line. The menu opens at `x`, `y` (an event's `x` and `y` work as they are) and moves to stay inside the window. A click on an item or Enter on the highlighted one sends `menu` to the owner with `key` set to the item's value; Escape, a click outside, scrolling or another right-click closes it and sends `menu_close`. Up and Down move the highlight. `open_menu` returns `false` when nothing is drawing the document (`grove dump`, tests), and `close_menu(document)` closes an open one.

## Performance

Grove keeps the work for a frame proportional to what changed and what is on screen, not to the size of the document:

- Every element remembers when it, or anything inside it, last changed. Setting an attribute, text, a class, a child or a hover/focus state through any Grove API records the change, so an app never has to tell Grove what to redraw. Setting something to the value it already has isn't a change.
- Styles are computed once per element and kept until the element, an ancestor or the stylesheet changes. Rules are indexed by their last tag, id or class, so an element is only matched against rules that could apply to it.
- Laid-out elements are kept too. An element that didn't change is placed again from the last frame instead of being measured and wrapped again, and elements scrolled out of view are skipped. Long lists, documents and logs cost about the same per frame as short ones, even when one big element is only partly on screen: only the parts inside the view are drawn.
- Replacing an element's text with `set_inner_text` updates its text in place, and the same text again isn't a change. Creating, moving and removing elements costs the same however many elements the document has made before.
- Elements that change every frame (a clock, a live value) are laid out directly instead of being cached, and elements off screen that keep changing are laid out once they scroll into view, so the content you're looking at doesn't jump.
- Draws are grouped so shapes and text that don't overlap go to the GPU together, and clips that don't cut anything off are dropped. A screen of text, buttons and inputs is usually under ten batches.
- When nothing has changed and nothing is moving, Grove redraws the window only twice a second.
- Code editors only read the editor's text and attributes when the element changed, and re-highlight only the lines an edit touched.

`GROVE_FRAME_STATS=1` prints, every two seconds, how many frames were drawn and skipped, the average time spent laying out, drawing and presenting, the worst frame, the number of draw operations and GPU batches, and how many elements were placed from the cache, skipped off screen or laid out again. `GROVE_LAYOUT_CACHE=0` and `GROVE_BATCHING=0` turn the layout cache and draw grouping off, to rule them out when something looks wrong.

## Making one executable

```sh
grove build todo.grove -o todo
./todo
```

`grove build` compiles the scripts and writes a single program holding the document, its Bark files and its compiled scripts. It runs without Grove, Walnut, a compiler or the source files. Scripts are unpacked into a per-user cache (`~/.cache/grove/apps` or `%LOCALAPPDATA%\Grove\apps`) the first time it runs. On Linux the scripts are linked with `-static-libstdc++` so they don't depend on the compiler's C++ library, when the compiler has a static C++ library to link (Fedora and others need `libstdc++-static` installed); without one they're linked normally. From C++, `AppBuildOptions::portable = false` asks for a normal link.

## Android

Start the document with `<!GROVE type=android>` and `grove build` makes an Android app (an `.apk`) instead of a desktop program. The same `.grove`, `.bark` and script files are used; `grove build --target desktop` still makes the desktop program from an Android document, and `--target android` makes an APK from any document.

```html
<!GROVE type=android package=com.family.recipes label="Recipe Box" version=1.2 version-code=3 icon=icon.png orientation=portrait>
<grove>
    ...
</grove>
```

| Key | |
|---|---|
| `type` | `desktop` (the default) or `android` |
| `package` | the app's Android id, like `com.example.app`; it can't change once the app is installed somewhere. Defaults to `org.grove.<file name>` |
| `label` | the name under the app's icon; defaults to `<title>`, then the file name |
| `version`, `version-code` | the version people see (`1.2`) and the whole number Android compares to decide if an APK is newer (`3`); default `1.0` and `1` |
| `icon` | a PNG for the launcher icon, relative to the document (512×512 or 192×192 works well) |
| `orientation` | `portrait`, `landscape`, `sensor`/`any` (follows the phone) or `locked`; by default the app follows the phone |
| `min-sdk`, `target-sdk` | the oldest Android the app installs on (26, Android 8.0, at the least) and the one it's written for (default 35) |
| `abi` | the processors to build for: `arm64-v8a` (phones, the default), `armeabi-v7a` (old phones), `x86_64` (the emulator, Chromebooks) |
| `fullscreen` | hide the status and navigation bars |
| `keep-screen-on` | stop the screen from turning off while the app is open |

`grove check` reports keys it doesn't know, values that can't work and elements that do nothing on a phone (`<terminal>`, `<input_file>`). `grove run` on a computer opens an Android document in a phone-sized window (412×892), so you can try the layout before building.

### What changes on a phone

- `<body>` fills the screen inside the status bar, the navigation bar, any camera cutout and the on-screen keyboard, so nothing is hidden behind them; when the keyboard opens, the page shrinks to the space above it.
- Bark `px` are Android's density-independent pixels: `font-size: 16px` is the same size on every phone, and `zoom` multiplies on top. `grove::window_info` reports the phone's scale in `dpi_scale`.
- A tap is a click, a double tap a double click, and a long press (it buzzes) a right-click, so text fields get the Cut / Copy / Paste menu. Dragging a finger scrolls whatever is under it, with a fling when you let go; touching and holding before dragging drags instead (sliders, selecting text). A mouse, trackpad or keyboard plugged in or on a Chromebook works as on a computer.
- Typing into a text field opens the keyboard that suits it: numbers for `input_integer` and `input_float`, a multi-line keyboard for `input_code` and `multiline` fields, and the email, URL, phone or password keyboard for `type="email"`, `"url"`, `"tel"` or `"password"`. Gesture typing and autocorrect work.
- Every `<window>` opens as a full screen of its own on top of the app; popups (`alert`, `confirm`, the `prompt_*` family) are cards over a dimmed screen.
- The Back button closes, in order: an open menu or modal, the top popup or window (a `<window>` gets its `close` event and can refuse), the last `<page>` it went to, and finally puts the app in the background like any Android app.
- Fonts come from the phone (Roboto and the Noto fonts for every script and emoji); a family the phone doesn't have falls back to `sans-serif`, `serif` or `monospace`.
- The clipboard is Android's.
- Not there yet: `<input_file>` (the file picker) and `<terminal>` (there's no shell to run).

### Building the APK

```sh
grove build recipes.grove                 # recipes.apk next to the document
grove build recipes.grove --install       # and put it on the phone plugged in over USB
grove build recipes.grove --install --launch
```

Building needs three things from Google on the computer, which Android Studio installs (or `sdkmanager "ndk;27.2.12479018" "build-tools;35.0.0" "platforms;android-35" "platform-tools"` from the command-line tools):

- the **Android NDK**, which compiles the scripts for the phone's processor. Grove finds it through `--ndk`, `ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT` or the newest one in the SDK's `ndk` folder;
- the **SDK build tools** (`aapt2`, `zipalign`, `apksigner`) and one **platform** (`android.jar`), found through `--sdk`, `ANDROID_HOME` or `ANDROID_SDK_ROOT`, or the usual `~/Android/Sdk`, `~/Library/Android/sdk` or `%LOCALAPPDATA%\Android\Sdk`;
- **Java** (`keytool`), which comes with Android Studio, to make the signing key the first time.

It also needs the **Grove Android kit**: Grove's runtime and Fizmo built for each processor. Releases have it as `Grove-<version>-android-kit.zip`; unzip it into the Grove folder so it sits at `lib/grove/android/<abi>/`, or point `--android-kit` or `GROVE_ANDROID_KIT` at the folder holding the `<abi>` folders. To build it yourself:

```sh
cmake -S . -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DFIZMO_TUNE=OFF
cmake --build build-android --target grove_android_kit
```

That downloads and builds FreeType, HarfBuzz, libjpeg-turbo and OpenSSL for the phone too (OpenSSL needs `perl` and `make`), and leaves the kit in `build-android/kit/lib/grove/android/arm64-v8a`.

`grove build` compiles each script once per `abi`, writes the manifest, packs the document, its Bark files, images and the compiled scripts into the APK, aligns it and signs it. Without `--keystore` it signs with the standard debug key in `~/.android/debug.keystore` (making it if it isn't there); that's fine for putting the app on your own phones. To publish an app, make a key of your own once and keep it safe, since every update has to be signed with the same key:

```sh
keytool -genkeypair -keystore recipes.keystore -alias recipes -keyalg RSA -keysize 2048 -validity 10000
grove build recipes.grove --keystore recipes.keystore --key-alias recipes --ks-pass env:RECIPES_KEY_PASS
```

| Flag | |
|---|---|
| `--target android` / `--target desktop` | build for the other target than the document says |
| `--abi arm64-v8a,x86_64` | processors to build for |
| `--android-kit <dir>`, `--ndk <dir>`, `--sdk <dir>` | where to find the kit, NDK and SDK |
| `--keystore <file>`, `--key-alias <name>`, `--ks-pass <pass>`, `--key-pass <pass>` | sign with your own key; a pass can be `pass:...`, `env:VAR` or `file:path` |
| `--install`, `--launch` | install the APK on the phone over `adb`, then start it |

To put the app on a phone without `--install`, send the `.apk` to it (a cable, email, a shared folder) and open it there; Android asks once to allow installing apps from that source. `adb logcat -s grove fizmo` shows what the app prints, including script errors.
