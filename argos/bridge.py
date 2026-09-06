"""Loopback-only Argos adapter. Install language packages with --install first."""
import argparse
import ctypes
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
# Keep the handle alive for the lifetime of this process; multiple app launches
# must not start competing servers. Installation is allowed alongside the server.
import sys
if sys.platform == 'win32' and '--install' not in sys.argv:
    ctypes.windll.kernel32.CreateMutexW.restype = ctypes.c_void_p
    _mutex = ctypes.windll.kernel32.CreateMutexW(None, False, 'Local\\PointTranslatorArgosBridge')
    if ctypes.windll.kernel32.GetLastError() == 183:
        sys.exit(0)
import argostranslate.package
import argostranslate.translate
from pypinyin import lazy_pinyin, Style

def pinyin(text):
    if not any('\u3400' <= char <= '\u9fff' for char in text):
        return ''
    return ' '.join(lazy_pinyin(text, style=Style.TONE))

PAIRS = [('zh', 'en'), ('en', 'zh'), ('en', 'th'), ('th', 'en')]

def install():
    argostranslate.package.update_package_index()
    available = argostranslate.package.get_available_packages()
    installed = {(p.from_code, p.to_code) for p in argostranslate.package.get_installed_packages()}
    for source, target in PAIRS:
        if (source, target) not in installed:
            package = next(p for p in available if p.from_code == source and p.to_code == target)
            print(f'Installing {source} -> {target}', flush=True)
            argostranslate.package.install_from_path(package.download())

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass  # Do not log translated text.

    def reply(self, code, value):
        data = json.dumps(value, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.reply(200, {'service': 'point-translator-argos'})

    def do_POST(self):
        if self.path != '/translate' or self.headers.get('Origin'):
            self.reply(403, {'message': 'Unsupported request'})
            return
        try:
            length = int(self.headers.get('Content-Length', 0))
            if not 0 < length <= 100000:
                raise ValueError('Invalid text size')
            data = json.loads(self.rfile.read(length))
            source, target, text = data['source'], data['target'], data['q']
            if source not in ('zh', 'en', 'th') or target not in ('zh', 'en', 'th'):
                raise ValueError('Unsupported language')
            if not isinstance(text, str) or not text.strip():
                raise ValueError('Text is required')
            value = argostranslate.translate.translate(text, source, target)
            self.reply(200, {'translatedText': value,
                             'originalPinyin': pinyin(text) if source == 'zh' else '',
                             'translatedPinyin': pinyin(value) if target == 'zh' else ''})
        except Exception as error:
            self.reply(400, {'message': str(error)})

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    if args.install:
        install()
    else:
        HTTPServer(('127.0.0.1', 18765), Handler).serve_forever()
