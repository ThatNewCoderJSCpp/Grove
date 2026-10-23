#include "grove/grove.hpp"
#include "grove/android_package.hpp"
#include "grove/script/builder.hpp"
#include "grove/script/loader.hpp"

#ifdef GROVE_HAS_RUNTIME
#include "grove/runtime/runtime.hpp"
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <iostream>
#include <optional>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

namespace {

void print_usage(std::ostream& os) {
    os << "usage: grove <command> <file.grove> [options]\n"
          "\n"
          "commands:\n"
          "  check    parse the document, its Bark styles, math and scripts and report problems\n"
          "  dump     print the parsed element tree\n"
          "  styles   print the merged Bark stylesheet\n"
          "  computed print the computed style of every element\n"
          "  scripts  list the scripts the document uses; with --build, compile them too\n"
          "  exec     build and run the scripts without a window, then print the element tree\n"
          "  math     print every <math> block in brace form\n"
          "  inputs   list every input with its type and current value\n"
          "  highlight  print tokens, colors and diagnostics as JSON for editors (.grove or .bark; --stdin reads the text from standard input)\n"
          "  run      open the document's <body> as a window (needs the fizmo runtime)\n"
          "  build    make one executable that holds the document, its styles and its compiled scripts\n"
          "           (for an <!GROVE type=android> document, or with --target android, make a signed .apk)\n"
          "  version  print the Grove version\n"
          "\n"
          "options:\n"
          "  --window <id>   open the <window> with this id instead of <body>\n"
          "  --viewport WxH  window size used for vwidth/vheight/vmin/vmax/vdiag (default 800x600)\n"
          "  --stdin         with highlight: read the file's text from standard input\n"
          "  --build         with scripts: compile every script and report errors\n"
          "  --rebuild       compile scripts again even if they're cached\n"
          "  --no-scripts    with run: don't build or run scripts\n"
          "  -o <file>       with build: where to write the executable or .apk (default: the document's name)\n"
          "  --target <t>    with build: desktop or android (default: the document's <!GROVE type>)\n"
          "  --abi <list>    with an Android build: arm64-v8a, armeabi-v7a, x86_64 and/or x86, comma separated (default: abi= or arm64-v8a)\n"
          "  --android-kit <dir>  with an Android build: the folder holding one Grove runtime kit per ABI\n"
          "  --ndk <dir>     with an Android build: the Android NDK used to compile scripts (default: ANDROID_NDK_HOME or the SDK's ndk/)\n"
          "  --sdk <dir>     with an Android build: the Android SDK with build-tools and platforms (default: ANDROID_HOME)\n"
          "  --keystore <file>    with an Android build: sign with this keystore instead of the debug key\n"
          "  --ks-pass <p>   the keystore password: the password itself, env:VAR or file:path\n"
          "  --key-alias <a> the key in the keystore to sign with\n"
          "  --key-pass <p>  the key's password, if it isn't the keystore's (same forms as --ks-pass)\n"
          "  --install       with an Android build: install the .apk with adb\n"
          "  --launch        with an Android build: install the .apk with adb and start it\n"
          "  --event \"T S\"   with exec: dispatch event T to the first element matching selector S (repeatable)\n"
          "  --for <ms>      with exec: keep running timers for this long (default 0)\n"
          "  --tools         print where grove looks for walnut, the compilers and its headers\n"
          "  --full          with dump and exec: print text and attribute values in full instead of shortening them\n"
          "  -v, --verbose   print every compiler command\n"
          "  -q, --quiet     only print errors\n";
}

void dump_computed(const grove::Node& node, grove::Project& project, int depth) {
    if (!node.is_element()) return;
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    std::cout << indent << '<' << node.tag();
    if (!node.id().empty()) std::cout << " #" << node.id();
    std::cout << ">\n";
    const grove::bark::ComputedStyle& style = project.styles.style_of(node, project.diagnostics);
    for (const auto& [name, values] : style.values()) {
        std::cout << indent << "  " << name << ": " << grove::bark::to_string(values) << '\n';
    }
    for (const auto& c : node.children()) dump_computed(*c, project, depth + 1);
}

void print_diagnostics(const grove::Diagnostics& diagnostics, bool quiet) {
    for (const grove::Diagnostic& d : diagnostics.items()) {
        if (quiet && d.severity != grove::Severity::Error) continue;
        std::cerr << (d.file.empty() ? "<input>" : d.file) << ':' << d.span.begin.line << ':' << d.span.begin.column << ": " << grove::to_string(d.severity) << ": " << d.message << '\n';
    }
}

#ifdef GROVE_HAS_RUNTIME
int run_embedded_app(const grove::Bundle& bundle) {
#if defined(_WIN32)
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1) FreeConsole();
#endif
    grove::LoadedApp app;
    std::string error;
    if (!grove::load_app(bundle, app, error)) {
        std::cerr << error;
        if (!error.empty() && error.back() != '\n') std::cerr << '\n';
        return 1;
    }
    grove::runtime::RunOptions options;
    options.window_id = app.window_id;
    options.prebuilt = &app.scripts;
    return grove::runtime::run(app.project, options);
}
#endif

} // namespace

