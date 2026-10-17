#ifndef GROVE_WINDOWING_HPP
#define GROVE_WINDOWING_HPP

#include "markup/node.hpp"

#include <string>
#include <vector>

namespace grove {

bool is_window_element(const Node& node);
bool is_popup(const Node& node);
Node* main_window(Document& document);
Node* window_of(Document& document, Node* node);
Node* owner_window(Document& document, Node& window);

bool window_is_open(Document& document, const Node& window);
bool open_window(Document& document, Node& window);
bool close_window(Document& document, Node& window);
bool window_size(Document& document, const Node& window, double& width, double& height);

struct PopupOptions {
    std::string kind = "info";
    std::string title;
    std::string message;
    std::string input;
    std::string value;
    std::vector<std::string> options;
};

std::vector<std::string> split_popup_options(const std::string& text);
Node* create_popup(Document& document, Node* owner, const PopupOptions& options);
bool answer_popup(Document& document, Node& popup, bool ok);
std::vector<Node*> open_popups(Document& document, const Node& owner);

} // namespace grove

#endif // GROVE_WINDOWING_HPP
