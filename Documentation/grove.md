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

`<!grove>` and the `<grove>` root are optional; a file can start straight at `<body>`.

`<body>` is the app's window. Its `title`, `width` and `height` attributes, or Bark's `size` and `title` on `body`, set up the window; without them the title comes from `<title>` and the size defaults to 800×600. A document with no `<body>` draws everything in one window.

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
| `window` | an extra window, set up like `<body>`; `grove run --window id` opens it instead of `<body>`. Opening several windows at once is planned; until then a `<window>` inside `<body>` is hidden |
| `panel`, `div`, `row`, `column`, `list`, `item` | blocks |
| `h1`–`h4`, `p` | headings and paragraphs |
| `text`, `span`, `b`, `i`, `u`, `code`, `link` | inline text |
| `input_integer`, `input_float`, `input_string`, `input_bool`, `input_radio`, `input_slider`, `input_color` | typed inputs, see below |
| `button`, `label` | controls |
| `img`, `br`, `hr` | images and breaks |
| `math` | MathText, see below |
| `style` | Bark, inline or `src="file.bark"` |
| `script` | Walnut, C++ or C, inline or `src="..."` |

`grove run` lays out blocks, headings, paragraphs, text, math and every input; `button` and `img` parse and style but don't draw yet.

## Inputs

Each input is its own element, named after the type of value it holds, so the type is fixed by the markup and can't drift:

| Element | Value type (C++ / C / Walnut) | Typing accepts |
|---|---|---|
| `<input_integer>` | `std::int64_t` / `int64_t` / `long int` | `0`–`9`, and a leading `-` only if `min` allows negatives |
| `<input_float>` | `double` / `double` / `short float` | digits, one `.`, a leading `-` if allowed |
| `<input_string>` | `std::string` / `const char*` / `string` | any printable text, up to `maxlength` characters |
| `<input_bool>Label</input_bool>` | `bool` | click to toggle; drawn as a checkbox or a switch |
| `<input_radio name="g" value="v">Label</input_radio>` | `bool` per option, `string` per group | click to pick; one per `name` group |
| `<input_slider>` | `double` | click or drag |
| `<input_color>` | `bark::Rgba` / `uint32_t` `0xRRGGBBAA` | `#` and hex digits |

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
| `placeholder` | integer, float, string | shown while empty |
| `checked` | bool, radio | on |
| `switch` | bool | draw it as a switch instead of a checkbox |
| `name` | radio | the group |
| `disabled` | all | greyed out and not editable |

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

`<input type="integer">` (and `float`, `string`, `bool`, `radio`, `slider`, `color`) still works and means the same as the matching `input_*` element; `checkbox`, `slider` and `color` are kept as short names for `input_bool`, `input_slider` and `input_color`.

`grove inputs doc.grove` lists every input with its type and current value.

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

For inputs:

```walnut
extern "C" {
    function grove_get_integer_by_id(string: id) -> long int;
    function grove_set_integer_by_id(string: id, long int: value) -> bool;
    function grove_get_float_by_id(string: id) -> short float;
    function grove_get_bool_by_id(string: id) -> bool;
    function grove_get_string_by_id(string: id) -> string;
    function grove_get_choice(string: group) -> string;
} end extern

long int: count = grove_get_integer_by_id("count");
grove_set_integer_by_id("count", count + 1l);
```

C and C++ get the full element-handle API (`grove_find_by_id`, `grove_child`, `grove_get_attribute`, `grove_set_text`, ...); the `*_by_id` functions work on the document the runtime has bound with `grove_bind_document`. Every change bumps the document revision and the runtime re-lays-out on the next frame.

Building scripts into the running app (Walnut to C++, then a shared module the runtime loads) is the next step; today `grove scripts` lists and validates them.
