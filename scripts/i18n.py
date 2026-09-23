"""Переклад інтерфейсу: звіряє src/core/util/i18n_en.inc з рядками, які код передає в tr()/trf().

Ключ перекладу — сам український текст (як у gettext). Команди:

  python scripts/i18n.py check          показати рядки без перекладу, зайві переклади і невідповідні
                                        плейсхолдери ({} і %d); код виходу 1, якщо є пропуски чи помилки
  python scripts/i18n.py todo [файл]    записати рядки без перекладу в JSON {"український": ""}
                                        (типово i18n_todo.json) — заповнити англійські значення
  python scripts/i18n.py merge <файл>   додати переклади з такого JSON і перегенерувати i18n_en.inc
                                        (порядок — як у коді, переклади рядків, яких уже немає, прибираються)

Що вважається ключем: перший аргумент-літерал tr(...)/trf(...) (сусідні літерали склеюються,
суфікс ImGui "##id" відкидається), усі кириличні літерали в src/gui/app_ui.hpp (таблиці пресетів, кодеків)
і масиви kCheckNames/kCheckHints у src/core/render/jobs.cpp.
"""
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'src')
INC = os.path.join(SRC, 'core', 'util', 'i18n_en.inc')

CYR = re.compile('[Ѐ-ӿ]')
IDENT = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')
BRACES = re.compile(r'\{[^{}]*\}')
PRINTF = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|L|z|j|t)?[diouxXeEfgGcspn%]')


# ---------- мінімальний лексер C++: рядкові літерали, коментарі, ідентифікатори ----------

def lex(src):
    """Токени (kind, text); kind: str, ws, comment, pp, char, id, num, punct."""
    i, n = 0, len(src)
    out = []
    line_start = True
    while i < n:
        c = src[i]
        if c in ' \t\r\n':
            j = i
            while j < n and src[j] in ' \t\r\n':
                line_start = line_start or src[j] == '\n'
                j += 1
            out.append(('ws', src[i:j]))
            i = j
            continue
        if c == '#' and line_start:
            j = i
            while j < n and not (src[j] == '\n' and src[j - 1] != '\\'):
                j += 1
            out.append(('pp', src[i:j]))
            i = j
            continue
        line_start = False
        if src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(('comment', src[i:j]))
            i = j
            continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(('comment', src[i:j]))
            i = j
            continue
        m = re.match(r'(u8|u|U|L)?R"([^(\s]*)\(', src[i:i + 40])
        if m:
            j = src.find(')' + m.group(2) + '"', i + m.end()) + len(m.group(2)) + 2
            out.append(('str', src[i:j]))
            i = j
            continue
        m = re.match(r'(u8|u|U|L)?"', src[i:i + 3])
        if m:
            j = i + m.end()
            while j < n and src[j] != '"':
                j += 2 if src[j] == '\\' else 1
            j += 1
            out.append(('str', src[i:j]))
            i = j
            continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == '\\' else 1
            j += 1
            out.append(('char', src[i:j]))
            i = j
            continue
        m = IDENT.match(src, i) or re.compile(r"[0-9][0-9a-zA-Z_.']*").match(src, i)
        if m:
            out.append(('id' if IDENT.match(src, i) else 'num', m.group()))
            i = m.end()
            continue
        out.append(('punct', c))
        i += 1
    return out


def decode_literal(tok):
    """Вміст літерала після обробки escape-послідовностей."""
    m = re.match(r'(u8|u|U|L)?R"([^(\s]*)\(', tok)
    if m:
        return tok[m.end():len(tok) - len(m.group(2)) - 2]
    m = re.match(r'(u8|u|U|L)?"', tok)
    raw = tok[m.end():-1].encode('utf-8')
    simple = {'n': 10, 't': 9, 'r': 13, '\\': 92, '"': 34, "'": 39, 'a': 7, 'b': 8, 'f': 12, 'v': 11, '?': 63}
    out = bytearray()
    i = 0
    while i < len(raw):
        if raw[i] != 0x5C:
            out.append(raw[i])
            i += 1
            continue
        e = chr(raw[i + 1])
        i += 2
        if e == 'x':
            h = ''
            while i < len(raw) and chr(raw[i]) in '0123456789abcdefABCDEF':
                h += chr(raw[i])
                i += 1
            out.append(int(h, 16) & 0xFF)
        elif e in '01234567':
            o = e
            while i < len(raw) and len(o) < 3 and chr(raw[i]) in '01234567':
                o += chr(raw[i])
                i += 1
            out.append(int(o, 8) & 0xFF)
        else:
            out.append(simple.get(e, ord(e)))
    return out.decode('utf-8', errors='surrogateescape')


def significant(tokens, k):
    k += 1
    while k < len(tokens) and tokens[k][0] in ('ws', 'comment'):
        k += 1
    return k


def literal_group(tokens, k):
    """Сусідні літерали, починаючи з k: (склеєний текст, індекс останнього)."""
    last = k
    text = decode_literal(tokens[k][1])
    while True:
        q = significant(tokens, last)
        if q < len(tokens) and tokens[q][0] == 'str':
            text += decode_literal(tokens[q][1])
            last = q
        else:
            return text, last


# ---------- ключі в коді і наявні переклади ----------

