#include <grove/api/grove_api.h>

extern "C" void grove_main(grove_document* document) {
    grove_element* lead = grove_first_by_tag(document, "p");
    if (lead) grove_set_attribute(document, lead, "title", "set from C++");
}