int main(int argc, char** argv) {
#ifdef GROVE_HAS_RUNTIME
    if (const std::optional<grove::Bundle> bundle = grove::read_embedded_bundle(grove::executable_path())) {
        return run_embedded_app(*bundle);
    }
#endif
    if (argc < 2) {
        print_usage(std::cerr);
        return 2;
    }
    const std::string command = argv[1];
    if (command == "-h" || command == "--help" || command == "help") {
        print_usage(std::cout);
        return 0;
    }
    if (command == "version" || command == "--version") {
        std::cout << "grove " << GROVE_VERSION_STRING << '\n';
        return 0;
    }

    std::string file;
    std::string window_id;
    std::string output;
    grove::bark::Viewport viewport{ 800, 600 };
    bool quiet = false;
    bool from_stdin = false;
    bool build_flag = false;
    bool rebuild = false;
    bool no_scripts = false;
    bool show_tools = false;
    bool verbose = false;
    bool full = false;
    double run_for = 0.0;
    std::optional<grove::Target> target;
    grove::AndroidBuildOptions android;
    std::vector<std::string> events;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-q" || a == "--quiet") quiet = true;
        else if (a == "--stdin") from_stdin = true;
        else if (a == "--build") build_flag = true;
        else if (a == "--rebuild") rebuild = true;
        else if (a == "--no-scripts") no_scripts = true;
        else if (a == "--tools") show_tools = true;
        else if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "--full") full = true;
        else if (a == "--event" && i + 1 < argc) events.push_back(argv[++i]);
        else if (a == "--for" && i + 1 < argc) {
            const auto ms = grove::parse_float(argv[++i]);
            if (!ms || *ms < 0) {
                std::cerr << "grove: --for expects milliseconds\n";
                return 2;
            }
            run_for = *ms;
        }
        else if (a == "--window" && i + 1 < argc) window_id = argv[++i];
        else if (a == "--target" && i + 1 < argc) {
            target = grove::parse_target(argv[++i]);
            if (!target) {
                std::cerr << "grove: --target expects desktop or android\n";
                return 2;
            }
        }
        else if (a == "--abi" && i + 1 < argc) {
            std::string list = argv[++i];
            std::replace(list.begin(), list.end(), ';', ',');
            for (const std::string& abi : grove::split_asset_list(list)) android.abis.push_back(abi);
        }
        else if (a == "--android-kit" && i + 1 < argc) android.kit_dir = argv[++i];
        else if (a == "--ndk" && i + 1 < argc) android.ndk = argv[++i];
        else if (a == "--sdk" && i + 1 < argc) android.sdk = argv[++i];
        else if (a == "--keystore" && i + 1 < argc) android.keystore = argv[++i];
        else if (a == "--ks-pass" && i + 1 < argc) android.ks_pass = argv[++i];
        else if (a == "--key-alias" && i + 1 < argc) android.key_alias = argv[++i];
        else if (a == "--key-pass" && i + 1 < argc) android.key_pass = argv[++i];
        else if (a == "--install") android.install = true;
        else if (a == "--launch") android.launch = true;
        else if ((a == "-o" || a == "--output") && i + 1 < argc) output = argv[++i];
        else if (a == "--viewport" && i + 1 < argc) {
            const std::string v = argv[++i];
            const std::size_t x = v.find('x');
            const auto w = x == std::string::npos ? std::nullopt : grove::parse_integer(v.substr(0, x));
            const auto h = x == std::string::npos ? std::nullopt : grove::parse_integer(v.substr(x + 1));
            if (!w || !h || *w <= 0 || *h <= 0) {
                std::cerr << "grove: --viewport expects WIDTHxHEIGHT, like 1280x720\n";
                return 2;
            }
            viewport = grove::bark::Viewport{ static_cast<double>(*w), static_cast<double>(*h) };
        }
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "grove: unknown option '" << a << "'\n";
            return 2;
        } else if (file.empty()) file = a;
        else {
            std::cerr << "grove: only one document can be given\n";
            return 2;
        }
    }
    if (file.empty()) {
        std::cerr << "grove: " << command << " needs a .grove file\n";
        return 2;
    }

    if (command == "highlight") {
        std::string text;
        if (from_stdin) {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            text = ss.str();
        } else if (const std::optional<std::string> read = grove::read_file(file)) {
            text = *read;
        } else {
            std::cerr << "grove: can't read '" << file << "'\n";
            return 1;
        }
        std::cout << grove::highlight_json(text, file);
        return 0;
    }

    if (grove::is_bark_path(file) && (command == "check" || command == "styles")) {
        const std::optional<std::string> text = grove::read_file(file);
        if (!text) {
            std::cerr << "grove: can't read '" << file << "'\n";
            return 1;
        }
        grove::Diagnostics diagnostics;
        const grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(*text, file, diagnostics);
        print_diagnostics(diagnostics, quiet || command == "styles");
        if (command == "styles") sheet.dump(std::cout);
        else if (!quiet) std::cout << file << ": " << diagnostics.error_count() << " error(s), " << diagnostics.warning_count() << " warning(s); " << sheet.rules().size() << " rule(s), " << sheet.variables().size() << " variable(s)\n";
        return diagnostics.ok() ? 0 : 1;
    }

    if (command == "build" && target != grove::Target::Desktop && (target == grove::Target::Android || grove::load_project(file).android())) {
        android.output = output;
        android.window_id = window_id;
        android.force_target = target == grove::Target::Android;
        android.tools = grove::default_script_tools(file);
        android.tools.rebuild = rebuild;
        android.tools.verbose = verbose;
        if (show_tools) std::cout << grove::describe(android.tools);
        grove::Diagnostics diagnostics;
        const grove::AndroidBuildResult result = grove::build_android_app(file, android, diagnostics, &std::cerr);
        print_diagnostics(diagnostics, quiet);
        if (!result.ok) {
            std::cerr << "grove: build failed\n";
            return 1;
        }
        if (!quiet) {
            std::string abis;
            for (const std::string& abi : result.abis) abis += (abis.empty() ? "" : ", ") + abi;
            const std::string size = result.bytes >= 1024 * 1024 ? std::to_string((result.bytes + 512 * 1024) / (1024 * 1024)) + " MB" : std::to_string((result.bytes + 512) / 1024) + " KB";
            std::cout << "built " << result.output << " (" << size << "; " << abis << "; " << result.files << " file" << (result.files == 1 ? "" : "s") << ", "
                      << result.modules << " script" << (result.modules == 1 ? "" : "s") << "; " << result.package << "; " << (result.debug_signed ? "signed with the debug key" : "signed") << ")\n";
            if (result.launched) std::cout << "installed and started " << result.package << '\n';
            else if (result.installed) std::cout << "installed " << result.package << '\n';
        }
        return 0;
    }

    if (command == "build") {
#ifdef GROVE_HAS_RUNTIME
        grove::AppBuildOptions options;
        options.output = output;
        options.window_id = window_id;
        options.tools = grove::default_script_tools(file);
        options.tools.rebuild = rebuild;
        options.tools.verbose = verbose;
        if (show_tools) std::cout << grove::describe(options.tools);
        grove::Diagnostics diagnostics;
        const grove::AppBuildResult result = grove::build_app(file, options, diagnostics, &std::cerr);
        print_diagnostics(diagnostics, quiet);
        if (!result.ok) {
            std::cerr << "grove: build failed\n";
            return 1;
        }
        if (!quiet) {
            std::cout << "built " << result.output << " (" << (result.bytes + 512 * 1024) / (1024 * 1024) << " MB; " << result.files << " file" << (result.files == 1 ? "" : "s") << ", "
                      << result.modules << " script" << (result.modules == 1 ? "" : "s") << ")\n";
        }
        return 0;
#else
        std::cerr << "grove: this build has no runtime, so it can't make an executable; configure with -DGROVE_BUILD_RUNTIME=ON\n";
        return 1;
#endif
    }

    grove::Project project = grove::load_project(file);
    project.styles.set_viewport(viewport);

    if (command == "check") {
        print_diagnostics(project.diagnostics, quiet);
        if (!quiet) {
            std::cout << file << ": " << project.diagnostics.error_count() << " error(s), " << project.diagnostics.warning_count() << " warning(s); "
                      << project.styles.sheet().rules().size() << " style rule(s), " << project.math.size() << " math block(s), " << project.scripts.size() << " script(s)\n";
        }
        return project.diagnostics.ok() ? 0 : 1;
    }
    if (command == "dump") {
        print_diagnostics(project.diagnostics, true);
        grove::dump(project.document.root(), std::cout, full ? grove::DumpOptions::full() : grove::DumpOptions{});
        return project.diagnostics.ok() ? 0 : 1;
    }
    if (command == "styles") {
        print_diagnostics(project.diagnostics, true);
        project.styles.sheet().dump(std::cout);
        return project.diagnostics.ok() ? 0 : 1;
    }
    if (command == "computed") {
        dump_computed(project.document.root(), project, 0);
        print_diagnostics(project.diagnostics, true);
        return project.diagnostics.ok() ? 0 : 1;
    }
    grove::ScriptTools tools = grove::default_script_tools(file);
    tools.rebuild = rebuild;
    tools.verbose = verbose;
    if (show_tools) std::cout << grove::describe(tools);

    if (command == "exec") {
        const grove::ScriptBuild build = grove::build_scripts(project, tools, &std::cerr);
        grove::Document& doc = project.document;
        doc.host().computed_style = [&project](const grove::Node& n, const std::string& property) {
            const grove::bark::ComputedStyle& style = project.styles.style_of(n, project.diagnostics);
            const grove::bark::ValueList* v = style.get(property);
            return v ? grove::bark::to_string(*v) : std::string();
        };
        grove::run_scripts(doc, build, project.diagnostics, project.path);
        grove::Event load;
        load.type = "load";
        load.bubbles = false;
        doc.events().dispatch(doc, nullptr, load);
        for (const std::string& spec : events) {
            const std::size_t space = spec.find(' ');
            const std::string type = spec.substr(0, space);
            const std::string selector = space == std::string::npos ? std::string() : spec.substr(space + 1);
            grove::Node* target = selector.empty() ? nullptr : grove::query(doc.root(), selector);
            if (!selector.empty() && !target) {
                std::cerr << "grove: --event: nothing matches '" << selector << "'\n";
                continue;
            }
            grove::Event e;
            e.type = type;
            if (doc.events().dispatch(doc, target, e) && type == "click" && target) grove::follow_link(doc, target);
            doc.events().run_due_timers();
        }
        const double until = doc.events().now() + run_for;
        while (!doc.events().quit_requested()) {
            doc.events().run_due_timers();
            const double next = doc.events().next_timer_due();
            const double now = doc.events().now();
            if (now >= until || next > until) break;
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(std::max(0.0, std::min(next, until) - now)));
        }
        grove::dump(doc.root(), std::cout, full ? grove::DumpOptions::full() : grove::DumpOptions{});
        print_diagnostics(project.diagnostics, quiet);
        return project.diagnostics.ok() ? 0 : 1;
    }

    if (command == "scripts") {
        if (build_flag) {
            const grove::ScriptBuild build = grove::build_scripts(project, tools, &std::cerr);
            print_diagnostics(project.diagnostics, quiet);
            for (const grove::BuiltScript& b : build.scripts) {
                std::cout << grove::to_string(b.script.language) << "  " << (b.script.inline_code ? "<inline, line " + std::to_string(b.script.span.begin.line) + ">" : b.script.source_path);
                if (!b.runnable) std::cout << "  (header, not run)\n";
                else std::cout << "  " << (b.ok ? (b.cached ? "cached" : "built") : "FAILED") << '\n';
            }
            return build.ok() && project.diagnostics.ok() ? 0 : 1;
        }
        print_diagnostics(project.diagnostics, true);
        for (const grove::ScriptBlock& s : project.scripts) {
            std::cout << grove::to_string(s.language) << "  ";
            if (s.inline_code) std::cout << "<inline, line " << s.span.begin.line << ">";
            else std::cout << s.source_path;
            if (!s.entry.empty()) std::cout << "  entry=" << s.entry;
            std::cout << '\n';
        }
        return project.diagnostics.ok() ? 0 : 1;
    }
    if (command == "math") {
        print_diagnostics(project.diagnostics, true);
        for (const grove::MathBlock& m : project.math) {
            std::cout << "line " << m.element->span().begin.line << ": " << grove::to_brace_form(m) << '\n';
        }
        return project.diagnostics.ok() ? 0 : 1;
    }
    if (command == "inputs") {
        print_diagnostics(project.diagnostics, true);
        project.document.root().walk([&project](const grove::Node& n) {
            const std::optional<grove::InputKind> kind = grove::input_kind_of(n);
            if (!kind) return;
            const bool toggle = grove::is_switch(n, project.styles.style_of(n, project.diagnostics).text("appearance"));
            std::cout << "line " << n.span().begin.line << "  " << grove::to_string(*kind) << (toggle ? " (switch)" : "");
            if (!n.id().empty()) std::cout << "  #" << n.id();
            if (*kind == grove::InputKind::Radio) std::cout << "  " << n.attribute_or("name", "") << "=" << n.attribute_or("value", "");
            std::cout << "  = " << grove::input_text(n) << '\n';
        });
        return project.diagnostics.ok() ? 0 : 1;
    }
    if (command == "run") {
        print_diagnostics(project.diagnostics, quiet);
        if (!project.diagnostics.ok()) return 1;
#ifdef GROVE_HAS_RUNTIME
        grove::runtime::RunOptions options;
        options.window_id = window_id;
        options.scripts = !no_scripts;
        options.tools = tools;
        return grove::runtime::run(project, options);
#else
        (void)no_scripts;
        std::cerr << "grove: this build has no runtime; configure with -DGROVE_BUILD_RUNTIME=ON\n";
        return 1;
#endif
    }

    std::cerr << "grove: unknown command '" << command << "'\n";
    print_usage(std::cerr);
    return 2;
}
