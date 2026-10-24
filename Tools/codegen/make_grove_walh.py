import sys

S = 'const string&'

ELEMENT = [
    ('exists', [], 'bool', True),
    ('tag', [], 'string', True),
    ('id', [], 'string', True),
    ('has_attribute', [(S, 'name')], 'bool', True),
    ('get_attribute', [(S, 'name')], 'string', True),
    ('set_attribute', [(S, 'name'), (S, 'value')], 'void', False),
    ('remove_attribute', [(S, 'name')], 'void', False),
    ('inner_text', [], 'string', True),
    ('set_inner_text', [(S, 'value')], 'void', False),
    ('has_class', [(S, 'name')], 'bool', True),
    ('add_class', [(S, 'name')], 'void', False),
    ('remove_class', [(S, 'name')], 'void', False),
    ('toggle_class', [(S, 'name')], 'bool', False),
    ('style', [(S, 'property')], 'string', True),
    ('set_style', [(S, 'property'), (S, 'value')], 'void', False),
    ('remove_style', [(S, 'property')], 'void', False),
    ('computed_style', [(S, 'property')], 'string', True),
    ('parent', [], 'Element', True),
    ('child_count', [], 'long int', True),
    ('child', [('long int', 'index')], 'Element', True),
    ('index', [], 'long int', True),
    ('next_sibling', [], 'Element', True),
    ('previous_sibling', [], 'Element', True),
    ('append_child', [('const Element&', 'child')], 'bool', False),
    ('insert_before', [('const Element&', 'child'), ('const Element&', 'reference')], 'bool', False),
    ('remove', [], 'bool', False),
    ('is_attached', [], 'bool', True),
    ('query', [(S, 'selector')], 'Element', True),
    ('listen', [(S, 'type'), ('function() -> void', 'fn')], 'long int', False),
    ('remove_event_listener', [('long int', 'id')], 'bool', False),
    ('dispatch', [(S, 'type')], 'bool', False),
    ('focus', [], 'bool', False),
    ('hovered', [], 'bool', True),
    ('focused', [], 'bool', True),
    ('active', [], 'bool', True),
    ('input_kind', [], 'long int', True),
    ('value_integer', [], 'long int', True),
    ('value_float', [], 'short float', True),
    ('value_bool', [], 'bool', True),
    ('value_string', [], 'string', True),
    ('value_text', [], 'string', True),
    ('value_color', [], 'string', True),
    ('set_value_integer', [('long int', 'value')], 'bool', False),
    ('set_value_float', [('short float', 'value')], 'bool', False),
    ('set_value_bool', [('bool', 'value')], 'bool', False),
    ('set_value_string', [(S, 'value')], 'bool', False),
    ('set_value_color', [(S, 'hex')], 'bool', False),
    ('window', [], 'Element', True),
    ('open', [], 'bool', False),
    ('close', [], 'bool', False),
    ('is_open', [], 'bool', True),
    ('window_width', [], 'short float', True),
    ('window_height', [], 'short float', True),
    ('resize_window', [('short float', 'width'), ('short float', 'height')], 'bool', False),
    ('move_window', [('short float', 'x'), ('short float', 'y')], 'bool', False),
    ('focus_window', [], 'bool', False),
    ('answer', [('bool', 'ok')], 'bool', False),
    ('answer_value', [], 'string', True),
    ('show_page', [], 'bool', False),
    ('open_modal', [], 'bool', False),
    ('close_modal', [], 'bool', False),
    ('is_current', [], 'bool', True),
]

LIST = [
    ('size', [], 'long int', True),
    ('empty', [], 'bool', True),
    ('at', [('long int', 'index')], 'Element', True),
    ('first', [], 'Element', True),
    ('last', [], 'Element', True),
]

EVENT = [
    ('exists', [], 'bool', True),
    ('type', [], 'string', True),
    ('target', [], 'Element', True),
    ('current_target', [], 'Element', True),
    ('x', [], 'short float', True),
    ('y', [], 'short float', True),
    ('delta_x', [], 'short float', True),
    ('delta_y', [], 'short float', True),
    ('button', [], 'long int', True),
    ('key', [], 'string', True),
    ('input_text', [], 'string', True, 'text'),
    ('shift', [], 'bool', True),
    ('ctrl', [], 'bool', True),
    ('alt', [], 'bool', True),
    ('meta', [], 'bool', True),
    ('stop_propagation', [], 'void', False),
    ('prevent_default', [], 'void', False),
    ('default_prevented', [], 'bool', True),
]

