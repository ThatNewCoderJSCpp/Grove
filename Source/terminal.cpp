#include "grove/terminal.hpp"
#include "grove/markup/node.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace grove {

namespace {

struct Range {
    char32_t first;
    char32_t last;
};

const Range k_zero_width[] = {
    { 0x0300, 0x036F }, { 0x0483, 0x0489 }, { 0x0591, 0x05BD }, { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 }, { 0x05C4, 0x05C5 },
    { 0x05C7, 0x05C7 }, { 0x0610, 0x061A }, { 0x061C, 0x061C }, { 0x064B, 0x065F }, { 0x0670, 0x0670 }, { 0x06D6, 0x06DC },
    { 0x06DF, 0x06E4 }, { 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED }, { 0x0711, 0x0711 }, { 0x0730, 0x074A }, { 0x07A6, 0x07B0 },
    { 0x07EB, 0x07F3 }, { 0x07FD, 0x07FD }, { 0x0816, 0x0819 }, { 0x081B, 0x0823 }, { 0x0825, 0x0827 }, { 0x0829, 0x082D },
    { 0x0859, 0x085B }, { 0x0898, 0x089F }, { 0x08CA, 0x08E1 }, { 0x08E3, 0x0902 }, { 0x093A, 0x093A }, { 0x093C, 0x093C },
    { 0x0941, 0x0948 }, { 0x094D, 0x094D }, { 0x0951, 0x0957 }, { 0x0962, 0x0963 }, { 0x0981, 0x0981 }, { 0x09BC, 0x09BC },
    { 0x09C1, 0x09C4 }, { 0x09CD, 0x09CD }, { 0x09E2, 0x09E3 }, { 0x09FE, 0x09FE }, { 0x0A01, 0x0A02 }, { 0x0A3C, 0x0A3C },
    { 0x0A41, 0x0A51 }, { 0x0A70, 0x0A71 }, { 0x0A75, 0x0A75 }, { 0x0A81, 0x0A82 }, { 0x0ABC, 0x0ABC }, { 0x0AC1, 0x0AC8 },
    { 0x0ACD, 0x0ACD }, { 0x0AE2, 0x0AE3 }, { 0x0AFA, 0x0AFF }, { 0x0B01, 0x0B01 }, { 0x0B3C, 0x0B3C }, { 0x0B3F, 0x0B3F },
    { 0x0B41, 0x0B44 }, { 0x0B4D, 0x0B4D }, { 0x0B55, 0x0B56 }, { 0x0B62, 0x0B63 }, { 0x0B82, 0x0B82 }, { 0x0BC0, 0x0BC0 },
    { 0x0BCD, 0x0BCD }, { 0x0C00, 0x0C00 }, { 0x0C04, 0x0C04 }, { 0x0C3C, 0x0C3C }, { 0x0C3E, 0x0C40 }, { 0x0C46, 0x0C56 },
    { 0x0C62, 0x0C63 }, { 0x0C81, 0x0C81 }, { 0x0CBC, 0x0CBC }, { 0x0CBF, 0x0CBF }, { 0x0CC6, 0x0CC6 }, { 0x0CCC, 0x0CCD },
    { 0x0CE2, 0x0CE3 }, { 0x0D00, 0x0D01 }, { 0x0D3B, 0x0D3C }, { 0x0D41, 0x0D44 }, { 0x0D4D, 0x0D4D }, { 0x0D62, 0x0D63 },
    { 0x0D81, 0x0D81 }, { 0x0DCA, 0x0DCA }, { 0x0DD2, 0x0DD6 }, { 0x0E31, 0x0E31 }, { 0x0E34, 0x0E3A }, { 0x0E47, 0x0E4E },
    { 0x0EB1, 0x0EB1 }, { 0x0EB4, 0x0EBC }, { 0x0EC8, 0x0ECE }, { 0x0F18, 0x0F19 }, { 0x0F35, 0x0F35 }, { 0x0F37, 0x0F37 },
    { 0x0F39, 0x0F39 }, { 0x0F71, 0x0F7E }, { 0x0F80, 0x0F84 }, { 0x0F86, 0x0F87 }, { 0x0F8D, 0x0FBC }, { 0x0FC6, 0x0FC6 },
    { 0x102D, 0x1030 }, { 0x1032, 0x1037 }, { 0x1039, 0x103A }, { 0x103D, 0x103E }, { 0x1058, 0x1059 }, { 0x105E, 0x1060 },
    { 0x1071, 0x1074 }, { 0x1082, 0x1082 }, { 0x1085, 0x1086 }, { 0x108D, 0x108D }, { 0x109D, 0x109D }, { 0x1160, 0x11FF },
    { 0x135D, 0x135F }, { 0x1712, 0x1714 }, { 0x1732, 0x1733 }, { 0x1752, 0x1753 }, { 0x1772, 0x1773 }, { 0x17B4, 0x17B5 },
    { 0x17B7, 0x17BD }, { 0x17C6, 0x17C6 }, { 0x17C9, 0x17D3 }, { 0x17DD, 0x17DD }, { 0x180B, 0x180F }, { 0x1885, 0x1886 },
    { 0x18A9, 0x18A9 }, { 0x1920, 0x1922 }, { 0x1927, 0x1928 }, { 0x1932, 0x1932 }, { 0x1939, 0x193B }, { 0x1A17, 0x1A18 },
    { 0x1A1B, 0x1A1B }, { 0x1A56, 0x1A56 }, { 0x1A58, 0x1A5E }, { 0x1A60, 0x1A60 }, { 0x1A62, 0x1A62 }, { 0x1A65, 0x1A6C },
    { 0x1A73, 0x1A7C }, { 0x1A7F, 0x1A7F }, { 0x1AB0, 0x1ACE }, { 0x1B00, 0x1B03 }, { 0x1B34, 0x1B34 }, { 0x1B36, 0x1B3A },
    { 0x1B3C, 0x1B3C }, { 0x1B42, 0x1B42 }, { 0x1B6B, 0x1B73 }, { 0x1B80, 0x1B81 }, { 0x1BA2, 0x1BA5 }, { 0x1BA8, 0x1BA9 },
    { 0x1BAB, 0x1BAD }, { 0x1BE6, 0x1BE6 }, { 0x1BE8, 0x1BE9 }, { 0x1BED, 0x1BED }, { 0x1BEF, 0x1BF1 }, { 0x1C2C, 0x1C33 },
    { 0x1C36, 0x1C37 }, { 0x1CD0, 0x1CD2 }, { 0x1CD4, 0x1CE0 }, { 0x1CE2, 0x1CE8 }, { 0x1CED, 0x1CED }, { 0x1CF4, 0x1CF4 },
    { 0x1CF8, 0x1CF9 }, { 0x1DC0, 0x1DFF }, { 0x200B, 0x200F }, { 0x202A, 0x202E }, { 0x2060, 0x2064 }, { 0x2066, 0x206F },
    { 0x20D0, 0x20F0 }, { 0x2CEF, 0x2CF1 }, { 0x2D7F, 0x2D7F }, { 0x2DE0, 0x2DFF }, { 0x302A, 0x302D }, { 0x3099, 0x309A },
    { 0xA66F, 0xA672 }, { 0xA674, 0xA67D }, { 0xA69E, 0xA69F }, { 0xA6F0, 0xA6F1 }, { 0xA802, 0xA802 }, { 0xA806, 0xA806 },
    { 0xA80B, 0xA80B }, { 0xA825, 0xA826 }, { 0xA82C, 0xA82C }, { 0xA8C4, 0xA8C5 }, { 0xA8E0, 0xA8F1 }, { 0xA8FF, 0xA8FF },
    { 0xA926, 0xA92D }, { 0xA947, 0xA951 }, { 0xA980, 0xA982 }, { 0xA9B3, 0xA9B3 }, { 0xA9B6, 0xA9B9 }, { 0xA9BC, 0xA9BD },
    { 0xA9E5, 0xA9E5 }, { 0xAA29, 0xAA2E }, { 0xAA31, 0xAA32 }, { 0xAA35, 0xAA36 }, { 0xAA43, 0xAA43 }, { 0xAA4C, 0xAA4C },
    { 0xAA7C, 0xAA7C }, { 0xAAB0, 0xAAB0 }, { 0xAAB2, 0xAAB4 }, { 0xAAB7, 0xAAB8 }, { 0xAABE, 0xAABF }, { 0xAAC1, 0xAAC1 },
    { 0xAAEC, 0xAAED }, { 0xAAF6, 0xAAF6 }, { 0xABE5, 0xABE5 }, { 0xABE8, 0xABE8 }, { 0xABED, 0xABED }, { 0xD7B0, 0xD7FF },
    { 0xFB1E, 0xFB1E }, { 0xFE00, 0xFE0F }, { 0xFE20, 0xFE2F }, { 0xFEFF, 0xFEFF }, { 0xFFF9, 0xFFFB }, { 0x101FD, 0x101FD },
    { 0x102E0, 0x102E0 }, { 0x10376, 0x1037A }, { 0x10A01, 0x10A0F }, { 0x10A38, 0x10A3F }, { 0x10AE5, 0x10AE6 }, { 0x10D24, 0x10D27 },
    { 0x10EAB, 0x10EAC }, { 0x10F46, 0x10F50 }, { 0x11001, 0x11001 }, { 0x11038, 0x11046 }, { 0x11070, 0x11070 }, { 0x11073, 0x11074 },
    { 0x1107F, 0x11081 }, { 0x110B3, 0x110B6 }, { 0x110B9, 0x110BA }, { 0x110C2, 0x110C2 }, { 0x11100, 0x11102 }, { 0x11127, 0x1112B },
    { 0x1112D, 0x11134 }, { 0x11173, 0x11173 }, { 0x11180, 0x11181 }, { 0x111B6, 0x111BE }, { 0x111C9, 0x111CC }, { 0x1122F, 0x11231 },
    { 0x11234, 0x11234 }, { 0x11236, 0x11237 }, { 0x1123E, 0x1123E }, { 0x112DF, 0x112DF }, { 0x112E3, 0x112EA }, { 0x11300, 0x11301 },
    { 0x1133B, 0x1133C }, { 0x11340, 0x11340 }, { 0x11366, 0x11374 }, { 0x11438, 0x1143F }, { 0x11442, 0x11444 }, { 0x11446, 0x11446 },
    { 0x1145E, 0x1145E }, { 0x114B3, 0x114B8 }, { 0x114BA, 0x114BA }, { 0x114BF, 0x114C0 }, { 0x114C2, 0x114C3 }, { 0x115B2, 0x115B5 },
    { 0x115BC, 0x115BD }, { 0x115BF, 0x115C0 }, { 0x115DC, 0x115DD }, { 0x11633, 0x1163A }, { 0x1163D, 0x1163D }, { 0x1163F, 0x11640 },
    { 0x116AB, 0x116AB }, { 0x116AD, 0x116AD }, { 0x116B0, 0x116B5 }, { 0x116B7, 0x116B7 }, { 0x1171D, 0x1171F }, { 0x11722, 0x11725 },
    { 0x11727, 0x1172B }, { 0x1182F, 0x11837 }, { 0x11839, 0x1183A }, { 0x1193B, 0x1193C }, { 0x1193E, 0x1193E }, { 0x11943, 0x11943 },
    { 0x16AF0, 0x16AF4 }, { 0x16B30, 0x16B36 }, { 0x16F4F, 0x16F4F }, { 0x16F8F, 0x16F92 }, { 0x16FE4, 0x16FE4 }, { 0x1BC9D, 0x1BC9E },
    { 0x1BCA0, 0x1BCA3 }, { 0x1CF00, 0x1CF2D }, { 0x1CF30, 0x1CF46 }, { 0x1D167, 0x1D169 }, { 0x1D173, 0x1D182 }, { 0x1D185, 0x1D18B },
    { 0x1D1AA, 0x1D1AD }, { 0x1D242, 0x1D244 }, { 0x1DA00, 0x1DA36 }, { 0x1DA3B, 0x1DA6C }, { 0x1DA75, 0x1DA75 }, { 0x1DA84, 0x1DA84 },
    { 0x1DA9B, 0x1DA9F }, { 0x1DAA1, 0x1DAAF }, { 0x1E000, 0x1E02A }, { 0x1E08F, 0x1E08F }, { 0x1E130, 0x1E136 }, { 0x1E2AE, 0x1E2AE },
    { 0x1E2EC, 0x1E2EF }, { 0x1E4EC, 0x1E4EF }, { 0x1E8D0, 0x1E8D6 }, { 0x1E944, 0x1E94A }, { 0xE0001, 0xE0001 }, { 0xE0020, 0xE007F },
    { 0xE0100, 0xE01EF },
};

const Range k_wide[] = {
    { 0x1100, 0x115F }, { 0x231A, 0x231B }, { 0x2329, 0x232A }, { 0x23E9, 0x23EC }, { 0x23F0, 0x23F0 }, { 0x23F3, 0x23F3 },
    { 0x25FD, 0x25FE }, { 0x2614, 0x2615 }, { 0x2648, 0x2653 }, { 0x267F, 0x267F }, { 0x2693, 0x2693 }, { 0x26A1, 0x26A1 },
    { 0x26AA, 0x26AB }, { 0x26BD, 0x26BE }, { 0x26C4, 0x26C5 }, { 0x26CE, 0x26CE }, { 0x26D4, 0x26D4 }, { 0x26EA, 0x26EA },
    { 0x26F2, 0x26F3 }, { 0x26F5, 0x26F5 }, { 0x26FA, 0x26FA }, { 0x26FD, 0x26FD }, { 0x2705, 0x2705 }, { 0x270A, 0x270B },
    { 0x2728, 0x2728 }, { 0x274C, 0x274C }, { 0x274E, 0x274E }, { 0x2753, 0x2755 }, { 0x2757, 0x2757 }, { 0x2795, 0x2797 },
    { 0x27B0, 0x27B0 }, { 0x27BF, 0x27BF }, { 0x2B1B, 0x2B1C }, { 0x2B50, 0x2B50 }, { 0x2B55, 0x2B55 }, { 0x2E80, 0x303E },
    { 0x3041, 0x33FF }, { 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF }, { 0xA000, 0xA4CF }, { 0xA960, 0xA97F }, { 0xAC00, 0xD7A3 },
    { 0xF900, 0xFAFF }, { 0xFE10, 0xFE19 }, { 0xFE30, 0xFE6F }, { 0xFF00, 0xFF60 }, { 0xFFE0, 0xFFE6 }, { 0x16FE0, 0x16FE3 },
    { 0x16FF0, 0x16FF1 }, { 0x17000, 0x187F7 }, { 0x18800, 0x18CD5 }, { 0x18D00, 0x18D08 }, { 0x1AFF0, 0x1AFFE }, { 0x1B000, 0x1B122 },
    { 0x1B132, 0x1B132 }, { 0x1B150, 0x1B152 }, { 0x1B155, 0x1B155 }, { 0x1B164, 0x1B167 }, { 0x1B170, 0x1B2FB }, { 0x1F004, 0x1F004 },
    { 0x1F0CF, 0x1F0CF }, { 0x1F18E, 0x1F18E }, { 0x1F191, 0x1F19A }, { 0x1F200, 0x1F202 }, { 0x1F210, 0x1F23B }, { 0x1F240, 0x1F248 },
    { 0x1F250, 0x1F251 }, { 0x1F260, 0x1F265 }, { 0x1F300, 0x1F320 }, { 0x1F32D, 0x1F335 }, { 0x1F337, 0x1F37C }, { 0x1F37E, 0x1F393 },
    { 0x1F3A0, 0x1F3CA }, { 0x1F3CF, 0x1F3D3 }, { 0x1F3E0, 0x1F3F0 }, { 0x1F3F4, 0x1F3F4 }, { 0x1F3F8, 0x1F43E }, { 0x1F440, 0x1F440 },
    { 0x1F442, 0x1F4FC }, { 0x1F4FF, 0x1F53D }, { 0x1F54B, 0x1F54E }, { 0x1F550, 0x1F567 }, { 0x1F57A, 0x1F57A }, { 0x1F595, 0x1F596 },
    { 0x1F5A4, 0x1F5A4 }, { 0x1F5FB, 0x1F64F }, { 0x1F680, 0x1F6C5 }, { 0x1F6CC, 0x1F6CC }, { 0x1F6D0, 0x1F6D2 }, { 0x1F6D5, 0x1F6D7 },
    { 0x1F6DC, 0x1F6DF }, { 0x1F6EB, 0x1F6EC }, { 0x1F6F4, 0x1F6FC }, { 0x1F7E0, 0x1F7EB }, { 0x1F7F0, 0x1F7F0 }, { 0x1F90C, 0x1F93A },
    { 0x1F93C, 0x1F945 }, { 0x1F947, 0x1F9FF }, { 0x1FA70, 0x1FA7C }, { 0x1FA80, 0x1FA88 }, { 0x1FA90, 0x1FABD }, { 0x1FABF, 0x1FAC5 },
    { 0x1FACE, 0x1FADB }, { 0x1FAE0, 0x1FAE8 }, { 0x1FAF0, 0x1FAF8 }, { 0x20000, 0x2FFFD }, { 0x30000, 0x3FFFD },
};

template <std::size_t N>
bool in_table(const Range (&table)[N], char32_t c) {
    std::size_t lo = 0;
    std::size_t hi = N;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (c < table[mid].first) hi = mid;
        else if (c > table[mid].last) lo = mid + 1;
        else return true;
    }
    return false;
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

char32_t dec_graphics(char32_t c) {
    static const char32_t table[32] = {
        0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C,
        0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
    };
    if (c >= 0x5F && c <= 0x7E) return table[c - 0x5F];
    return c;
}

std::string hex4(int v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04x", v * 257);
    return buf;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_hex_component(const std::string& text, int& out) {
    if (text.empty() || text.size() > 4) return false;
    int v = 0;
    for (char c : text) {
        const int d = hex_digit(c);
        if (d < 0) return false;
        v = v * 16 + d;
    }
    const int max = (1 << (4 * static_cast<int>(text.size()))) - 1;
    out = (v * 255 + max / 2) / max;
    return true;
}

bool parse_color_spec(const std::string& spec, TermRgb& out) {
    if (spec.rfind("rgb:", 0) == 0) {
        const std::string rest = spec.substr(4);
        const std::size_t a = rest.find('/');
        const std::size_t b = a == std::string::npos ? std::string::npos : rest.find('/', a + 1);
        if (b == std::string::npos) return false;
        int r = 0;
        int g = 0;
        int bl = 0;
        if (!parse_hex_component(rest.substr(0, a), r) || !parse_hex_component(rest.substr(a + 1, b - a - 1), g) || !parse_hex_component(rest.substr(b + 1), bl)) return false;
        out = TermRgb{ static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(bl) };
        return true;
    }
    if (!spec.empty() && spec[0] == '#') {
        const std::string hex = spec.substr(1);
        if (hex.empty() || hex.size() % 3 != 0 || hex.size() > 12) return false;
        const std::size_t n = hex.size() / 3;
        int r = 0;
        int g = 0;
        int bl = 0;
        if (!parse_hex_component(hex.substr(0, n), r) || !parse_hex_component(hex.substr(n, n), g) || !parse_hex_component(hex.substr(2 * n, n), bl)) return false;
        out = TermRgb{ static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(bl) };
        return true;
    }
    return false;
}

std::string percent_decode(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hex_digit(text[i + 1]) >= 0 && hex_digit(text[i + 2]) >= 0) {
            out += static_cast<char>(hex_digit(text[i + 1]) * 16 + hex_digit(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool word_char(char32_t c) {
    if (c == U' ' || c == 0) return false;
    static const std::u32string separators = U"()[]{}'\",`;|<>─│‘’“”";
    return separators.find(c) == std::u32string::npos;
}

const TermLine& empty_line() {
    static const TermLine line;
    return line;
}

} // namespace

int terminal_char_width(char32_t c) noexcept {
    if (c == 0) return 0;
    if (c < 0x20 || (c >= 0x7F && c < 0xA0)) return -1;
    if (c < 0x300) return 1;
    if (in_table(k_zero_width, c)) return 0;
    if (in_table(k_wide, c)) return 2;
    return 1;
}

Terminal::Terminal(int columns, int rows, std::size_t scrollback) : m_cols(std::max(1, columns)), m_rows(std::max(1, rows)), m_limit(scrollback) {
    reset_palette();
    reset();
}

void Terminal::reset() {
    m_main.assign(static_cast<std::size_t>(m_rows), TermLine{});
    m_alt.assign(static_cast<std::size_t>(m_rows), TermLine{});
    m_alt_active = false;
    m_cur = Cursor{};
    for (auto& l : m_main) l.cells.assign(static_cast<std::size_t>(m_cols), TermCell{});
    for (auto& l : m_alt) l.cells.assign(static_cast<std::size_t>(m_cols), TermCell{});
    m_saved_main = Cursor{};
    m_saved_alt = Cursor{};
    m_top = 0;
    m_bottom = m_rows - 1;
    m_tabs.assign(static_cast<std::size_t>(m_cols), false);
    for (int i = m_tab_width; i < m_cols; i += m_tab_width) m_tabs[static_cast<std::size_t>(i)] = true;
    m_insert = false;
    m_newline_mode = false;
    m_app_cursor = false;
    m_app_keypad = false;
    m_cursor_visible = true;
    m_cursor_blink = false;
    m_cursor_shape = TermCursorShape::Block;
    m_bracketed_paste = false;
    m_reverse_video = false;
    m_focus_events = false;
    m_alternate_scroll = true;
    m_mouse_mode = TermMouseMode::None;
    m_mouse_encoding = TermMouseEncoding::Default;
    m_state = State::Ground;
    clear_params();
    m_view = 0;
    m_title.clear();
    m_title_changed = true;
    m_default_fg = m_base_fg;
    m_default_bg = m_base_bg;
    m_cursor_color = m_base_cursor;
    clear_selection();
    touch();
}

void Terminal::set_tab_width(int columns) {
    columns = std::clamp(columns, 1, 16);
    if (columns == m_tab_width) return;
    m_tab_width = columns;
    m_tabs.assign(static_cast<std::size_t>(m_cols), false);
    for (int i = m_tab_width; i < m_cols; i += m_tab_width) m_tabs[static_cast<std::size_t>(i)] = true;
}

void Terminal::set_scrollback_limit(std::size_t lines) {
    m_limit = lines;
    while (m_history.size() > m_limit) {
        m_history.pop_front();
        shift_selection(-1);
    }
    m_view = std::min<int>(m_view, static_cast<int>(m_history.size()));
}

void Terminal::clear_scrollback() {
    m_history.clear();
    m_view = 0;
    clear_selection();
    touch();
}

const TermLine& Terminal::line(int row) const {
    const auto& s = screen();
    if (row < 0 || row >= static_cast<int>(s.size())) return empty_line();
    return s[static_cast<std::size_t>(row)];
}

const TermLine& Terminal::absolute_line(long long l) const {
    if (l < 0) return empty_line();
    if (l < static_cast<long long>(m_history.size())) return m_history[static_cast<std::size_t>(l)];
    return line(static_cast<int>(l - static_cast<long long>(m_history.size())));
}

const TermLine& Terminal::view_line(int row) const { return absolute_line(view_line_number(row)); }

void Terminal::scroll_view(int lines) {
    const int before = m_view;
    m_view = std::clamp(m_view + lines, 0, m_alt_active ? 0 : static_cast<int>(m_history.size()));
    if (m_view != before) touch();
}

TermCell Terminal::blank() const {
    TermCell c;
    c.bg = m_cur.pen.bg;
    return c;
}

TermLine Terminal::blank_line() const {
    TermLine l;
    l.cells.assign(static_cast<std::size_t>(m_cols), blank());
    return l;
}

void Terminal::clear_params() {
    m_params.clear();
    m_subparams.clear();
    m_param_started = false;
    m_sub = false;
    m_intermediates.clear();
    m_prefix = 0;
}

int Terminal::param(std::size_t i, int fallback) const {
    if (i >= m_params.size() || m_params[i] <= 0) return fallback;
    return m_params[i];
}

void Terminal::feed(std::string_view bytes) {
    for (unsigned char c : bytes) {
        if (m_utf8_left > 0) {
            if ((c & 0xC0) == 0x80) {
                m_utf8 = (m_utf8 << 6) | (c & 0x3F);
                if (--m_utf8_left == 0) {
                    char32_t cp = m_utf8;
                    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                    if (m_state == State::Ground) print(cp);
                    else if (m_state == State::Osc || m_state == State::Dcs || m_state == State::String) append_utf8(m_string, cp);
                }
                continue;
            }
            m_utf8_left = 0;
            if (m_state == State::Ground) print(0xFFFD);
        }
        if (c >= 0x80) {
            if (c >= 0xC2 && c <= 0xDF) {
                m_utf8 = c & 0x1F;
                m_utf8_left = 1;
            } else if (c >= 0xE0 && c <= 0xEF) {
                m_utf8 = c & 0x0F;
                m_utf8_left = 2;
            } else if (c >= 0xF0 && c <= 0xF4) {
                m_utf8 = c & 0x07;
                m_utf8_left = 3;
            } else if (m_state == State::Ground) {
                print(0xFFFD);
            }
            continue;
        }
        byte(c);
    }
}

void Terminal::byte(unsigned char c) {
    if (m_state == State::Osc || m_state == State::Dcs || m_state == State::String) {
        if (m_string_escape) {
            m_string_escape = false;
            const State kind = m_state;
            m_state = State::Ground;
            if (kind == State::Osc) osc_dispatch();
            else if (kind == State::Dcs) dcs_dispatch();
            m_string.clear();
            if (c == '\\') return;
            m_state = State::Escape;
            clear_params();
            byte(c);
            return;
        }
        if (c == 0x1B) {
            m_string_escape = true;
            return;
        }
        if (c == 0x07) {
            const State kind = m_state;
            m_state = State::Ground;
            if (kind == State::Osc) osc_dispatch();
            else if (kind == State::Dcs) dcs_dispatch();
            m_string.clear();
            return;
        }
        if (c == 0x18 || c == 0x1A) {
            m_state = State::Ground;
            m_string.clear();
            return;
        }
        if (c >= 0x20 || m_state != State::Osc) {
            if (m_string.size() < 65536) m_string += static_cast<char>(c);
        }
        return;
    }

    if (c == 0x18 || c == 0x1A) {
        m_state = State::Ground;
        clear_params();
        return;
    }
    if (c == 0x1B) {
        m_state = State::Escape;
        clear_params();
        return;
    }
    if (c < 0x20) {
        control(c);
        return;
    }
    if (c == 0x7F) return;

    switch (m_state) {
        case State::Ground:
            print(c);
            break;
        case State::Escape:
            if (c >= 0x20 && c <= 0x2F) {
                m_intermediates += static_cast<char>(c);
                m_state = State::EscapeIntermediate;
            } else if (c == '[') {
                clear_params();
                m_state = State::CsiEntry;
            } else if (c == ']') {
                m_string.clear();
                m_state = State::Osc;
            } else if (c == 'P') {
                m_string.clear();
                m_state = State::Dcs;
            } else if (c == 'X' || c == '^' || c == '_') {
                m_string.clear();
                m_state = State::String;
            } else {
                m_state = State::Ground;
                escape_dispatch(c);
            }
            break;
        case State::EscapeIntermediate:
            if (c >= 0x20 && c <= 0x2F) m_intermediates += static_cast<char>(c);
            else {
                m_state = State::Ground;
                escape_dispatch(c);
            }
            break;
        case State::CsiEntry:
        case State::CsiParam:
            if (c >= '0' && c <= '9') {
                if (!m_param_started) {
                    m_params.push_back(0);
                    m_subparams.emplace_back();
                    m_param_started = true;
                }
                if (m_sub) {
                    int& v = m_subparams.back().back();
                    v = std::min((v < 0 ? 0 : v) * 10 + (c - '0'), 99999);
                } else {
                    m_params.back() = std::min(m_params.back() * 10 + (c - '0'), 99999);
                }
                m_state = State::CsiParam;
            } else if (c == ';') {
                if (!m_param_started) {
                    m_params.push_back(0);
                    m_subparams.emplace_back();
                }
                m_param_started = false;
                m_sub = false;
                m_state = State::CsiParam;
            } else if (c == ':') {
                if (!m_param_started) {
                    m_params.push_back(0);
                    m_subparams.emplace_back();
                    m_param_started = true;
                }
                m_sub = true;
                m_subparams.back().push_back(-1);
                m_state = State::CsiParam;
            } else if (c >= '<' && c <= '?') {
                if (m_state == State::CsiEntry && m_params.empty()) {
                    m_prefix = static_cast<char>(c);
                    m_state = State::CsiParam;
                } else {
                    m_state = State::CsiIgnore;
                }
            } else if (c >= 0x20 && c <= 0x2F) {
                m_intermediates += static_cast<char>(c);
                m_state = State::CsiIntermediate;
            } else if (c >= 0x40 && c <= 0x7E) {
                m_state = State::Ground;
                csi_dispatch(c);
            }
            break;
        case State::CsiIntermediate:
            if (c >= 0x20 && c <= 0x2F) m_intermediates += static_cast<char>(c);
            else if (c >= 0x30 && c <= 0x3F) m_state = State::CsiIgnore;
            else if (c >= 0x40 && c <= 0x7E) {
                m_state = State::Ground;
                csi_dispatch(c);
            }
            break;
        case State::CsiIgnore:
            if (c >= 0x40 && c <= 0x7E) m_state = State::Ground;
            break;
        default:
            break;
    }
}

void Terminal::control(unsigned char c) {
    switch (c) {
        case 0x07:
            m_bell = true;
            break;
        case 0x08:
            if (m_cur.pending_wrap) m_cur.pending_wrap = false;
            if (m_cur.x > 0) --m_cur.x;
            break;
        case 0x09:
            tab_forward(1);
            break;
        case 0x0A:
        case 0x0B:
        case 0x0C:
            line_feed();
            if (m_newline_mode) carriage_return();
            break;
        case 0x0D:
            carriage_return();
            break;
        case 0x0E:
            m_cur.gl = 1;
            break;
        case 0x0F:
            m_cur.gl = 0;
            break;
        default:
            break;
    }
    touch();
}

char32_t Terminal::translate(char32_t c) const {
    if (c > 0x7E) return c;
    const int set = m_cur.single_shift >= 0 ? m_cur.single_shift : m_cur.gl;
    const char cs = m_cur.charsets[static_cast<std::size_t>(std::clamp(set, 0, 3))];
    if (cs == '0') return dec_graphics(c);
    if (cs == 'A' && c == '#') return 0x00A3;
    return c;
}

void Terminal::fix_wide_at(int row, int column) {
    auto& cells = screen()[static_cast<std::size_t>(row)].cells;
    if (column < 0 || column >= m_cols) return;
    TermCell& cell = cells[static_cast<std::size_t>(column)];
    if (cell.width == 0 && column > 0) {
        TermCell& lead = cells[static_cast<std::size_t>(column - 1)];
        if (lead.width == 2) {
            lead.ch = U' ';
            lead.width = 1;
            lead.marks.clear();
        }
    } else if (cell.width == 2 && column + 1 < m_cols) {
        TermCell& tail = cells[static_cast<std::size_t>(column + 1)];
        if (tail.width == 0) {
            tail.ch = U' ';
            tail.width = 1;
        }
    }
}

void Terminal::print(char32_t c) {
    c = translate(c);
    m_cur.single_shift = -1;
    const int w = terminal_char_width(c);
    if (w < 0) return;
    auto& s = screen();
    if (w == 0) {
        int x = m_cur.pending_wrap ? m_cur.x : m_cur.x - 1;
        if (x < 0) return;
        TermCell* target = &s[static_cast<std::size_t>(m_cur.y)].cells[static_cast<std::size_t>(x)];
        if (target->width == 0 && x > 0) target = &s[static_cast<std::size_t>(m_cur.y)].cells[static_cast<std::size_t>(x - 1)];
        if (target->marks.size() < 8) target->marks += c;
        touch();
        return;
    }
    if (m_cur.pending_wrap && m_cur.autowrap) {
        s[static_cast<std::size_t>(m_cur.y)].wrapped = true;
        m_cur.x = 0;
        m_cur.pending_wrap = false;
        line_feed();
    }
    if (w == 2 && m_cur.x == m_cols - 1) {
        if (m_cur.autowrap && m_cols > 1) {
            fix_wide_at(m_cur.y, m_cur.x);
            TermCell& last = screen()[static_cast<std::size_t>(m_cur.y)].cells[static_cast<std::size_t>(m_cur.x)];
            last = blank();
            screen()[static_cast<std::size_t>(m_cur.y)].wrapped = true;
            m_cur.x = 0;
            line_feed();
        } else if (m_cols > 1) {
            m_cur.x = m_cols - 2;
        } else {
            return;
        }
    }
    if (m_insert) insert_chars(w);
    auto& cells = screen()[static_cast<std::size_t>(m_cur.y)].cells;
    fix_wide_at(m_cur.y, m_cur.x);
    if (w == 2) fix_wide_at(m_cur.y, m_cur.x + 1);
    TermCell& cell = cells[static_cast<std::size_t>(m_cur.x)];
    cell = m_cur.pen;
    cell.ch = c;
    cell.width = static_cast<std::uint8_t>(w);
    cell.marks.clear();
    if (w == 2 && m_cur.x + 1 < m_cols) {
        TermCell& tail = cells[static_cast<std::size_t>(m_cur.x + 1)];
        tail = m_cur.pen;
        tail.ch = U' ';
        tail.width = 0;
        tail.marks.clear();
    }
    m_last_char = c;
    if (m_cur.x + w >= m_cols) {
        m_cur.x = m_cols - 1;
        m_cur.pending_wrap = m_cur.autowrap;
    } else {
        m_cur.x += w;
    }
    touch();
}

void Terminal::push_history(TermLine line) {
    if (m_limit == 0) return;
    m_history.push_back(std::move(line));
    if (m_view > 0) m_view = std::min<int>(m_view + 1, static_cast<int>(m_history.size()));
    while (m_history.size() > m_limit) {
        m_history.pop_front();
        shift_selection(-1);
        if (m_view > static_cast<int>(m_history.size())) m_view = static_cast<int>(m_history.size());
    }
}

void Terminal::shift_selection(long long lines) {
    if (!m_sel_active) return;
    m_sel_from.line += lines;
    m_sel_to.line += lines;
    if (m_sel_to.line < 0) clear_selection();
    else if (m_sel_from.line < 0) m_sel_from = TermPoint{ 0, 0 };
}

void Terminal::scroll_up(int top, int bottom, int count, bool keep) {
    if (top < 0 || bottom >= m_rows || top > bottom) return;
    count = std::min(count, bottom - top + 1);
    if (count <= 0) return;
    auto& s = screen();
    const bool save = keep && !m_alt_active && top == 0;
    for (int i = 0; i < count; ++i) {
        if (save) push_history(std::move(s[static_cast<std::size_t>(top)]));
        s.erase(s.begin() + top);
        s.insert(s.begin() + bottom, blank_line());
    }
    touch();
}

void Terminal::scroll_down(int top, int bottom, int count) {
    if (top < 0 || bottom >= m_rows || top > bottom) return;
    count = std::min(count, bottom - top + 1);
    if (count <= 0) return;
    auto& s = screen();
    for (int i = 0; i < count; ++i) {
        s.erase(s.begin() + bottom);
        s.insert(s.begin() + top, blank_line());
    }
    touch();
}

void Terminal::line_feed() {
    m_cur.pending_wrap = false;
    if (m_cur.y == m_bottom) scroll_up(m_top, m_bottom, 1);
    else if (m_cur.y < m_rows - 1) ++m_cur.y;
    touch();
}

void Terminal::reverse_index() {
    m_cur.pending_wrap = false;
    if (m_cur.y == m_top) scroll_down(m_top, m_bottom, 1);
    else if (m_cur.y > 0) --m_cur.y;
    touch();
}

void Terminal::carriage_return() {
    m_cur.x = 0;
    m_cur.pending_wrap = false;
}

void Terminal::move_to(int x, int y) {
    m_cur.pending_wrap = false;
    if (m_cur.origin) {
        m_cur.y = std::clamp(y + m_top, m_top, m_bottom);
    } else {
        m_cur.y = std::clamp(y, 0, m_rows - 1);
    }
    m_cur.x = std::clamp(x, 0, m_cols - 1);
    touch();
}

void Terminal::clamp_cursor() {
    m_cur.x = std::clamp(m_cur.x, 0, m_cols - 1);
    m_cur.y = std::clamp(m_cur.y, 0, m_rows - 1);
}

void Terminal::erase_cells(int row, int from, int to) {
    if (row < 0 || row >= m_rows) return;
    from = std::max(0, from);
    to = std::min(m_cols, to);
    if (from >= to) return;
    fix_wide_at(row, from);
    fix_wide_at(row, to - 1);
    auto& cells = screen()[static_cast<std::size_t>(row)].cells;
    const TermCell b = blank();
    for (int i = from; i < to; ++i) cells[static_cast<std::size_t>(i)] = b;
    touch();
}

void Terminal::erase_display(int mode) {
    m_cur.pending_wrap = false;
    if (mode == 0) {
        erase_cells(m_cur.y, m_cur.x, m_cols);
        for (int r = m_cur.y + 1; r < m_rows; ++r) {
            erase_cells(r, 0, m_cols);
            screen()[static_cast<std::size_t>(r)].wrapped = false;
        }
        screen()[static_cast<std::size_t>(m_cur.y)].wrapped = false;
    } else if (mode == 1) {
        for (int r = 0; r < m_cur.y; ++r) {
            erase_cells(r, 0, m_cols);
            screen()[static_cast<std::size_t>(r)].wrapped = false;
        }
        erase_cells(m_cur.y, 0, m_cur.x + 1);
    } else if (mode == 2) {
        for (int r = 0; r < m_rows; ++r) {
            erase_cells(r, 0, m_cols);
            screen()[static_cast<std::size_t>(r)].wrapped = false;
        }
    } else if (mode == 3) {
        clear_scrollback();
    }
}

void Terminal::erase_line(int mode) {
    m_cur.pending_wrap = false;
    if (mode == 0) {
        erase_cells(m_cur.y, m_cur.x, m_cols);
        screen()[static_cast<std::size_t>(m_cur.y)].wrapped = false;
    } else if (mode == 1) {
        erase_cells(m_cur.y, 0, m_cur.x + 1);
    } else if (mode == 2) {
        erase_cells(m_cur.y, 0, m_cols);
        screen()[static_cast<std::size_t>(m_cur.y)].wrapped = false;
    }
}

void Terminal::insert_chars(int count) {
    auto& cells = screen()[static_cast<std::size_t>(m_cur.y)].cells;
    count = std::min(count, m_cols - m_cur.x);
    if (count <= 0) return;
    fix_wide_at(m_cur.y, m_cur.x);
    fix_wide_at(m_cur.y, m_cols - count);
    cells.insert(cells.begin() + m_cur.x, static_cast<std::size_t>(count), blank());
    cells.resize(static_cast<std::size_t>(m_cols));
    if (cells.back().width == 2) cells.back() = blank();
    m_cur.pending_wrap = false;
    touch();
}

void Terminal::delete_chars(int count) {
    auto& cells = screen()[static_cast<std::size_t>(m_cur.y)].cells;
    count = std::min(count, m_cols - m_cur.x);
    if (count <= 0) return;
    fix_wide_at(m_cur.y, m_cur.x);
    fix_wide_at(m_cur.y, m_cur.x + count - 1);
    if (m_cur.x + count < m_cols) fix_wide_at(m_cur.y, m_cur.x + count);
    cells.erase(cells.begin() + m_cur.x, cells.begin() + m_cur.x + count);
    cells.insert(cells.end(), static_cast<std::size_t>(count), blank());
    if (m_cur.x < m_cols && cells[static_cast<std::size_t>(m_cur.x)].width == 0) cells[static_cast<std::size_t>(m_cur.x)] = blank();
    m_cur.pending_wrap = false;
    touch();
}

void Terminal::insert_lines(int count) {
    if (m_cur.y < m_top || m_cur.y > m_bottom) return;
    scroll_down(m_cur.y, m_bottom, count);
    m_cur.x = 0;
    m_cur.pending_wrap = false;
}

void Terminal::delete_lines(int count) {
    if (m_cur.y < m_top || m_cur.y > m_bottom) return;
    scroll_up(m_cur.y, m_bottom, count, false);
    m_cur.x = 0;
    m_cur.pending_wrap = false;
}

void Terminal::tab_forward(int count) {
    m_cur.pending_wrap = false;
    for (int i = 0; i < count; ++i) {
        int x = m_cur.x + 1;
        while (x < m_cols - 1 && !m_tabs[static_cast<std::size_t>(x)]) ++x;
        m_cur.x = std::min(x, m_cols - 1);
    }
}

void Terminal::tab_backward(int count) {
    m_cur.pending_wrap = false;
    for (int i = 0; i < count; ++i) {
        int x = m_cur.x - 1;
        while (x > 0 && !m_tabs[static_cast<std::size_t>(x)]) --x;
        m_cur.x = std::max(x, 0);
    }
}

void Terminal::save_cursor() {
    (m_alt_active ? m_saved_alt : m_saved_main) = m_cur;
}

void Terminal::restore_cursor() {
    const Cursor& saved = m_alt_active ? m_saved_alt : m_saved_main;
    m_cur = saved;
    clamp_cursor();
    touch();
}

void Terminal::enter_alternate(bool clear, bool save) {
    if (save) save_cursor();
    if (m_alt_active) {
        if (clear) {
            for (auto& l : m_alt) l = blank_line();
        }
        return;
    }
    m_alt_active = true;
    m_view = 0;
    if (clear) {
        for (auto& l : m_alt) l = blank_line();
    }
    touch();
}

void Terminal::leave_alternate(bool restore) {
    if (m_alt_active) {
        m_alt_active = false;
        touch();
    }
    if (restore) restore_cursor();
}

void Terminal::soft_reset() {
    m_cursor_visible = true;
    m_insert = false;
    m_cur.origin = false;
    m_cur.autowrap = true;
    m_cur.pen = TermCell{};
    m_cur.charsets = { { 'B', 'B', 'B', 'B' } };
    m_cur.gl = 0;
    m_cur.pending_wrap = false;
    m_top = 0;
    m_bottom = m_rows - 1;
    m_app_cursor = false;
    m_app_keypad = false;
    m_saved_main = Cursor{};
    m_saved_alt = Cursor{};
    touch();
}

void Terminal::escape_dispatch(unsigned char final) {
    if (m_intermediates.empty()) {
        switch (final) {
            case '7': save_cursor(); break;
            case '8': restore_cursor(); break;
            case 'D': line_feed(); break;
            case 'E':
                line_feed();
                carriage_return();
                break;
            case 'H':
                if (m_cur.x < m_cols) m_tabs[static_cast<std::size_t>(m_cur.x)] = true;
                break;
            case 'M': reverse_index(); break;
            case 'c': {
                const std::size_t keep = m_limit;
                reset();
                m_limit = keep;
                break;
            }
            case '=': m_app_keypad = true; break;
            case '>': m_app_keypad = false; break;
            case 'N': m_cur.single_shift = 2; break;
            case 'O': m_cur.single_shift = 3; break;
            case 'n': m_cur.gl = 2; break;
            case 'o': m_cur.gl = 3; break;
            default: break;
        }
        return;
    }
    const char i = m_intermediates[0];
    if (i == '(' || i == ')' || i == '*' || i == '+') {
        const int slot = i == '(' ? 0 : i == ')' ? 1 : i == '*' ? 2 : 3;
        m_cur.charsets[static_cast<std::size_t>(slot)] = static_cast<char>(final);
    } else if (i == '#' && final == '8') {
        for (auto& l : screen()) {
            for (auto& c : l.cells) {
                c = TermCell{};
                c.ch = U'E';
            }
            l.wrapped = false;
        }
        m_top = 0;
        m_bottom = m_rows - 1;
        m_cur.origin = false;
        move_to(0, 0);
    }
    touch();
}

void Terminal::sgr() {
    TermCell& pen = m_cur.pen;
    if (m_params.empty()) {
        pen.attrs = 0;
        pen.fg = TermColor{};
        pen.bg = TermColor{};
        return;
    }
    auto sub = [&](std::size_t i, std::size_t k) {
        const auto& s = m_subparams[i];
        return k < s.size() && s[k] > 0 ? s[k] : 0;
    };
    for (std::size_t i = 0; i < m_params.size(); ++i) {
        const int p = m_params[i];
        const bool has_sub = i < m_subparams.size() && !m_subparams[i].empty();
        if (p == 38 || p == 48 || p == 58) {
            TermColor color;
            bool ok = false;
            if (has_sub) {
                const int kind = sub(i, 0);
                const std::size_t n = m_subparams[i].size();
                if (kind == 5 && n >= 2) {
                    color = TermColor::indexed(std::clamp(sub(i, 1), 0, 255));
                    ok = true;
                } else if (kind == 2 && n >= 4) {
                    const std::size_t at = n >= 5 ? 2 : 1;
                    color = TermColor::rgb(std::clamp(sub(i, at), 0, 255), std::clamp(sub(i, at + 1), 0, 255), std::clamp(sub(i, at + 2), 0, 255));
                    ok = true;
                }
            } else if (i + 1 < m_params.size()) {
                const int kind = m_params[i + 1];
                if (kind == 5 && i + 2 < m_params.size()) {
                    color = TermColor::indexed(std::clamp(m_params[i + 2], 0, 255));
                    i += 2;
                    ok = true;
                } else if (kind == 2 && i + 4 < m_params.size()) {
                    color = TermColor::rgb(std::clamp(m_params[i + 2], 0, 255), std::clamp(m_params[i + 3], 0, 255), std::clamp(m_params[i + 4], 0, 255));
                    i += 4;
                    ok = true;
                } else {
                    ++i;
                }
            }
            if (ok && p == 38) pen.fg = color;
            else if (ok && p == 48) pen.bg = color;
            continue;
        }
        switch (p) {
            case 0:
                pen.attrs = 0;
                pen.fg = TermColor{};
                pen.bg = TermColor{};
                break;
            case 1: pen.attrs |= TermBold; break;
            case 2: pen.attrs |= TermDim; break;
            case 3: pen.attrs |= TermItalic; break;
            case 4: {
                pen.attrs &= static_cast<std::uint16_t>(~(TermUnderline | TermDoubleUnderline | TermCurlyUnderline));
                const int style = has_sub ? sub(i, 0) : 1;
                if (style == 1) pen.attrs |= TermUnderline;
                else if (style == 2) pen.attrs |= TermDoubleUnderline;
                else if (style >= 3) pen.attrs |= TermCurlyUnderline;
                break;
            }
            case 5:
            case 6: pen.attrs |= TermBlink; break;
            case 7: pen.attrs |= TermInverse; break;
            case 8: pen.attrs |= TermHidden; break;
            case 9: pen.attrs |= TermStrike; break;
            case 21:
                pen.attrs &= static_cast<std::uint16_t>(~(TermUnderline | TermCurlyUnderline));
                pen.attrs |= TermDoubleUnderline;
                break;
            case 22: pen.attrs &= static_cast<std::uint16_t>(~(TermBold | TermDim)); break;
            case 23: pen.attrs &= static_cast<std::uint16_t>(~TermItalic); break;
            case 24: pen.attrs &= static_cast<std::uint16_t>(~(TermUnderline | TermDoubleUnderline | TermCurlyUnderline)); break;
            case 25: pen.attrs &= static_cast<std::uint16_t>(~TermBlink); break;
            case 27: pen.attrs &= static_cast<std::uint16_t>(~TermInverse); break;
            case 28: pen.attrs &= static_cast<std::uint16_t>(~TermHidden); break;
            case 29: pen.attrs &= static_cast<std::uint16_t>(~TermStrike); break;
            case 39: pen.fg = TermColor{}; break;
            case 49: pen.bg = TermColor{}; break;
            case 53: pen.attrs |= TermOverline; break;
            case 55: pen.attrs &= static_cast<std::uint16_t>(~TermOverline); break;
            default:
                if (p >= 30 && p <= 37) pen.fg = TermColor::indexed(p - 30);
                else if (p >= 40 && p <= 47) pen.bg = TermColor::indexed(p - 40);
                else if (p >= 90 && p <= 97) pen.fg = TermColor::indexed(p - 90 + 8);
                else if (p >= 100 && p <= 107) pen.bg = TermColor::indexed(p - 100 + 8);
                break;
        }
    }
}

int Terminal::mode_state(int mode, bool priv) const {
    auto on = [](bool b) { return b ? 1 : 2; };
    if (!priv) {
        if (mode == 4) return on(m_insert);
        if (mode == 20) return on(m_newline_mode);
        return 0;
    }
    switch (mode) {
        case 1: return on(m_app_cursor);
        case 5: return on(m_reverse_video);
        case 6: return on(m_cur.origin);
        case 7: return on(m_cur.autowrap);
        case 12: return on(m_cursor_blink);
        case 25: return on(m_cursor_visible);
        case 47:
        case 1047:
        case 1049: return on(m_alt_active);
        case 66: return on(m_app_keypad);
        case 9: return on(m_mouse_mode == TermMouseMode::Press);
        case 1000: return on(m_mouse_mode == TermMouseMode::PressRelease);
        case 1002: return on(m_mouse_mode == TermMouseMode::ButtonMotion);
        case 1003: return on(m_mouse_mode == TermMouseMode::AnyMotion);
        case 1004: return on(m_focus_events);
        case 1005: return on(m_mouse_encoding == TermMouseEncoding::Utf8);
        case 1006: return on(m_mouse_encoding == TermMouseEncoding::Sgr);
        case 1007: return on(m_alternate_scroll);
        case 1015: return on(m_mouse_encoding == TermMouseEncoding::Urxvt);
        case 2004: return on(m_bracketed_paste);
        case 2026: return on(m_sync);
        default: return 0;
    }
}

void Terminal::set_mode(int mode, bool on, bool priv) {
    if (!priv) {
        if (mode == 4) m_insert = on;
        else if (mode == 20) m_newline_mode = on;
        return;
    }
    switch (mode) {
        case 1: m_app_cursor = on; break;
        case 3:
            m_top = 0;
            m_bottom = m_rows - 1;
            for (int r = 0; r < m_rows; ++r) {
                erase_cells(r, 0, m_cols);
                screen()[static_cast<std::size_t>(r)].wrapped = false;
            }
            m_cur.origin = false;
            move_to(0, 0);
            break;
        case 5:
            m_reverse_video = on;
            touch();
            break;
        case 6:
            m_cur.origin = on;
            move_to(0, 0);
            break;
        case 7:
            m_cur.autowrap = on;
            if (!on) m_cur.pending_wrap = false;
            break;
        case 12: m_cursor_blink = on; break;
        case 25:
            m_cursor_visible = on;
            touch();
            break;
        case 9: m_mouse_mode = on ? TermMouseMode::Press : TermMouseMode::None; break;
        case 1000: m_mouse_mode = on ? TermMouseMode::PressRelease : TermMouseMode::None; break;
        case 1002: m_mouse_mode = on ? TermMouseMode::ButtonMotion : TermMouseMode::None; break;
        case 1003: m_mouse_mode = on ? TermMouseMode::AnyMotion : TermMouseMode::None; break;
        case 1004: m_focus_events = on; break;
        case 1005: m_mouse_encoding = on ? TermMouseEncoding::Utf8 : TermMouseEncoding::Default; break;
        case 1006: m_mouse_encoding = on ? TermMouseEncoding::Sgr : TermMouseEncoding::Default; break;
        case 1015: m_mouse_encoding = on ? TermMouseEncoding::Urxvt : TermMouseEncoding::Default; break;
        case 1007: m_alternate_scroll = on; break;
        case 66: m_app_keypad = on; break;
        case 47:
            if (on) enter_alternate(false, false);
            else leave_alternate(false);
            break;
        case 1047:
            if (on) enter_alternate(false, false);
            else {
                if (m_alt_active) {
                    for (auto& l : m_alt) l = blank_line();
                }
                leave_alternate(false);
            }
            break;
        case 1048:
            if (on) save_cursor();
            else restore_cursor();
            break;
        case 1049:
            if (on) {
                if (!m_alt_active) {
                    m_saved_main = m_cur;
                    enter_alternate(true, false);
                    m_cur.pending_wrap = false;
                }
            } else if (m_alt_active) {
                leave_alternate(false);
                m_cur = m_saved_main;
                clamp_cursor();
                touch();
            }
            break;
        case 2004: m_bracketed_paste = on; break;
        case 2026: m_sync = on; break;
        default: break;
    }
}

void Terminal::csi_dispatch(unsigned char final) {
    if (m_prefix == '?') {
        if (!m_intermediates.empty()) {
            if (m_intermediates == "$" && final == 'p') {
                const int mode = param(0, 0);
                reply("\x1b[?" + std::to_string(mode) + ";" + std::to_string(mode_state(mode, true)) + "$y");
            }
            return;
        }
        switch (final) {
            case 'h':
            case 'l':
                for (int p : m_params) set_mode(p, final == 'h', true);
                break;
            case 'J': erase_display(param(0, 0)); break;
            case 'K': erase_line(param(0, 0)); break;
            case 'n':
                if (param(0, 0) == 6) {
                    const int row = m_cur.origin ? m_cur.y - m_top : m_cur.y;
                    reply("\x1b[?" + std::to_string(row + 1) + ";" + std::to_string(m_cur.x + 1) + "R");
                }
                break;
            default: break;
        }
        return;
    }
    if (m_prefix == '>') {
        if (final == 'c' && m_intermediates.empty()) reply("\x1b[>1;10;0c");
        else if (final == 'q' && m_intermediates.empty()) reply("\x1bP>|Grove terminal\x1b\\");
        return;
    }
    if (m_prefix == '=') {
        if (final == 'c' && m_intermediates.empty()) reply("\x1bP!|00000000\x1b\\");
        return;
    }
    if (m_prefix != 0) return;
    if (!m_intermediates.empty()) {
        if (m_intermediates == " " && final == 'q') {
            const int s = param(0, 0);
            m_cursor_blink = s == 0 || s == 1 || s == 3 || s == 5;
            m_cursor_shape = s <= 2 ? TermCursorShape::Block : s <= 4 ? TermCursorShape::Underline : TermCursorShape::Bar;
            touch();
        } else if (m_intermediates == "!" && final == 'p') {
            soft_reset();
        } else if (m_intermediates == "$" && final == 'p') {
            const int mode = param(0, 0);
            reply("\x1b[" + std::to_string(mode) + ";" + std::to_string(mode_state(mode, false)) + "$y");
        }
        return;
    }
    const int n = param(0, 1);
    switch (final) {
        case '@': insert_chars(n); break;
        case 'A': {
            const int limit = m_cur.y >= m_top ? m_top : 0;
            m_cur.y = std::max(limit, m_cur.y - n);
            m_cur.pending_wrap = false;
            touch();
            break;
        }
        case 'B':
        case 'e': {
            const int limit = m_cur.y <= m_bottom ? m_bottom : m_rows - 1;
            m_cur.y = std::min(limit, m_cur.y + n);
            m_cur.pending_wrap = false;
            touch();
            break;
        }
        case 'C':
        case 'a':
            m_cur.x = std::min(m_cols - 1, m_cur.x + n);
            m_cur.pending_wrap = false;
            touch();
            break;
        case 'D':
            m_cur.x = std::max(0, m_cur.x - n);
            m_cur.pending_wrap = false;
            touch();
            break;
        case 'E': {
            const int limit = m_cur.y <= m_bottom ? m_bottom : m_rows - 1;
            m_cur.y = std::min(limit, m_cur.y + n);
            carriage_return();
            touch();
            break;
        }
        case 'F': {
            const int limit = m_cur.y >= m_top ? m_top : 0;
            m_cur.y = std::max(limit, m_cur.y - n);
            carriage_return();
            touch();
            break;
        }
        case 'G':
        case '`':
            m_cur.x = std::clamp(n - 1, 0, m_cols - 1);
            m_cur.pending_wrap = false;
            touch();
            break;
        case 'H':
        case 'f':
            move_to(param(1, 1) - 1, param(0, 1) - 1);
            break;
        case 'I': tab_forward(n); break;
        case 'J': erase_display(param(0, 0)); break;
        case 'K': erase_line(param(0, 0)); break;
        case 'L': insert_lines(n); break;
        case 'M': delete_lines(n); break;
        case 'P': delete_chars(n); break;
        case 'S': scroll_up(m_top, m_bottom, n); break;
        case 'T':
            if (m_params.size() <= 1) scroll_down(m_top, m_bottom, n);
            break;
        case 'X': erase_cells(m_cur.y, m_cur.x, m_cur.x + n); break;
        case 'Z': tab_backward(n); break;
        case 'b':
            if (m_last_char != 0) {
                for (int i = 0; i < std::min(n, 65535); ++i) print(m_last_char);
            }
            break;
        case 'c':
            if (param(0, 0) == 0) reply("\x1b[?62;22c");
            break;
        case 'd':
            move_to(m_cur.x, n - 1);
            break;
        case 'g': {
            const int mode = param(0, 0);
            if (mode == 0 && m_cur.x < m_cols) m_tabs[static_cast<std::size_t>(m_cur.x)] = false;
            else if (mode == 3) std::fill(m_tabs.begin(), m_tabs.end(), false);
            break;
        }
        case 'h':
        case 'l':
            for (int p : m_params) set_mode(p, final == 'h', false);
            break;
        case 'm': sgr(); break;
        case 'n': {
            const int what = param(0, 0);
            if (what == 5) reply("\x1b[0n");
            else if (what == 6) {
                const int row = m_cur.origin ? m_cur.y - m_top : m_cur.y;
                reply("\x1b[" + std::to_string(row + 1) + ";" + std::to_string(m_cur.x + 1) + "R");
            }
            break;
        }
        case 'r': {
            const int top = param(0, 1) - 1;
            const int bottom = param(1, m_rows) - 1;
            if (top < bottom && bottom < m_rows) {
                m_top = top;
                m_bottom = bottom;
                move_to(0, 0);
            }
            break;
        }
        case 'x': {
            const int what = param(0, 0);
            if (what <= 1) reply("\x1b[" + std::to_string(what + 2) + ";1;1;128;128;1;0x");
            break;
        }
        case 's': save_cursor(); break;
        case 'u': restore_cursor(); break;
        case 't': {
            const int op = param(0, 0);
            if (op == 18) reply("\x1b[8;" + std::to_string(m_rows) + ";" + std::to_string(m_cols) + "t");
            else if (op == 14) reply("\x1b[4;" + std::to_string(m_rows * 16) + ";" + std::to_string(m_cols * 8) + "t");
            else if (op == 16) reply("\x1b[6;16;8t");
            break;
        }
        default: break;
    }
}

std::string Terminal::color_report(int which) const {
    const TermRgb c = which == 10 ? m_default_fg : which == 11 ? m_default_bg : m_cursor_color;
    return "\x1b]" + std::to_string(which) + ";rgb:" + hex4(c.r) + "/" + hex4(c.g) + "/" + hex4(c.b) + "\x1b\\";
}

void Terminal::osc_dispatch() {
    const std::size_t semi = m_string.find(';');
    const std::string head = m_string.substr(0, semi);
    const std::string rest = semi == std::string::npos ? std::string() : m_string.substr(semi + 1);
    if (head.empty() || head.find_first_not_of("0123456789") != std::string::npos) return;
    const int code = std::atoi(head.c_str());
    if (code == 0 || code == 2) {
        if (m_title != rest) {
            m_title = rest;
            m_title_changed = true;
        }
    } else if (code == 4) {
        std::size_t at = 0;
        while (at < rest.size()) {
            const std::size_t a = rest.find(';', at);
            if (a == std::string::npos) break;
            const std::size_t b = rest.find(';', a + 1);
            const int index = std::atoi(rest.substr(at, a - at).c_str());
            const std::string spec = rest.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            if (index >= 0 && index < 256) {
                if (spec == "?") {
                    const TermRgb c = m_palette[static_cast<std::size_t>(index)];
                    reply("\x1b]4;" + std::to_string(index) + ";rgb:" + hex4(c.r) + "/" + hex4(c.g) + "/" + hex4(c.b) + "\x1b\\");
                } else {
                    TermRgb c;
                    if (parse_color_spec(spec, c)) {
                        m_palette[static_cast<std::size_t>(index)] = c;
                        touch();
                    }
                }
            }
            if (b == std::string::npos) break;
            at = b + 1;
        }
    } else if (code == 7) {
        std::string url = rest;
        if (url.rfind("file://", 0) == 0) {
            const std::size_t slash = url.find('/', 7);
            url = slash == std::string::npos ? std::string() : url.substr(slash);
        }
        m_cwd = percent_decode(url);
    } else if (code >= 10 && code <= 12) {
        std::string spec = rest;
        int which = code;
        while (true) {
            const std::size_t s = spec.find(';');
            const std::string one = spec.substr(0, s);
            if (one == "?") reply(color_report(which));
            else {
                TermRgb c;
                if (parse_color_spec(one, c)) {
                    if (which == 10) m_default_fg = c;
                    else if (which == 11) m_default_bg = c;
                    else m_cursor_color = c;
                    touch();
                }
            }
            if (s == std::string::npos || which >= 12) break;
            spec = spec.substr(s + 1);
            ++which;
        }
    } else if (code == 104) {
        if (rest.empty()) reset_palette();
        else {
            const std::array<TermRgb, 256> was = m_palette;
            reset_palette();
            const std::array<TermRgb, 256> fresh = m_palette;
            m_palette = was;
            std::size_t at = 0;
            while (at <= rest.size()) {
                const std::size_t s = rest.find(';', at);
                const int index = std::atoi(rest.substr(at, s == std::string::npos ? std::string::npos : s - at).c_str());
                if (index >= 0 && index < 256) m_palette[static_cast<std::size_t>(index)] = fresh[static_cast<std::size_t>(index)];
                if (s == std::string::npos) break;
                at = s + 1;
            }
        }
        touch();
    } else if (code == 110) {
        m_default_fg = m_base_fg;
        touch();
    } else if (code == 111) {
        m_default_bg = m_base_bg;
        touch();
    } else if (code == 112) {
        m_cursor_color = m_base_cursor;
        touch();
    }
}

void Terminal::dcs_dispatch() {
    if (m_string.rfind("$q", 0) == 0) {
        const std::string what = m_string.substr(2);
        if (what == "m") reply("\x1bP1$r0m\x1b\\");
        else if (what == " q") {
            const int shape = m_cursor_shape == TermCursorShape::Block ? 1 : m_cursor_shape == TermCursorShape::Underline ? 3 : 5;
            reply("\x1bP1$r" + std::to_string(shape + (m_cursor_blink ? 0 : 1)) + " q\x1b\\");
        } else if (what == "r") {
            reply("\x1bP1$r" + std::to_string(m_top + 1) + ";" + std::to_string(m_bottom + 1) + "r\x1b\\");
        } else {
            reply("\x1bP0$r\x1b\\");
        }
    } else if (m_string.rfind("+q", 0) == 0) {
        reply("\x1bP0+r\x1b\\");
    }
}

void Terminal::resize(int columns, int rows) {
    columns = std::max(1, columns);
    rows = std::max(1, rows);
    if (columns == m_cols && rows == m_rows) return;
    auto fit_width = [&](std::vector<TermLine>& buffer) {
        for (auto& l : buffer) {
            if (static_cast<int>(l.cells.size()) > columns) {
                l.cells.resize(static_cast<std::size_t>(columns));
                if (!l.cells.empty() && l.cells.back().width == 2) l.cells.back() = TermCell{};
                l.wrapped = false;
            } else {
                l.cells.resize(static_cast<std::size_t>(columns), TermCell{});
            }
        }
    };
    m_cols = columns;
    fit_width(m_main);
    fit_width(m_alt);
    auto blank_row = [&]() {
        TermLine l;
        l.cells.assign(static_cast<std::size_t>(m_cols), TermCell{});
        return l;
    };
    auto is_blank = [](const TermLine& l) {
        for (const TermCell& c : l.cells) {
            if (c.ch != U' ' || c.bg.kind != TermColor::Kind::Default || c.attrs != 0) return false;
        }
        return true;
    };
    Cursor& main_cursor = m_alt_active ? m_saved_main : m_cur;
    const int old_rows = m_rows;
    if (rows < old_rows) {
        int excess = old_rows - rows;
        while (excess > 0 && static_cast<int>(m_main.size()) - 1 > main_cursor.y && is_blank(m_main.back())) {
            m_main.pop_back();
            --excess;
        }
        while (excess > 0) {
            push_history(std::move(m_main.front()));
            m_main.erase(m_main.begin());
            main_cursor.y = std::max(0, main_cursor.y - 1);
            --excess;
        }
        while (static_cast<int>(m_main.size()) > rows) m_main.pop_back();
        int alt_excess = static_cast<int>(m_alt.size()) - rows;
        Cursor& alt_cursor = m_alt_active ? m_cur : m_saved_alt;
        while (alt_excess > 0 && static_cast<int>(m_alt.size()) - 1 > alt_cursor.y) {
            m_alt.pop_back();
            --alt_excess;
        }
        while (alt_excess > 0) {
            m_alt.erase(m_alt.begin());
            alt_cursor.y = std::max(0, alt_cursor.y - 1);
            --alt_excess;
        }
    } else if (rows > old_rows) {
        int extra = rows - old_rows;
        while (extra > 0 && !m_history.empty() && !m_alt_active) {
            TermLine l = std::move(m_history.back());
            m_history.pop_back();
            l.cells.resize(static_cast<std::size_t>(m_cols), TermCell{});
            m_main.insert(m_main.begin(), std::move(l));
            ++main_cursor.y;
            --extra;
        }
        while (static_cast<int>(m_main.size()) < rows) m_main.push_back(blank_row());
        while (static_cast<int>(m_alt.size()) < rows) m_alt.push_back(blank_row());
    }
    m_rows = rows;
    m_top = 0;
    m_bottom = m_rows - 1;
    m_tabs.resize(static_cast<std::size_t>(m_cols), false);
    for (int i = m_tab_width; i < m_cols; i += m_tab_width) m_tabs[static_cast<std::size_t>(i)] = true;
    for (Cursor* c : { &m_cur, &m_saved_main, &m_saved_alt }) {
        c->x = std::clamp(c->x, 0, m_cols - 1);
        c->y = std::clamp(c->y, 0, m_rows - 1);
        c->pending_wrap = false;
    }
    m_view = std::clamp(m_view, 0, static_cast<int>(m_history.size()));
    clear_selection();
    touch();
}

std::string Terminal::take_reply() {
    std::string out;
    out.swap(m_reply);
    return out;
}

bool Terminal::take_changed() noexcept {
    const bool c = m_changed;
    m_changed = false;
    return c;
}

bool Terminal::take_bell() noexcept {
    const bool b = m_bell;
    m_bell = false;
    return b;
}

bool Terminal::take_title_changed() noexcept {
    const bool t = m_title_changed;
    m_title_changed = false;
    return t;
}

void Terminal::reset_palette() {
    static const TermRgb base[16] = {
        { 0, 0, 0 }, { 205, 49, 49 }, { 13, 188, 121 }, { 229, 229, 16 }, { 36, 114, 200 }, { 188, 63, 188 }, { 17, 168, 205 }, { 229, 229, 229 },
        { 102, 102, 102 }, { 241, 76, 76 }, { 35, 209, 139 }, { 245, 245, 67 }, { 59, 142, 234 }, { 214, 112, 214 }, { 41, 184, 219 }, { 229, 229, 229 },
    };
    for (int i = 0; i < 16; ++i) m_palette[static_cast<std::size_t>(i)] = base[i];
    static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
    for (int i = 0; i < 216; ++i) {
        m_palette[static_cast<std::size_t>(16 + i)] = TermRgb{ static_cast<std::uint8_t>(levels[i / 36]), static_cast<std::uint8_t>(levels[(i / 6) % 6]), static_cast<std::uint8_t>(levels[i % 6]) };
    }
    for (int i = 0; i < 24; ++i) {
        const auto v = static_cast<std::uint8_t>(8 + i * 10);
        m_palette[static_cast<std::size_t>(232 + i)] = TermRgb{ v, v, v };
    }
    touch();
}

void Terminal::set_palette(int index, TermRgb rgb) {
    if (index < 0 || index > 255) return;
    m_palette[static_cast<std::size_t>(index)] = rgb;
    touch();
}

TermRgb Terminal::palette(int index) const { return m_palette[static_cast<std::size_t>(std::clamp(index, 0, 255))]; }

void Terminal::set_default_colors(TermRgb foreground, TermRgb background, TermRgb cursor) {
    m_base_fg = m_default_fg = foreground;
    m_base_bg = m_default_bg = background;
    m_base_cursor = m_cursor_color = cursor;
    touch();
}

TermRgb Terminal::resolve(const TermColor& color, bool foreground) const {
    if (color.kind == TermColor::Kind::Indexed) return m_palette[color.index];
    if (color.kind == TermColor::Kind::Rgb) return TermRgb{ color.r, color.g, color.b };
    return foreground ? m_default_fg : m_default_bg;
}

std::string Terminal::encode_key(TermKey key, unsigned modifiers) const {
    const int m = 1 + ((modifiers & TermShift) ? 1 : 0) + ((modifiers & TermAlt) ? 2 : 0) + ((modifiers & TermCtrl) ? 4 : 0) + ((modifiers & TermMeta) ? 8 : 0);
    const std::string mods = std::to_string(m);
    auto cursor = [&](char c) {
        if (m > 1) return "\x1b[1;" + mods + c;
        return std::string(m_app_cursor ? "\x1bO" : "\x1b[") + c;
    };
    auto tilde = [&](int n) { return "\x1b[" + std::to_string(n) + (m > 1 ? ";" + mods : std::string()) + "~"; };
    auto ss3 = [&](char c) {
        if (m > 1) return "\x1b[1;" + mods + c;
        return std::string("\x1bO") + c;
    };
    const std::string alt = (modifiers & TermAlt) ? "\x1b" : "";
    switch (key) {
        case TermKey::Up: return cursor('A');
        case TermKey::Down: return cursor('B');
        case TermKey::Right: return cursor('C');
        case TermKey::Left: return cursor('D');
        case TermKey::Home: return cursor('H');
        case TermKey::End: return cursor('F');
        case TermKey::Insert: return tilde(2);
        case TermKey::Delete: return tilde(3);
        case TermKey::PageUp: return tilde(5);
        case TermKey::PageDown: return tilde(6);
        case TermKey::F1: return ss3('P');
        case TermKey::F2: return ss3('Q');
        case TermKey::F3: return ss3('R');
        case TermKey::F4: return ss3('S');
        case TermKey::F5: return tilde(15);
        case TermKey::F6: return tilde(17);
        case TermKey::F7: return tilde(18);
        case TermKey::F8: return tilde(19);
        case TermKey::F9: return tilde(20);
        case TermKey::F10: return tilde(21);
        case TermKey::F11: return tilde(23);
        case TermKey::F12: return tilde(24);
        case TermKey::Backspace:
            if (modifiers & TermCtrl) return alt + "\x08";
            return alt + "\x7f";
        case TermKey::Tab:
            if (modifiers & TermShift) return "\x1b[Z";
            return alt + "\t";
        case TermKey::Enter: return alt + (m_newline_mode ? "\r\n" : "\r");
        case TermKey::KeypadEnter:
            if (m_app_keypad) return "\x1bOM";
            return alt + (m_newline_mode ? "\r\n" : "\r");
        case TermKey::Escape: return alt + "\x1b";
        case TermKey::None: break;
    }
    return std::string();
}

std::string Terminal::encode_char(char32_t c, unsigned modifiers) const {
    std::string out;
    if (modifiers & TermAlt) out += '\x1b';
    if (modifiers & TermCtrl) {
        char32_t k = c;
        if (k >= U'A' && k <= U'Z') k = k - U'A' + U'a';
        if (k >= U'a' && k <= U'z') {
            out += static_cast<char>(k - U'a' + 1);
            return out;
        }
        switch (k) {
            case U' ':
            case U'@':
            case U'2': out += '\0'; return out;
            case U'[':
            case U'3': out += '\x1b'; return out;
            case U'\\':
            case U'4': out += '\x1c'; return out;
            case U']':
            case U'5': out += '\x1d'; return out;
            case U'^':
            case U'6':
            case U'~': out += '\x1e'; return out;
            case U'_':
            case U'7':
            case U'-':
            case U'/': out += '\x1f'; return out;
            case U'8':
            case U'?': out += '\x7f'; return out;
            default: break;
        }
    }
    append_utf8(out, c);
    return out;
}

std::string Terminal::encode_text(std::string_view text) const { return std::string(text); }

std::string Terminal::encode_paste(std::string_view text) const {
    std::string body;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        if (c == '\n') body += '\r';
        else if (c == '\x1b' && m_bracketed_paste) continue;
        else body += c;
    }
    if (m_bracketed_paste) return "\x1b[200~" + body + "\x1b[201~";
    return body;
}

std::string Terminal::encode_mouse(const TermMouse& mouse) const {
    if (m_mouse_mode == TermMouseMode::None) return std::string();
    const bool wheel = mouse.button >= 64;
    if (m_mouse_mode == TermMouseMode::Press && (!mouse.press || mouse.motion)) return std::string();
    if (mouse.motion) {
        if (m_mouse_mode == TermMouseMode::PressRelease || m_mouse_mode == TermMouseMode::Press) return std::string();
        if (m_mouse_mode == TermMouseMode::ButtonMotion && mouse.button == 3) return std::string();
    }
    if (!mouse.press && wheel) return std::string();
    int code = mouse.button;
    if (m_mouse_mode != TermMouseMode::Press) {
        if (mouse.modifiers & TermShift) code += 4;
        if (mouse.modifiers & (TermAlt | TermMeta)) code += 8;
        if (mouse.modifiers & TermCtrl) code += 16;
    }
    if (mouse.motion) code += 32;
    const int x = mouse.column + 1;
    const int y = mouse.row + 1;
    if (m_mouse_encoding == TermMouseEncoding::Sgr) {
        return "\x1b[<" + std::to_string(code) + ";" + std::to_string(x) + ";" + std::to_string(y) + (mouse.press ? "M" : "m");
    }
    const int legacy = mouse.press || mouse.motion ? code : ((code & ~3) | 3);
    if (m_mouse_encoding == TermMouseEncoding::Urxvt) {
        return "\x1b[" + std::to_string(legacy + 32) + ";" + std::to_string(x) + ";" + std::to_string(y) + "M";
    }
    std::string out = "\x1b[M";
    out += static_cast<char>(32 + legacy);
    if (m_mouse_encoding == TermMouseEncoding::Utf8) {
        if (x + 32 > 2047 || y + 32 > 2047) return std::string();
        append_utf8(out, static_cast<char32_t>(32 + x));
        append_utf8(out, static_cast<char32_t>(32 + y));
        return out;
    }
    if (x > 223 || y > 223) return std::string();
    out += static_cast<char>(32 + x);
    out += static_cast<char>(32 + y);
    return out;
}

std::string Terminal::encode_wheel(bool up, int column, int row, unsigned modifiers) const {
    if (m_mouse_mode != TermMouseMode::None) {
        TermMouse m;
        m.button = up ? 64 : 65;
        m.press = true;
        m.column = column;
        m.row = row;
        m.modifiers = modifiers;
        return encode_mouse(m);
    }
    if (m_alt_active && m_alternate_scroll) {
        const std::string one = up ? (m_app_cursor ? "\x1bOA" : "\x1b[A") : (m_app_cursor ? "\x1bOB" : "\x1b[B");
        return one + one + one;
    }
    return std::string();
}

std::string Terminal::encode_focus(bool focused) const {
    if (!m_focus_events) return std::string();
    return focused ? "\x1b[I" : "\x1b[O";
}

void Terminal::select(TermPoint from, TermPoint to, bool block) {
    m_sel_active = true;
    m_sel_block = block;
    m_sel_from = from;
    m_sel_to = to;
    touch();
}

void Terminal::clear_selection() noexcept {
    if (m_sel_active) m_changed = true;
    m_sel_active = false;
    m_sel_block = false;
    m_sel_from = TermPoint{};
    m_sel_to = TermPoint{};
}

void Terminal::select_word(TermPoint at) {
    const TermLine& l = absolute_line(at.line);
    if (l.cells.empty()) return;
    int col = std::clamp(at.column, 0, static_cast<int>(l.cells.size()) - 1);
    if (l.cells[static_cast<std::size_t>(col)].width == 0 && col > 0) --col;
    const bool word = word_char(l.cells[static_cast<std::size_t>(col)].ch);
    auto same = [&](int c) {
        const TermCell& cell = l.cells[static_cast<std::size_t>(c)];
        if (cell.width == 0) return true;
        return word ? word_char(cell.ch) : cell.ch == l.cells[static_cast<std::size_t>(col)].ch;
    };
    int a = col;
    while (a > 0 && same(a - 1)) --a;
    int b = col;
    while (b + 1 < static_cast<int>(l.cells.size()) && same(b + 1)) ++b;
    select(TermPoint{ at.line, a }, TermPoint{ at.line, b + 1 });
}

void Terminal::select_line(TermPoint at) {
    long long first = at.line;
    while (first > 0 && absolute_line(first - 1).wrapped) --first;
    long long last = at.line;
    while (absolute_line(last).wrapped && last + 1 < total_lines()) ++last;
    select(TermPoint{ first, 0 }, TermPoint{ last, m_cols });
}

void Terminal::select_all() { select(TermPoint{ 0, 0 }, TermPoint{ total_lines() - 1, m_cols }); }

bool Terminal::is_selected(long long line, int column) const noexcept {
    if (!m_sel_active) return false;
    TermPoint a = m_sel_from;
    TermPoint b = m_sel_to;
    if (b < a) std::swap(a, b);
    if (line < a.line || line > b.line) return false;
    if (m_sel_block) {
        const int lo = std::min(m_sel_from.column, m_sel_to.column);
        const int hi = std::max(m_sel_from.column, m_sel_to.column);
        return column >= lo && column < hi;
    }
    const TermPoint p{ line, column };
    return !(p < a) && p < b;
}

std::string Terminal::selected_text() const {
    if (!has_selection()) return std::string();
    TermPoint a = m_sel_from;
    TermPoint b = m_sel_to;
    if (b < a) std::swap(a, b);
    std::string out;
    for (long long l = a.line; l <= b.line; ++l) {
        const TermLine& line = absolute_line(l);
        int from = 0;
        int to = static_cast<int>(line.cells.size());
        if (m_sel_block) {
            from = std::min(m_sel_from.column, m_sel_to.column);
            to = std::max(m_sel_from.column, m_sel_to.column);
        } else {
            if (l == a.line) from = a.column;
            if (l == b.line) to = std::min(to, b.column);
        }
        std::string text;
        for (int c = std::max(0, from); c < std::min(to, static_cast<int>(line.cells.size())); ++c) {
            const TermCell& cell = line.cells[static_cast<std::size_t>(c)];
            if (cell.width == 0) continue;
            append_utf8(text, cell.ch);
            for (char32_t mark : cell.marks) append_utf8(text, mark);
        }
        const bool joined = !m_sel_block && line.wrapped && l != b.line;
        if (!joined) {
            while (!text.empty() && text.back() == ' ') text.pop_back();
        }
        out += text;
        if (l != b.line && !joined) out += '\n';
    }
    return out;
}

std::string Terminal::row_text(int row) const {
    std::string out;
    for (const TermCell& c : line(row).cells) {
        if (c.width == 0) continue;
        append_utf8(out, c.ch);
        for (char32_t mark : c.marks) append_utf8(out, mark);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string Terminal::screen_text() const {
    std::string out;
    for (int r = 0; r < m_rows; ++r) {
        out += row_text(r);
        out += '\n';
    }
    return out;
}

bool Terminal::send(const std::string& bytes) {
    if (bytes.empty() || !m_input) return false;
    m_input(bytes);
    return true;
}

std::shared_ptr<Terminal> terminal_of(Document& document, const Node& element) {
    auto& map = document.host().terminals;
    auto it = map.find(&element);
    if (it == map.end()) {
        it = map.emplace(&element, std::make_shared<Terminal>()).first;
        const std::string tab = element.attribute_or("tab-size", "");
        if (!tab.empty()) {
            char* end = nullptr;
            const long n = std::strtol(tab.c_str(), &end, 10);
            if (end != tab.c_str() && n > 0) it->second->set_tab_width(static_cast<int>(n));
        }
    }
    return it->second;
}

std::shared_ptr<Terminal> find_terminal(const Document& document, const Node& element) {
    const auto& map = document.host().terminals;
    const auto it = map.find(&element);
    return it == map.end() ? nullptr : it->second;
}

void forget_terminal(Document& document, const Node& element) { document.host().terminals.erase(&element); }

} // namespace grove
