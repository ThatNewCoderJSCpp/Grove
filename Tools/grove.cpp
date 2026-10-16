#include "grove/grove.hpp"

#ifdef GROVE_HAS_RUNTIME
#include "grove/runtime/runtime.hpp"
#endif

#include <iostream>
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
          "  scripts  list the scripts the document uses\n"
          "  math     print every <math> block in brace form\n"
          "  run      open the document's window (needs the fizmo runtime)\n"
          "  version  print the Grove version\n"
          "\n"
          "options:\n"
          "  --window <id>   which <window> to open with run\n"
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

} // namespace

int main(int argc, char** argv) {
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
    bool quiet = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-q" || a == "--quiet") quiet = true;
        else if (a == "--window" && i + 1 < argc) window_id = argv[++i];
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

    grove::Project project = grove::load_project(file);

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
        grove::dump(project.document.root(), std::cout);
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
    if (command == "scripts") {
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
    if (command == "run") {
        print_diagnostics(project.diagnostics, quiet);
        if (!project.diagnostics.ok()) return 1;
#ifdef GROVE_HAS_RUNTIME
        grove::runtime::RunOptions options;
        options.window_id = window_id;
        return grove::runtime::run(project, options);
#else
        std::cerr << "grove: this build has no runtime; configure with -DGROVE_BUILD_RUNTIME=ON\n";
        return 1;
#endif
    }

    std::cerr << "grove: unknown command '" << command << "'\n";
    print_usage(std::cerr);
    return 2;
}
