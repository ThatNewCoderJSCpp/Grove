# Grove

Grove is an HTML-style way to build native apps on top of [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo) and [Walnut](https://github.com/ThatNewCoderJSCpp/Walnut).

| Web        | Grove                 | File       |
|------------|-----------------------|------------|
| HTML       | **Grove** markup      | `.grove`   |
| CSS        | **Bark** stylesheets  | `.bark`    |
| JavaScript | **Walnut**, C++ or C  | `.wal` `.cpp` `.c` |

A walnut grows on a tree, a grove is where the trees are, and bark is how a tree looks from the outside.

```html
<grove>
    <head>
        <style src="theme.bark" />
        <script lang="walnut" src="app.wal"></script>
    </head>
    <body title="Hello">
        <h1 id="greeting">Hello from Grove</h1>
        <math display="block">
            x = (-b \pm\ sqrt(b^2 - 4*a*c)) / (2*a)
        </math>
    </body>
</grove>
```

```
$accent = #89b4fa;

body {
    size: 900px 640px;
    background: #1e1e2e;
    padding: 32px;
}

panel.card {
    background: #313244;

    > math {
        font-size: 24px;
        text-align: center;
    }
}
```

## Building

```sh
cmake -S . -B build
cmake --build build --target grove
./build/grove check Examples/hello/hello.grove
./build/grove run Examples/hello/hello.grove
```

Fizmo (and Walnut, unless `-DGROVE_WITH_WALNUT=OFF`) are pulled with `FetchContent` at the tags in `GROVE_FIZMO_TAG` / `GROVE_WALNUT_TAG`. To build against local checkouts instead, pass `-DFETCHCONTENT_SOURCE_DIR_FIZMO=/path/to/Fizmo` and `-DFETCHCONTENT_SOURCE_DIR_WALNUT=/path/to/Walnut`.

| Option | Default | |
|---|---|---|
| `GROVE_BUILD_RUNTIME` | `ON` | `grove_runtime` and `grove run` (links `fizmo::graphics`) |
| `GROVE_WITH_WALNUT` | `ON` | fetch and build the Walnut compiler for `<script lang="walnut">` |
| `GROVE_BUILD_TESTS` | `ON` | `grove_tests` plus `ctest` entries |

Without the runtime, `grove_core` only needs Fizmo's headers (MathText is header-only), so it builds in seconds.

## The `grove` tool

| Command | |
|---|---|
| `grove check doc.grove` | parse markup, Bark, math and scripts; print every diagnostic with file:line:column |
| `grove dump doc.grove [--full]` | the element tree; `--full` prints long text and attribute values without shortening them |
| `grove styles doc.grove` | the merged Bark stylesheet |
| `grove computed doc.grove` | the computed style of every element |
| `grove math doc.grove` | every `<math>` block converted to brace form |
| `grove inputs doc.grove` | every input with its type and current value |
| `grove scripts doc.grove` | the scripts the document uses |
| `grove run doc.grove [--window id]` | build the scripts and open the document's `<body>` as a Fizmo window |
| `grove exec doc.grove [--event "click #id"] [--for ms]` | build and run the scripts without a window, then print the element tree |
| `grove build doc.grove [-o app]` | one executable with the document, its styles and its compiled scripts |
| `grove scripts doc.grove --build` | compile every script and report errors |
| `grove highlight doc.grove` | tokens, colors and diagnostics as JSON, for editors (`.bark` files too; `--stdin` reads unsaved text) |

`--viewport WxH` sets the window size the other commands use for viewport units (default 800x600). `--rebuild` compiles scripts again even if they're cached, `-v` prints every compiler command, and `--tools` shows where Grove found Walnut, the compilers and its headers.

## Scripts

Scripts work like JavaScript in a web page: they find elements, change them, listen for events and set timers. The same API is in Walnut (`import module.grove`), C++ (`#include <grove/dom.hpp>`) and C (`#include <grove/api/grove_api.h>`).

```walnut
import module.grove

grove::get_by_id("add").on("click", [&](grove::Event&: e) -> void {
    grove::Element: item = grove::create_element("p");
    item.set_inner_text("Clicked at " + e.x().to_string());
    grove::get_by_id("list").append_child(item);
});
```

Centering is one line (`align: center`, `<div center>` or `<center>`), with `justify-content`, `align-items`, `align-self`, `margin: auto` and `calc()`/`min()`/`max()`/`clamp()` underneath when you want full control. Several screens go in one document as `<page>` elements, each optionally loaded from its own file with `src`, and any element with `to="page-id"` switches to it, so tabs are a row of buttons; `<modal>` is a dialog drawn over the window. `<input_code>` is a code editor (line numbers, highlighting for C, C++, Walnut, Grove, Bark, CMake, HTML, CSS, JavaScript, Rust, Zig, Python, Lua, Java, C# and assembly with colors you set in Bark through `code-*` properties, several carets with Alt+click, selection, undo, clipboard, Ctrl+/ to comment lines) `<input_math>` is a text box drawn as math while you type (powers, subscripts, fractions, roots, Greek letters), and `<input_color>` has a color wheel picker and `<input_file>` opens the system's file or folder picker. A C++ program that hosts a document can give its scripts functions with `grove::register_native`, which scripts call with `grove::call`. Every `<window>` is a separate window whose events bubble up to the window it sits in, and `alert`, `confirm` and typed `prompt_*` popups ask the user things. Scripts are compiled the first time the document runs and cached after that, so they need Walnut (for `.wal`) and a C/C++ compiler: Grove uses the compiler Walnut uses, so `walnut init` sets one up for both. A program made with `grove build` needs neither. See [Documentation/grove.md](Documentation/grove.md#scripts) for the whole API and the list of events.

## Layout

```
Include/grove/
    common/     diagnostics, source cursor
    markup/     Grove markup: nodes, tag table, parser
    bark/       Bark: values, selectors, parser, cascade
    math/       <math> blocks through fizmo::mathtext
    script/     <script> collection, building scripts into modules, loading them
    markup/events.hpp  listeners, bubbling and timers
    dom.hpp     the C++ script API (Document, Element, Event)
    dom_tools.hpp  query and query_all with Bark selectors, classes, inline styles
    walnut/grove.walh  the Walnut script API
    bundle.hpp, app.hpp  grove build: one executable holding a document
    windowing.hpp  <window> open/close, which window an element is in, popups
    input/      typed inputs: kinds, values, typing rules, validation; code_buffer.hpp, the editing and highlighting behind <input_code>
    native.hpp  functions a host program registers for its scripts, and the file dialog hook
    api/        grove_api.h, the C API scripts use; grove_host.h and grove_module.cpp tie a script module to the program
    highlight.hpp  the JSON behind grove highlight
    runtime/    the Fizmo-backed window and renderer
    project.hpp loads a .grove file with all of its styles, scripts and math
Source/         implementations, same layout
Tools/grove.cpp the command-line tool
Tests/          grove_tests
Tools/vscode-grove  the VS Code extension
Examples/hello  a small document using markup, Bark, math and scripts
Examples/inputs every input type, sized with viewport units
Examples/todo   a to-do list with a Walnut script and a C++ script
Examples/windows  a settings window, a notes window that asks before closing, and every kind of popup
Examples/editor   wrapped text with inline elements, a flex layout, a scrolling list, input_code, input_file and pre
Tools/codegen   grove_api.spec and make_grove_api.py, which write grove_api.h and grove_api_list.h; make_grove_walh.py, which writes grove.walh
Documentation/  the language references
```

## Editor support

`Tools/vscode-grove` is a VS Code extension for `.grove` and `.bark` files: color swatches you can click to open a color picker, coloring (with Bark, MathText, Walnut, C++ and C inside a `.grove` file), unknown tags, properties and variables marked, and `grove check`'s errors underlined. It works like the Walnut extension, with its own `colors.json`, nine color themes (Neon, Dark Modern, Soft, Ember, Amber, Ocean, Forest, Mono, Light) you can switch between with a live preview, and a color designer page; see [its README](Tools/vscode-grove/README.md). `.grove` and `.bark` files get their own icons (the symbols and file-document versions are in `Tools/icons` as SVG, PNG and `.ico`). Install `Tools/vscode-grove/grove-language-0.1.2.vsix` with **Extensions → ... → Install from VSIX...**.

## Releases

Pushing a tag `vX.Y.Z` that matches `project(grove VERSION X.Y.Z)` runs `.github/workflows/release.yml`. It builds, tests and packages `grove` for Linux x86_64, arm64 and i686 (`.tar.gz`) and Windows MinGW64, UCRT64 and ARM64 (`.zip`), then attaches them to the GitHub release. Each package has `bin/grove`, the headers, `grove_core`/`grove_runtime`, the docs and the examples. Walnut isn't in the package; install it from its own release and put `walnut` on `PATH` (or next to `grove`, or in `GROVE_WALNUT`) to run Walnut scripts. Locally, `-DGROVE_PACKAGE=ON` and `cpack` make the same package.

See [Documentation/grove.md](Documentation/grove.md) for the markup and [Documentation/bark.md](Documentation/bark.md) for styles.
