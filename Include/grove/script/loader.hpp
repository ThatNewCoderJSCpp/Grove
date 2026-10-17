#ifndef GROVE_SCRIPT_LOADER_HPP
#define GROVE_SCRIPT_LOADER_HPP

#include "builder.hpp"

namespace grove {

class Document;

std::size_t run_scripts(Document& document, const ScriptBuild& build, Diagnostics& diagnostics, const std::string& file);

} // namespace grove

#endif // GROVE_SCRIPT_LOADER_HPP