DOCUMENT = [
    ('root', [], 'Element', True),
    ('body', [], 'Element', True),
    ('get_by_id', [(S, 'id')], 'Element', True),
    ('get_by_class', [(S, 'name')], 'ElementList', True),
    ('get_by_tag', [(S, 'tag')], 'ElementList', True),
    ('id_selector', [(S, 'id')], 'Element', True),
    ('class_selector', [(S, 'name')], 'ElementList', True),
    ('tag_selector', [(S, 'tag')], 'ElementList', True),
    ('query', [(S, 'selector')], 'Element', True),
    ('query_all', [(S, 'selector')], 'ElementList', True),
    ('create_element', [(S, 'tag')], 'Element', True),
    ('choice', [(S, 'group')], 'string', True),
    ('set_choice', [(S, 'group'), (S, 'value')], 'bool', True),
    ('listen', [(S, 'type'), ('function() -> void', 'fn')], 'long int', True),
    ('remove_event_listener', [('long int', 'id')], 'bool', True),
    ('dispatch', [(S, 'type')], 'bool', True),
    ('set_timeout', [('short float', 'milliseconds'), ('function() -> void', 'fn')], 'long int', True),
    ('set_interval', [('short float', 'milliseconds'), ('function() -> void', 'fn')], 'long int', True),
    ('clear_timer', [('long int', 'id')], 'bool', True),
    ('main_window', [], 'Element', True),
    ('has_native', [(S, 'name')], 'bool', True),
    ('call', [(S, 'name'), (S, 'argument')], 'string', True),
    ('choose_file', [(S, 'mode'), (S, 'title'), (S, 'filter')], 'string', True),
    ('navigate', [(S, 'target')], 'bool', True),
    ('go_back', [], 'bool', True),
    ('popup', [(S, 'kind'), (S, 'title'), (S, 'message'), (S, 'input'), (S, 'value'), (S, 'options')], 'Element', True),
    ('popup_info', [(S, 'title'), (S, 'message')], 'Element', True),
    ('popup_warning', [(S, 'title'), (S, 'message')], 'Element', True),
    ('popup_error', [(S, 'title'), (S, 'message')], 'Element', True),
    ('alert', [(S, 'message')], 'Element', True),
    ('warning', [(S, 'message')], 'Element', True),
    ('error', [(S, 'message')], 'Element', True),
    ('confirm', [(S, 'title'), (S, 'message'), ('function(bool) -> void', 'fn')], 'Element', True),
    ('prompt_string', [(S, 'title'), (S, 'message'), (S, 'value'), ('function(string) -> void', 'fn')], 'Element', True),
    ('prompt_integer', [(S, 'title'), (S, 'message'), ('long int', 'value'), ('function(long int) -> void', 'fn')], 'Element', True),
    ('prompt_float', [(S, 'title'), (S, 'message'), ('short float', 'value'), ('function(short float) -> void', 'fn')], 'Element', True),
    ('prompt_bool', [(S, 'title'), (S, 'message'), ('bool', 'value'), ('function(bool) -> void', 'fn')], 'Element', True),
    ('prompt_color', [(S, 'title'), (S, 'message'), (S, 'value'), ('function(string) -> void', 'fn')], 'Element', True),
    ('prompt_choice', [(S, 'title'), (S, 'message'), (S, 'options'), (S, 'value'), ('function(string) -> void', 'fn')], 'Element', True),
    ('prompt_slider', [(S, 'title'), (S, 'message'), ('short float', 'value'), ('short float', 'min'), ('short float', 'max'), ('function(short float) -> void', 'fn')], 'Element', True),
    ('time_ms', [], 'short float', True),
    ('width', [], 'short float', True),
    ('height', [], 'short float', True),
    ('quit', [], 'void', True),
    ('set_max_fps', [('short float', 'fps')], 'void', True),
    ('max_fps', [], 'short float', True),
    ('set_idle_fps', [('short float', 'fps')], 'void', True),
    ('idle_fps', [], 'short float', True),
    ('set_vsync', [('bool', 'enabled')], 'void', True),
    ('vsync', [], 'bool', True),
    ('log', [(S, 'message')], 'void', True),
]

NATIVE = {'Element': 'NativeElement', 'ElementList': 'NativeElementList', 'Event': 'NativeEvent', 'Document': 'NativeDocument'}
CXX = {'NativeElement': 'grove::Element', 'NativeElementList': 'grove::ElementList', 'NativeEvent': 'grove::Event', 'NativeDocument': 'grove::Document'}


def native_type(t):
    for k, v in NATIVE.items():
        if t == k:
            return v
        if t == 'const ' + k + '&':
            return 'const ' + v + '&'
    return t


def params_text(params, native):
    return ', '.join((native_type(t) if native else t) + ': ' + n for t, n in params)


def native_decl(m):
    name, params, ret, const = m[0], m[1], m[2], m[3]
    alias = m[4] if len(m) > 4 else None
    q = ' const' if const else ''
    as_part = ' as "' + alias + '"' if alias else ''
    return '        function ' + name + '(' + params_text(params, True) + ')' + q + ' -> ' + native_type(ret) + as_part + ';'


