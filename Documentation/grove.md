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
| `panel`, `div`, `row`, `column`, `list`, `item`, `pre` | blocks; `pre` keeps its spaces and line breaks |
| `h1`–`h4`, `p` | headings and paragraphs |
| `text`, `span`, `b`, `i`, `u`, `code`, `link` | inline text |
| `input_integer`, `input_float`, `input_string`, `input_bool`, `input_radio`, `input_slider`, `input_color` | typed inputs, see below |
| `input_code` | a code editor, see [Code editor](#code-editor) |
| `input_file` | a button that opens the system's file or folder picker, see [File picker](#file-picker) |
| `button`, `label` | controls |
| `img`, `br`, `hr` | images and breaks |
| `math` | MathText, see below |
| `style` | Bark, inline or `src="file.bark"` |
| `script` | Walnut, C++ or C, inline or `src="..."` |

`grove run` lays out blocks, headings, paragraphs, wrapped text with inline elements, math, buttons and every input; `img` parses and styles but doesn't draw yet. Bark's [Layout](bark.md#layout) section covers widths, `flex`, scrolling and wrapping.

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
| `<input_code>` | `std::string` / `const char*` / `string` | everything; see [Code editor](#code-editor) |
| `<input_file>` | `std::string` / `const char*` / `string`, the chosen path (several are separated by `\n`) | nothing; clicking opens the system picker |

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

### Code editor

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
| `language` | `c`, `cpp`, `walnut`, `grove`, `bark` or `plain` (default); file extensions like `hpp` or `wal` work too |
| `tab-size` | columns per tab (default 4) |
| `indent` | `spaces` (default) or `tabs`: what Tab and Enter insert |
| `line-numbers` | `false` hides them |
| `readonly` | can be selected and copied but not changed |
| `placeholder` | shown while it's empty |
| `caret` | `line:column` (1-based) moves the caret there and scrolls to it whenever the attribute changes; anything after a `#` is ignored, so `"12:1#2"` moves there again |

Keys: arrows, Home (to the indent, then the line start), End, Page Up/Down, Ctrl+Home/End, Ctrl+Left/Right by word, Shift with any of them to select; Backspace and Delete (Ctrl for a whole word; Backspace between `()`, `[]`, `{}`, `""` or `''` removes both); Enter keeps the indent and indents after `{`, `(` or `[`; Tab and Shift+Tab indent and outdent (every selected line when the selection spans lines); Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V (with nothing selected, copy and cut take the whole line); Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z undo and redo. Click to move the caret, drag or Shift+click to select, double-click selects a word, and the wheel scrolls (Shift+wheel sideways). `input` fires on every edit, `change` when it loses focus with different text, and `cursor` whenever the caret moves, with `x()` as the line and `y()` as the column. Listen for `key_down` and call `prevent_default()` to take a key for yourself. Its colors follow `color`, `background`, `border-color` and `accent-color` (caret, selection and focus).

### File picker

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

Grove looks for Walnut in `$GROVE_WALNUT`, next to `grove`, then on `PATH`, and for compilers in `$CXX` and `$CC` (default `c++` and `cc`). Set `GROVE_INCLUDE_DIR` if Grove can't find its own headers and `GROVE_CACHE_DIR` to keep the cache somewhere else.

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

C++ scripts call `grove::document.call("app.save", "notes.txt")` with any number of string arguments (or `call_with(name, vector)`), and C scripts call `grove_call_native(doc, "app.save", args, count)`, which returns `NULL` when there's no such function. A function that throws reports a script error and returns `""`. `grove::has_native` and `grove::native_names` list what's there.

### Events

Events go to the element under the mouse (or the focused element for keys), then bubble up through its parents to the document. `e.stop_propagation()` stops the bubbling, and `e.prevent_default()` stops Grove's own handling, like scrolling for `wheel` or typing for `key_down`.

| Event | Fired when | Useful fields |
|---|---|---|
| `click`, `double_click` | pressed and released on the same element (not on disabled ones) | `x`, `y`, `button` |
| `mouse_down`, `mouse_up`, `mouse_move` | the mouse is pressed, released or moved | `x`, `y`, `button` |
| `mouse_enter`, `mouse_leave` | the mouse moves onto or off an element (doesn't bubble) | |
| `wheel` | scrolling | `delta_x`, `delta_y` |
| `key_down`, `key_up` | a key is pressed or released | `key`, `shift()`, `ctrl()`, `alt()`, `meta()` |
| `text` | text is typed | `text` |
| `focus`, `blur` | an element gains or loses focus (doesn't bubble) | |
| `input` | an input's value changes while editing | |
| `change` | an input's value is committed (Enter, a click, leaving the field, picking a file) | |
| `cursor` | the caret of an `input_code` moves | `x()` is the line, `y()` the column, both from 1 |
| `resize` | a window changes size; for a `<window>` it goes to that `<window>` and bubbles | `width()`, `height()`, or `window_width()` on the target |
| `load` | after the scripts have run | |
| `open` | a `<window>` opens; the target is the `<window>` | |
| `close` | a window closes; `prevent_default()` keeps it open when the user closed it | |
| `window_focus`, `window_blur` | a window gains or loses the keyboard | |
| `ok`, `cancel` | a popup is answered; the target is the popup. `cancel` also fires on an `input_file` whose dialog was closed without picking | |

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

## Making one executable

```sh
grove build todo.grove -o todo
./todo
```

`grove build` compiles the scripts and writes a single program holding the document, its Bark files and its compiled scripts. It runs without Grove, Walnut, a compiler or the source files. Scripts are unpacked into a per-user cache (`~/.cache/grove/apps` or `%LOCALAPPDATA%\Grove\apps`) the first time it runs. On Linux the scripts are linked with `-static-libstdc++` so they don't depend on the compiler's C++ library.
