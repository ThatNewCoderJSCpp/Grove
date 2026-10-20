#ifndef GROVE_BARK_PROPERTIES_HPP
#define GROVE_BARK_PROPERTIES_HPP

#include <string_view>
#include <vector>

namespace grove {
namespace bark {

bool is_known_property(std::string_view name) noexcept;
bool is_custom_property(std::string_view name) noexcept;
const std::vector<std::string_view>& known_properties() noexcept;

} // namespace bark
} // namespace grove

#endif // GROVE_BARK_PROPERTIES_HPP
