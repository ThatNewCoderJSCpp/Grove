#ifndef GROVE_RUNTIME_RUNTIME_HPP
#define GROVE_RUNTIME_RUNTIME_HPP

#include "../project.hpp"

namespace grove {
namespace runtime {

struct RunOptions {
    std::string window_id;
    double max_fps = 60.0;
};

int run(Project& project, const RunOptions& options = RunOptions());

} // namespace runtime
} // namespace grove

#endif // GROVE_RUNTIME_RUNTIME_HPP