def used_keys():
    keys = []
    seen = set()

    def add(v):
        if '##' in v:
            v = v[:v.index('##')]
        if v and CYR.search(v) and v not in seen:
            seen.add(v)
            keys.append(v)

    for dirpath, dirs, files in os.walk(SRC):
        dirs.sort()
        for fn in sorted(files):
            if not fn.endswith(('.cpp', '.hpp')) or fn == 'i18n.cpp':
                continue
            tokens = lex(open(os.path.join(dirpath, fn), encoding='utf-8').read())
            everything = fn == 'app_ui.hpp'
            k = 0
            while k < len(tokens):
                kind, text = tokens[k]
                if everything and kind == 'str':
                    v, k = literal_group(tokens, k)
                    add(v)
                elif kind == 'id' and text in ('tr', 'trf'):
                    p = significant(tokens, k)
                    if p < len(tokens) and tokens[p][1] == '(':
                        q = significant(tokens, p)
                        if q < len(tokens) and tokens[q][0] == 'str':
                            add(literal_group(tokens, q)[0])
                k += 1

    # Назви і підказки кроків тестового прогону — масиви поза функціями, tr() на місці показу
    src = open(os.path.join(SRC, 'core', 'render', 'jobs.cpp'), encoding='utf-8').read()
    a = src.index('const char* const kCheckNames')
    tokens = lex(src[a:src.index('} // namespace', a)])
    k = 0
    while k < len(tokens):
        if tokens[k][0] == 'str':
            v, k = literal_group(tokens, k)
            add(v)
        k += 1
    return keys


def load_inc():
    pairs = {}
    if not os.path.exists(INC):
        return pairs
    tokens = lex(open(INC, encoding='utf-8').read())
    strings = []
    k = 0
    while k < len(tokens):
        if tokens[k][0] == 'str':
            v, k = literal_group(tokens, k)
            strings.append(v)
        k += 1
    for uk, en in zip(strings[0::2], strings[1::2]):
        pairs[uk] = en
    return pairs


def problems_of(uk, en):
    out = []
    kb = BRACES.findall(uk.replace('{{', '').replace('}}', ''))
    vb = BRACES.findall(en.replace('{{', '').replace('}}', ''))
    if kb != vb:
        out.append(f'плейсхолдери {kb} != {vb}')
    # У рядках std::format ({}) «%» — звичайний символ, printf-формат перевіряємо лише в решті
    if not kb and PRINTF.findall(uk) != PRINTF.findall(en):
        out.append(f'формат {PRINTF.findall(uk)} != {PRINTF.findall(en)}')
    if '##' in en:
        out.append('## у перекладі')
    return out


# ---------- запис i18n_en.inc ----------

def c_literal(s):
    # MSVC обмежує один літерал ~16 КБ — довгі рядки ділимо на сусідні літерали (компілятор їх склеїть)
    if len(s) > 1500:
        parts = []
        while s:
            cut = s.rfind('\n', 0, 1500) + 1 if len(s) > 1500 else len(s)
            parts.append(c_literal(s[:cut if cut > 0 else 1500]))
            s = s[cut if cut > 0 else 1500:]
        return '\n  '.join(parts)
    esc = {'\\': '\\\\', '"': '\\"', '\n': '\\n', '\r': '\\r', '\t': '\\t'}
    return '"' + ''.join(esc.get(ch, ch) for ch in s) + '"'


def write_inc(keys, pairs):
    body = ['// Переклади інтерфейсу: українська (ключ) -> English. Генерує scripts/i18n.py (merge) —',
            '// див. docs/ARCHITECTURE.md, розділ «Мова інтерфейсу».']
    for k in keys:
        if k in pairs:
            body.append('{' + c_literal(k) + ',\n ' + c_literal(pairs[k]) + '},')
    with open(INC, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(body) + '\n')


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'check'
    keys = used_keys()
    pairs = load_inc()
    if cmd == 'check':
        missing = [k for k in keys if k not in pairs]
        stale = [k for k in pairs if k not in set(keys)]
        bad = [(k, p) for k in keys if k in pairs for p in problems_of(k, pairs[k])]
        print(f'рядків у коді: {len(keys)}, перекладено: {len(keys) - len(missing)}')
        for k in missing:
            print('  без перекладу:', json.dumps(k, ensure_ascii=False)[:150])
        for k, p in bad:
            print(f'  помилка ({p}):', json.dumps(k, ensure_ascii=False)[:120])
        for k in stale:
            print('  переклад більше не потрібен (прибере merge):', json.dumps(k, ensure_ascii=False)[:120])
        return 1 if missing or bad else 0
    if cmd == 'todo':
        path = sys.argv[2] if len(sys.argv) > 2 else 'i18n_todo.json'
        todo = {k: '' for k in keys if k not in pairs}
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(todo, f, ensure_ascii=False, indent=1)
        print(f'без перекладу: {len(todo)} -> {path}')
        return 0
    if cmd == 'merge':
        if len(sys.argv) > 2:
            with open(sys.argv[2], encoding='utf-8') as f:
                for uk, en in json.load(f).items():
                    if en:
                        pairs[uk] = en.replace('«', '“').replace('»', '”')   # в англійській — свої лапки
        bad = [(k, p) for k in keys if k in pairs for p in problems_of(k, pairs[k])]
        for k, p in bad:
            print(f'  помилка ({p}):', json.dumps(k, ensure_ascii=False)[:120])
        if bad:
            print('i18n_en.inc не змінено — виправте переклади')
            return 1
        write_inc(keys, pairs)
        print(f'записано {sum(k in pairs for k in keys)} перекладів, без перекладу: {sum(k not in pairs for k in keys)}')
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