def call_args(params):
    out = []
    for t, n in params:
        out.append(n + '.raw' if t in ('const Element&',) else n)
    return ', '.join(out)


def wrapper(m, native_expr=None):
    name, params, ret, const = m[0], m[1], m[2], m[3]
    q = ' const' if const else ''
    call = native_expr or ('raw.' + name + '(' + call_args(params) + ')')
    head = '    function ' + name + '(' + params_text(params, False) + ')' + q + ' -> ' + ret + ' {'
    if ret == 'void':
        body = '        ' + call + ';'
    elif ret in NATIVE:
        body = '        ' + ret + ': out;\n        out.raw = ' + call + ';\n        return out;'
    else:
        body = '        return ' + call + ';'
    return head + '\n' + body + '\n    } end function ' + name


def listener_adapters(holder):
    return '''    function add_event_listener(const string&: type, function(Event&) -> void: fn){c} -> long int {{
        return raw.listen(type, [fn]() -> void {{
            Event: e = Event::current();
            fn(e);
        }});
    }} end function add_event_listener

    function on(const string&: type, function(Event&) -> void: fn){c} -> long int {{
        return add_event_listener(type, fn);
    }} end function on'''.format(c=' const' if holder == 'Document' else '')


out = []
out.append('extern "C++" from "<grove/dom.hpp>" {')
for native, methods, extra in (
    ('NativeElement', ELEMENT, ['        operator(==) function(const NativeElement&: other) const -> bool;']),
    ('NativeElementList', LIST, ['        static function children_of(const NativeElement&: element) -> NativeElementList;', '        static function query_all_in(const NativeElement&: element, const string&: selector) -> NativeElementList;']),
    ('NativeEvent', EVENT, ['        static function current() -> NativeEvent;']),
    ('NativeDocument', DOCUMENT, []),
):
    out.append('    class ' + native + ' as "' + CXX[native] + '" {')
    out.append('    public:')
    out.extend(extra)
    for m in methods:
        out.append(native_decl(m))
    out.append('    } end class ' + native)
    out.append('')
out[-1] = '} end extern'
out.append('')
out.append('class Element;')
out.append('class ElementList;')
out.append('class Event;')
out.append('')

out.append('class Element {')
out.append('public:')
out.append('    NativeElement: raw;')
out.append('')
for m in ELEMENT:
    out.append(wrapper(m))
    out.append('')
out.append(wrapper(('children', [], 'ElementList', True), 'NativeElementList::children_of(raw)'))
out.append('')
out.append(wrapper(('query_all', [(S, 'selector')], 'ElementList', True), 'NativeElementList::query_all_in(raw, selector)'))
out.append('')
out.append(listener_adapters('Element'))
out.append('')
out.append('''    operator(==) function(const Element&: other) const -> bool {
        return raw == other.raw;
    }''')
out.append('} end class Element')
out.append('')

out.append('class ElementList {')
out.append('public:')
out.append('    NativeElementList: raw;')
out.append('')
for m in LIST:
    out.append(wrapper(m))
    out.append('')
out[-1] = '} end class ElementList'
out.append('')

out.append('class Event {')
out.append('public:')
out.append('    NativeEvent: raw;')
out.append('')
out.append('''    static function current() -> Event {
        Event: out;
        out.raw = NativeEvent::current();
        return out;
    } end function current''')
out.append('')
for m in EVENT:
    out.append(wrapper(m[:4]))
    out.append('')
out[-1] = '} end class Event'
out.append('')

out.append('class Document {')
out.append('public:')
out.append('    NativeDocument: raw;')
out.append('')
for m in DOCUMENT:
    out.append(wrapper(m))
    out.append('')
out.append(listener_adapters('Document'))
out.append('} end class Document')
out.append('')
out.append('function document() -> Document {')
out.append('    Document: d;')
out.append('    return d;')
out.append('} end function document')
out.append('')
for m in DOCUMENT:
    name, params, ret = m[0], m[1], m[2]
    if name in ('root', 'listen'):
        continue
    call = 'd.' + name + '(' + ', '.join(n for t, n in params) + ')'
    body = '    Document: d;\n    ' + ('' if ret == 'void' else 'return ') + call + ';'
    out.append('function ' + name + '(' + params_text(params, False) + ') -> ' + ret + ' {\n' + body + '\n} end function ' + name)
    out.append('')
for name in ('add_event_listener', 'on'):
    out.append('function ' + name + '(const string&: type, function(Event&) -> void: fn) -> long int {\n    Document: d;\n    return d.' + name + '(type, fn);\n} end function ' + name)
    out.append('')
out.append('export this')

open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
