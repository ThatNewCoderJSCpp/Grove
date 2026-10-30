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

## Custom properties

A property whose name starts with `--` is yours: Grove keeps it, doesn't warn about it, and lets children inherit it, the same as CSS custom properties. Unlike a `$variable`, its value can differ from element to element, follow `:hover` and classes, and be read by scripts.

```bark
body {
    --accent: #89b4fa;
    --gap: 6px;
}

.card {
    --glow: 4px;
    padding: var(--gap);
    border-color: var(--accent, white);
    margin: calc(var(--gap) * 2);
}
```

- `var(--name)` puts the value in place, from this element or the nearest parent that sets it. `var(--name, fallback)` uses the fallback when nothing does. It works on its own, inside other values and functions, and inside `calc()`, `min()`, `max()` and `clamp()`.
- Scripts read any property, custom or not, with `computed_style("--glow")` (the text), `computed_number("--glow")` (a number, with lengths in pixels: `2em` comes back as 32), `computed_color("--accent")` (`#rrggbbaa`), `computed_bool("--spin")` (`true`, `yes`, `on` or `1`) and `has_computed_style`. See [Scripts](grove.md#the-document-like-javascript).
- A property without `--` that Grove doesn't know is still kept for scripts to read, but `grove check` warns about it in case it's a typo, and it isn't inherited.

## Bark beyond styles: values and actions

Bark files can describe things to do, not just how things look. Canopy's `tasks.bark` is one: each rule is a task, and its properties are steps that run in order. `#include <grove/bark/actions.hpp>` gives any program the same building blocks, so its own Bark files can have prompts, values and steps without writing a parser.

```bark
$target = "all";

build {
    label: "Build {target}";
    ask: jobs "Parallel jobs" 8;
    run: "make {target} -j {jobs}";
    log: "built {target} with {jobs:8} jobs";
    done;
}
```

**Placeholders.** `{name}` inside a string is replaced with a value by `bark::interpolate`. `{name:fallback}` uses the fallback when there's no value, `{{` and `}}` are a literal `{` and `}`, and `${...}` (a shell variable) is left alone, as is anything that isn't a name (`{}`, `{ x }`, `awk '{print $1}'`). A name with no value is left as it was and reported, so a typo is easy to catch. Placeholders only work inside strings: a bare `{` starts a nested rule.

**A property with no value.** `done;` (a name and a `;`) is a property with no values, for actions that don't need any. Style properties still need a value.

**`bark::Values`** holds the names. `set(name, value)`, `set_default` (only when it has no value yet), `provide(name, function)` for a value that's worked out the first time it's used (reading a version from a file, say), `add_variables(sheet.variables())` to make the file's `$variables` usable as `{name}` too, and a parent `Values` to fall back on, so a program's own values can sit under each run's.

**`bark::Actions`** says what each property does. `add(name, handler)` (or with a minimum and maximum number of values) registers an action; the handler gets an `ActionCall` with the values as text, placeholders already filled in, and returns `Flow::Next`, `Wait` (for something that answers later, like a prompt), `Stop` or `Fail`. `setting(name)` registers a property that's read rather than run, like `label`. `add_standard()` adds `set: name value;`, `default: name value;`, `stop;` and `fail: "why";`. `check(rule)` lists unknown properties, wrong numbers of values and settings given twice.

**`bark::Runner`** runs a rule's actions in order with its own `Values`. `run()` goes until an action waits, stops, fails or the rule ends; after a `Wait`, `answer(name, value)` stores the answer and carries on (or `resume()` without one).

```cpp
grove::bark::Actions actions;
actions.add_standard();
actions.setting("label");
actions.add("ask", 1, 3, [&](grove::bark::ActionCall& call) {
    show_prompt(call.arg(1, call.arg(0)), call.arg(2));
    return grove::bark::Flow::Wait;
});
actions.add("run", 1, 1, [&](grove::bark::ActionCall& call) {
    return start(call.arg(0)) ? grove::bark::Flow::Next : call.fail("couldn't start " + call.arg(0));
});

grove::bark::Values program;
program.provide("version", [] { return read_version(); });
grove::bark::Runner runner(actions, *grove::bark::find_rule(sheet, "build"), &program);
runner.run();
on_prompt_answered = [&](std::string text) { runner.answer("jobs", text); };
```

`bark::text_of`, `texts_of` and `joined_text` turn values into plain text (strings without quotes, `8` as `8`), and `setting_of(rule, "label", values)` reads a setting with its placeholders filled in.

**Functions after the colon.** A property can also use values that are worked out when the step runs: `roll: dice(6);`, `say: upper("{name}");`, `run: join(" ", make, -j, cores());`. `actions.function(name, handler)` (or with a minimum and maximum number of values) registers one; the handler gets a `FunctionCall` with the values as text (placeholders filled in, and functions inside it already worked out, so `upper(join("-", a, b))` works) and returns the text to put in its place, or `call.fail("why")` to stop the rule with an error. A function nothing registered is left as it was written (`rgb(...)`, `calc(...)` and the other style functions keep working). `add_standard_functions()` adds `upper`, `lower`, `trim`, `join(separator, ...)`, `concat`, `replace(text, from, to)`, `length`, `env(NAME, fallback)` and `or(a, b, ...)` (the first one that isn't empty). `check(rule)` also reports functions given the wrong number of values, `actions.setting(rule, name, values)` reads a setting with its functions worked out, and `evaluate(value, values, error)` works out any single value.

```cpp
actions.function("dice", 1, 1, [](grove::bark::FunctionCall& call) {
    return std::to_string(1 + std::rand() % std::stoi(call.arg(0)));
});
```

### The easy way: `BarkScript`

`grove::BarkScript` puts the file, the actions, the program's values and the run together, so a new property or function is one line. It already has `add_standard()` and `add_standard_functions()`, and the file's `$variables` are usable as `{name}`.

```walnut
import module.grove

grove::BarkScript: steps = grove::BarkScript::load("steps.bark");
steps.set("app", "Grove");
steps.setting("label");
steps.on("say", [&](grove::BarkCall&: call) -> void {
    grove::log(call.rest(0));
});
steps.on_counted("ask", 1, 3, [&](grove::BarkCall&: call) -> void {
    call.wait();
});
steps.func("dice", [&](grove::BarkCall&: call) -> string {
    return "4";
});

grove::log(steps.read("greet", "label"));
steps.run("greet");
if (steps.waiting()) then {
    grove::prompt_string("Greeting", "How are you?", "fine", [&](string: answer) -> void {
        steps.answer("mood", answer);
    });
}
```

```bark
greet {
    label: upper("hello from {app}");
    ask: mood "How are you?";
    say: "{app} feels {mood} and rolled" dice(6);
}
```

- Making one: `BarkScript::load(path)`, `BarkScript::parse(source)` or `parse_named(source, path)`; `loaded()`, `ok()`, `error_count()`, `error_at(i)`.
- Properties (before the colon): `on(name, fn)` takes any number of values, `on_counted(name, min, max, fn)` checks how many, `on_raw` passes them without filling in placeholders, and `setting(name)` (or `setting_counted`) marks a property that's read, not run. An action carries on to the next step unless it calls `call.wait()`, `call.stop()` or `call.fail("why")`.
- Functions (after the colon): `func(name, fn)`, `func_counted(name, min, max, fn)` and `func_raw`; `fn` returns the text to use. `has(name)` and `remove(name)` work on both.
- Values: `set(name, value)`, `provide(name, fn)` and `get(name)` (the current run's value first, then the program's).
- Running: `run(rule)` starts a rule from the top (and returns `false` if it failed or isn't there), then `waiting()`, `answer(name, value)`, `resume()`, `stop()`, `running()`, `done()`, `stopped()`, `failed()`, `finished()`, `error()` and `current_rule()`.
- Reading: `rule_count()`, `rule_name(i)`, `has_rule(rule)`, `read(rule, setting)` (placeholders and functions worked out), and `problem_count(rule)`/`problem_at(rule, i)` for unknown properties and wrong numbers of values.
- C++ scripts and host programs use the same class from `#include <grove/bark_api.hpp>`, with `std::function` handlers. `file()`, `actions()` and `values()` give the parts underneath when you need more.

### From Walnut

Scripts get the same thing through `import module.grove`: `grove::BarkFile`, `grove::BarkValues`, `grove::BarkActions`, `grove::BarkRunner` and `grove::BarkCall`. An action is a function that takes the call; it carries on to the next step unless it calls `call.wait()`, `call.stop()` or `call.fail("why")`. `Examples/bark` has the whole thing, with the answer to an `ask:` coming from a popup:

```walnut
import module.grove

grove::BarkFile: file = grove::BarkFile::parse("greet {\n    ask: mood \"How are you?\" \"fine\";\n    say: \"feeling {mood}\";\n}\n", "steps.bark");
grove::BarkValues: program;
program.set("app", "Grove");

grove::BarkActions: actions;
actions.add_standard();
actions.add_counted("say", 1, 1, [&](grove::BarkCall&: call) -> void {
    grove::log(call.arg(0));
});
actions.add_counted("ask", 1, 3, [&](grove::BarkCall&: call) -> void {
    call.wait();
});

grove::BarkRunner: runner = grove::BarkRunner(actions, file, "greet", program);
runner.run();
if (runner.waiting()) then {
    grove::prompt_string("Greeting", "How are you?", "fine", [&](string: answer) -> void {
        runner.answer("mood", answer);
    });
}
```

- `BarkFile::parse(source, path)` or `BarkFile::load(path)`; `ok()`, `error_count()`/`error_at(i)`, `rule_count()`, `rule_name(i)`, `find_rule(name)` (`-1` when it isn't there), `property_count(rule)`, `property_name(rule, i)`, `property_line`, `property_text`, `value_count`, `value_at(rule, i, n)`, `setting(rule, name)`, `setting_with(rule, name, values)`, the `$variables` (`variable_count`, `variable_name`, `variable_value`) and `add_variables_to(values)`.
- `BarkValues`: `set`, `set_default`, `has`, `get`, `get_or`, `erase`, `provide(name, function)`, `set_parent`, `interpolate(source)`, `unknown_count(source)`/`unknown_at(source, i)` for the placeholders nothing sets, and `BarkValues::escape_braces(source)`.
- `BarkActions`: `add(name, fn)`, `add_counted(name, min, max, fn)` (`-1` for no maximum), `add_raw` (the values come without placeholders filled in), `setting(name)`, `add_standard()`, `has`, `remove`, `check_count(file, rule)`/`check_at(file, rule, i)`.
- `BarkActions` functions: `add_function(name, fn)`, `add_function_counted(name, min, max, fn)`, `add_function_raw`, `add_standard_functions()`, `has_function`, `remove_function`, `evaluate_setting(file, rule, name, values)`, `evaluate_value(file, rule, property, index, values)` and `BarkActions::evaluate_error()` for why the last one came back empty.
- `BarkRunner(actions, file, rule)` or with a parent `BarkValues`: `run`, `step`, `resume`, `answer(name, value)`, `stop`, `waiting`, `done`, `stopped`, `failed`, `finished`, `error`, `position`, `size` and `values()`.
- `BarkCall`: `name`, `arg_count`, `arg(i)`, `arg_or(i, fallback)`, `rest(first)`, `line`, `values()`, and `wait`, `stop`, `fail`.

C++ scripts use the same classes from `#include <grove/bark_api.hpp>`. There an action can also return what happens next: `acts.add("say", [](grove::BarkCall& call) { return call.next(); })`.

### From C

`#include <grove/api/grove_api.h>` has it as plain functions on handles, for C scripts and anything else that can call C:

```c
grove_bark_file* file = grove_bark_load("steps.bark");
grove_bark_values* program = grove_bark_values_new();
grove_bark_values_set(program, "app", "Grove");
grove_bark_add_variables(file, program);

grove_bark_actions* actions = grove_bark_actions_new();
grove_bark_actions_add_standard(actions);
grove_bark_actions_add(actions, "say", 1, 1, 0, say, NULL, NULL);

grove_bark_runner* runner = grove_bark_runner_new(actions, file, (size_t)grove_bark_find_rule(file, "greet"), program);
if (grove_bark_runner_run(runner) == GROVE_BARK_WAITING) grove_bark_runner_answer(runner, "mood", "great");
```

with an action like

```c
static int say(grove_bark_call* call, void* user_data) {
    grove_log(grove_bark_call_arg(call, 0));
    return GROVE_BARK_NEXT;
}
```

- Handles: `grove_bark_values` (`_new`, `_free`, `_set`, `_set_default`, `_has`, `_get` (`NULL` when unset), `_erase`, `_provide`, `_set_parent`, `_count`, `_name`), `grove_bark_file` (`grove_bark_parse`, `grove_bark_load` (`NULL` when the file can't be read), `grove_bark_file_free`, errors, rules, properties, values, settings and variables), `grove_bark_actions` (`_new`, `_free`, `_add`, `_setting`, `_add_standard`, `_has`, `_remove`, and `grove_bark_check_count`/`grove_bark_check`) and `grove_bark_runner` (`_new`, `_free`, `_run`, `_step`, `_resume`, `_answer`, `_stop`, `_state`, `_error`, `_values`, `_position`, `_size`).
- Functions: `grove_bark_actions_add_function(actions, name, min, max, flags, fn, user_data, release)` with `const char* fn(grove_bark_call* call, void* user_data)` returning the text (copied right away) or failing with `grove_bark_call_fail`; the `grove_bark_call_*` readers work on it the same way. Also `grove_bark_actions_add_standard_functions`, `_has_function`, `_remove_function`, `grove_bark_evaluate_setting`, `grove_bark_evaluate_value` and `grove_bark_evaluate_error`.
- An action returns `GROVE_BARK_NEXT`, `GROVE_BARK_WAIT`, `GROVE_BARK_STOP` or `GROVE_BARK_FAIL` (`return grove_bark_call_fail(call, "why");` sets the message too), and reads its call with `grove_bark_call_name`, `_arg_count`, `_arg`, `_rest`, `_line`, `_values` and `_runner`. `GROVE_BARK_ANY` as the maximum allows any number of values, and the `GROVE_BARK_RAW` flag passes them without filling in placeholders.
- The runner's state is `GROVE_BARK_READY`, `GROVE_BARK_WAITING`, `GROVE_BARK_DONE`, `GROVE_BARK_STOPPED` or `GROVE_BARK_FAILED`.
- `grove_bark_interpolate(text, values)`, `grove_bark_unknown_count`/`grove_bark_unknown` and `grove_bark_escape_braces` work on any text.
- Strings that come back stay valid until a few more calls return strings, so copy one you want to keep. The `release` function given to `_add` or `_provide` is called with `user_data` when the actions or values are freed. A runner keeps its actions alive, but the parent values have to outlive it.

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
| `min-size` | `body`, `window` | `width height` (one value sets both): the window can't be resized smaller than this, and never opens smaller |
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
| `scroll-anchor` | blocks that scroll | `bottom` keeps the block scrolled to the end while you're at the end, so lines added at the bottom stay in view, like a log or a console. Scroll up and it stays where you put it; scroll back to the end and it follows again. `none` (default) doesn't |
| `zoom` | `body`, `window` | scales every `px` and `pt` length, font size and image in the window: `zoom: 1.25` (or `125%`) makes everything a quarter bigger. Viewport units are already relative to the window, so they don't change |
| `appearance` | `input_bool` | `checkbox` (default) or `switch` |
| `accent-color` | inputs | checked boxes and radios, slider fill and thumb, focused border, the caret and selection of `input_code` |
| `gap` | blocks, `row` | extra space after a block; space between the items of a row |
| `flex-wrap` | `row` | `nowrap` (default) keeps every item on one line; `wrap` moves items that don't fit onto the next line, like CSS. An item without a fixed width wraps once it can't keep its `min-width` (80px when it has none) |
| `row-gap` | `row` | space between the lines of a wrapping row (default: the `gap`) |
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
| `position` | blocks | `static` (default) lays it out with everything else; `fixed` takes it out of the page and draws it on top of everything at `left`/`top` (or `right`/`bottom`) from the window's edges; `cursor` does the same but follows the mouse, `left` and `top` (default 14px and 18px) away from the pointer, flipping to the other side near the window's edge, and never catches clicks, so the element under the mouse still gets them. Without a `width` it's as wide as its content. Handy for drag previews and badges |
| `left`, `top`, `right`, `bottom` | `position: fixed` or `cursor` | where it sits; see `position` |
| `cursor` | any | the mouse pointer while it's over the element: `default`, `pointer`, `text`, `crosshair`, `move` (also `grab`), `wait`, `progress`, `not-allowed`, `help`, and the resize arrows `col-resize` (or `ew-resize`), `row-resize` (or `ns-resize`), `nwse-resize` and `nesw-resize` for splitters and corners |
| `text-align` | text, `math` | `left`, `center`, `right` (`start` and `end` mean left and right) |
| `tab-size` | `pre` and other text with `white-space: pre`, `input_code` hovers | how many columns a tab character moves (default 4); children inherit it |
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
