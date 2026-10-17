#ifndef GROVE_HIGHLIGHT_HPP
#define GROVE_HIGHLIGHT_HPP

#include <string>
#include <string_view>

namespace grove {

bool is_bark_path(std::string_view path) noexcept;
std::string highlight_json(std::string_view source, const std::string& path);

} // namespace grove

#endif // GROVE_HIGHLIGHT_HPP
