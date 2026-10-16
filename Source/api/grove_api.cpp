#define GROVE_API_BUILDING
#include "grove/api/grove_api.h"
#include "grove/markup/node.hpp"

#include <cstring>
#include <string>

namespace {

grove::Document* doc_of(grove_document* d) noexcept { return reinterpret_cast<grove::Document*>(d); }
const grove::Document* doc_of(const grove_document* d) noexcept { return reinterpret_cast<const grove::Document*>(d); }
grove::Node* node_of(grove_element* e) noexcept { return reinterpret_cast<grove::Node*>(e); }
const grove::Node* node_of(const grove_element* e) noexcept { return reinterpret_cast<const grove::Node*>(e); }
grove_element* handle(grove::Node* n) noexcept { return reinterpret_cast<grove_element*>(n); }

grove_document* g_current = nullptr;

} // namespace

extern "C" {

grove_element* grove_root(grove_document* document) {
    return document ? handle(&doc_of(document)->root()) : nullptr;
}

grove_element* grove_find_by_id(grove_document* document, const char* id) {
    return document && id ? handle(doc_of(document)->find_by_id(id)) : nullptr;
}

grove_element* grove_first_by_tag(grove_document* document, const char* tag) {
    return document && tag ? handle(doc_of(document)->first_by_tag(tag)) : nullptr;
}

const char* grove_tag(const grove_element* element) {
    return element ? node_of(element)->tag().c_str() : "";
}

grove_element* grove_parent(grove_element* element) {
    return element ? handle(node_of(element)->parent()) : nullptr;
}

size_t grove_child_count(const grove_element* element) {
    return element ? node_of(element)->children().size() : 0;
}

grove_element* grove_child(grove_element* element, size_t index) {
    if (!element) return nullptr;
    const auto& children = node_of(element)->children();
    return index < children.size() ? handle(children[index].get()) : nullptr;
}

int grove_has_attribute(const grove_element* element, const char* name) {
    return element && name && node_of(element)->has_attribute(name) ? 1 : 0;
}

const char* grove_get_attribute(const grove_element* element, const char* name) {
    if (!element || !name) return nullptr;
    const grove::Attribute* a = node_of(element)->find_attribute(name);
    return a ? a->value.c_str() : nullptr;
}

void grove_set_attribute(grove_document* document, grove_element* element, const char* name, const char* value) {
    if (!element || !name) return;
    node_of(element)->set_attribute(name, value ? value : "");
    if (document) doc_of(document)->touch();
}

int grove_remove_attribute(grove_document* document, grove_element* element, const char* name) {
    if (!element || !name) return 0;
    const bool removed = node_of(element)->remove_attribute(name);
    if (removed && document) doc_of(document)->touch();
    return removed ? 1 : 0;
}

size_t grove_get_text(const grove_element* element, char* buffer, size_t size) {
    if (!element) return 0;
    const std::string text = node_of(element)->inner_text();
    if (buffer && size > 0) {
        const size_t n = text.size() < size - 1 ? text.size() : size - 1;
        std::memcpy(buffer, text.data(), n);
        buffer[n] = '\0';
    }
    return text.size();
}

void grove_set_text(grove_document* document, grove_element* element, const char* text) {
    if (!element) return;
    node_of(element)->set_inner_text(text ? text : "");
    if (document) doc_of(document)->touch();
}

unsigned long long grove_revision(const grove_document* document) {
    return document ? static_cast<unsigned long long>(doc_of(document)->revision()) : 0ULL;
}

void grove_bind_document(grove_document* document) {
    g_current = document;
}

grove_document* grove_current_document(void) {
    return g_current;
}

int grove_set_text_by_id(const char* id, const char* text) {
    grove_element* e = grove_find_by_id(g_current, id);
    if (!e) return 0;
    grove_set_text(g_current, e, text);
    return 1;
}

const char* grove_get_attribute_by_id(const char* id, const char* name) {
    return grove_get_attribute(grove_find_by_id(g_current, id), name);
}

int grove_set_attribute_by_id(const char* id, const char* name, const char* value) {
    grove_element* e = grove_find_by_id(g_current, id);
    if (!e) return 0;
    grove_set_attribute(g_current, e, name, value);
    return 1;
}

}
