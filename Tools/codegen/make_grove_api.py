import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
API = os.path.join(ROOT, 'Include', 'grove', 'api')

PREAMBLE = '#ifndef GROVE_API_H\n#define GROVE_API_H\n\n#include <stddef.h>\n#include <stdint.h>\n#include <stdbool.h>\n\n#if defined(_WIN32) && defined(GROVE_API_SHARED)\n#if defined(GROVE_API_BUILDING)\n#define GROVE_API __declspec(dllexport)\n#else\n#define GROVE_API __declspec(dllimport)\n#endif\n#else\n#define GROVE_API\n#endif\n\n#ifdef __cplusplus\nextern "C" {\n#endif\n\ntypedef struct grove_document grove_document;\ntypedef struct grove_element grove_element;\ntypedef struct grove_event grove_event;\n\ntypedef void (*grove_entry_fn)(grove_document* document);\ntypedef void (*grove_listener_fn)(grove_event* event, void* user_data);\ntypedef void (*grove_timer_fn)(void* user_data);\ntypedef void (*grove_release_fn)(void* user_data);\n\ntypedef enum grove_input_kind {\n    GROVE_INPUT_NONE = 0,\n    GROVE_INPUT_INTEGER = 1,\n    GROVE_INPUT_FLOAT = 2,\n    GROVE_INPUT_STRING = 3,\n    GROVE_INPUT_BOOL = 4,\n    GROVE_INPUT_RADIO = 5,\n    GROVE_INPUT_SLIDER = 6,\n    GROVE_INPUT_COLOR = 7\n} grove_input_kind;\n\ntypedef enum grove_modifier {\n    GROVE_MOD_SHIFT = 1,\n    GROVE_MOD_CONTROL = 2,\n    GROVE_MOD_ALT = 4,\n    GROVE_MOD_META = 8\n} grove_modifier;\n\n'

EPILOGUE = '\n#ifdef __cplusplus\n}\n#endif\n\n#endif // GROVE_API_H\n'


def read_spec(path):
    out = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        ret, name, params = line.split('|')
        out.append((ret, name, params))
    return out


def arg_names(params):
    if params.strip() == 'void':
        return ''
    names = []
    for p in params.split(','):
        names.append(p.strip().replace('*', ' ').split()[-1])
    return ', '.join(names)


def main():
    spec = read_spec(os.path.join(HERE, 'grove_api.spec'))
    decls = ''.join('GROVE_API %s %s(%s);\n' % (r, n, p) for r, n, p in spec)
    open(os.path.join(API, 'grove_api.h'), 'w').write(PREAMBLE + decls + EPILOGUE)
    rows = ['    X(%s, %s, (%s), (%s))' % (r, n, p, arg_names(p)) for r, n, p in spec]
    body = ' \\\n'.join(rows)
    text = '#ifndef GROVE_API_LIST_H\n#define GROVE_API_LIST_H\n\n#include "grove_api.h"\n\n#define GROVE_API_FUNCTIONS(X) \\\n' + body + '\n\n#endif // GROVE_API_LIST_H\n'
    open(os.path.join(API, 'grove_api_list.h'), 'w').write(text)
    print('%d functions' % len(spec))


if __name__ == '__main__':
    main()
