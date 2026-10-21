import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
API = os.path.join(ROOT, 'Include', 'grove', 'api')

def read_preamble():
    text = open(os.path.join(API, 'grove_api.h')).read()
    cut = text.find('\nGROVE_API ')
    return text[:cut + 1] if cut >= 0 else text

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
    preamble = read_preamble()
    open(os.path.join(API, 'grove_api.h'), 'w').write(preamble + decls + EPILOGUE)
    rows = ['    X(%s, %s, (%s), (%s))' % (r, n, p, arg_names(p)) for r, n, p in spec]
    body = ' \\\n'.join(rows)
    text = '#ifndef GROVE_API_LIST_H\n#define GROVE_API_LIST_H\n\n#include "grove_api.h"\n\n#define GROVE_API_FUNCTIONS(X) \\\n' + body + '\n\n#endif // GROVE_API_LIST_H\n'
    open(os.path.join(API, 'grove_api_list.h'), 'w').write(text)
    print('%d functions' % len(spec))


if __name__ == '__main__':
    main()
