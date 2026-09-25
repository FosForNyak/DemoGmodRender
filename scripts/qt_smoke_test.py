"""Перевірка вікна Qt без екрана: кожен робочий простір відкривається (англійською й
українською), знімок вікна зберігається, а в журналі немає жодного попередження Qt чи QML.

  python scripts/qt_smoke_test.py <gmdr-qt[.exe]> <демо.dem> <тека для знімків>

Вікно малюється програмно (QT_QPA_PLATFORM=offscreen), без відеокарти. Налаштування й журнал —
у теці програми (як і в звичайному запуску з теки, куди можна писати).
"""
import os
import re
import subprocess
import sys

PAGES = ['project', 'edit', 'render', 'audio', 'ai', 'library', 'queue', 'log',
         'settings/appearance', 'settings/language', 'settings/graphics', 'settings/game',
         'settings/rendering', 'settings/storage', 'settings/notifications', 'settings/system']
LANGS = ['en', 'uk']
# Повідомлення Qt у журналі програми (main_qt.cpp пише їх як «Qt: [категорія] ...»); рівень — будь-якою
# мовою. Не рахуються лише повідомлення про оточення, а не про програму: платформа (qt.qpa.*) і шрифти
# без таблиць OpenType на CI-машині (Qt 6.4 пише це без категорії)
QT_PROBLEM = re.compile(r'\[(WARNING|ERROR|УВАГА|ПОМИЛКА)\] Qt: (?!\[qt\.qpa\.|\s*OpenType support missing for )')


def main():
    # Консоль Windows у CI — cp1252: українські рядки журналу інакше не надрукуються
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8', errors='replace')
    exe, demo, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    log = os.path.join(os.path.dirname(os.path.abspath(exe)), 'gmdr_log.txt')
    failures = []
    base_env = dict(os.environ)
    if sys.platform.startswith('linux') and not base_env.get('XDG_RUNTIME_DIR'):
        # Інакше Qt попереджає про XDG_RUNTIME_DIR — і перевірка впала б не через програму
        runtime = os.path.abspath(os.path.join(out, 'xdg-runtime'))
        os.makedirs(runtime, mode=0o700, exist_ok=True)
        os.chmod(runtime, 0o700)
        base_env['XDG_RUNTIME_DIR'] = runtime
    if sys.platform == 'win32' and not base_env.get('QT_QPA_FONTDIR'):
        # Платформа offscreen на Windows шукає шрифти в теці Qt (там їх немає) — дати системні,
        # інакше на знімках не буде тексту. Звичайне вікно (qwindows) бере шрифти системи саме
        base_env['QT_QPA_FONTDIR'] = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts')
    for lang in LANGS:
        for page in PAGES:
            shot = os.path.abspath(os.path.join(out, f'{lang}_{page.replace("/", "_")}.png'))
            if os.path.exists(shot):
                os.remove(shot)
            if os.path.exists(log):
                os.remove(log)
            env = dict(base_env, QT_QPA_PLATFORM='offscreen', GMDR_MULTI_INSTANCE='1', GMDR_TEST_PAGE=page,
                       GMDR_SCREENSHOT=shot, GMDR_SCREENSHOT_DELAY='3000', GMDR_WINDOW_SIZE='1440x900', GMDR_LANG=lang)
            try:
                rc = subprocess.run([exe, demo], env=env, timeout=90).returncode
            except subprocess.TimeoutExpired:
                rc = 'timeout'
            problems = []
            if os.path.exists(log):
                with open(log, encoding='utf-8', errors='replace') as f:
                    problems = [line.rstrip() for line in f if QT_PROBLEM.search(line)]
            ok = rc == 0 and os.path.exists(shot) and os.path.getsize(shot) > 10000 and not problems
            print(f'{"ok  " if ok else "FAIL"} {lang} {page}: exit {rc}', flush=True)
            for p in problems:
                print('     ' + p)
            if not ok:
                failures.append(f'{lang} {page}')
    if failures:
        print('Не пройшли: ' + ', '.join(failures))
        sys.exit(1)
    print(f'Усі {len(PAGES) * len(LANGS)} знімків — без попереджень Qt')


if __name__ == '__main__':
    main()
