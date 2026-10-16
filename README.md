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
    <body>
        <window id="main" title="Hello">
            <h1 id="greeting">Hello from Grove</h1>
            <math display="block">
                x = (-b \pm\ sqrt(b^2 - 4*a*c)) / (2*a)
            </math>
        </window>
    </body>
</grove>
```

```
$accent = #89b4fa;

window {
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
| `grove dump doc.grove` | the element tree |
| `grove styles doc.grove` | the merged Bark stylesheet |
| `grove computed doc.grove` | the computed style of every element |
| `grove math doc.grove` | every `<math>` block converted to brace form |
| `grove scripts doc.grove` | the scripts the document uses |
| `grove run doc.grove [--window id]` | open the document in a Fizmo window |

## Layout

```
Include/grove/
    common/     diagnostics, source cursor
    markup/     Grove markup: nodes, tag table, parser
    bark/       Bark: values, selectors, parser, cascade
    math/       <math> blocks through fizmo::mathtext
    script/     <script> collection (walnut, c++, c)
    api/        grove_api.h, the C API scripts use to touch the document
    runtime/    the Fizmo-backed window and renderer
    project.hpp loads a .grove file with all of its styles, scripts and math
Source/         implementations, same layout
Tools/grove.cpp the command-line tool
Tests/          grove_tests
Examples/hello  a small document using all of the above
Documentation/  the language references
```

See [Documentation/grove.md](Documentation/grove.md) for the markup and [Documentation/bark.md](Documentation/bark.md) for styles.
