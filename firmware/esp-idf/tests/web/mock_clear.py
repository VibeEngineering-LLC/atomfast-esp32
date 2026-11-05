import json
import threading
import urllib.parse

S = {'mode': 'ok', 'log': [], 'ev': threading.Event()}
RINGS = ('raw', 'agg', 'env', 'all')


def _send(h, code, body, ctype='application/json'):
    raw = (body if isinstance(body, str) else json.dumps(body, ensure_ascii=False)).encode('utf-8')
    h.send_response(code)
    h.send_header('Content-Type', ctype + '; charset=utf-8')
    h.send_header('Content-Length', str(len(raw)))
    h.end_headers()
    h.wfile.write(raw)
    return True


def _post(h, path):
    n = int(h.headers.get('Content-Length', 0))
    raw = h.rfile.read(n).decode('utf-8', 'replace') if n > 0 else ''
    try:
        o = json.loads(raw)
    except Exception:
        o = None
    S['log'].append({'path': path, 'csrf': h.headers.get('X-CSRF-Token'), 'body': o, 'raw': raw})
    if h.headers.get('X-CSRF-Token') != 'mock':
        return _send(h, 403, 'Bad or missing CSRF token', 'text/plain')
    if not isinstance(o, dict) or o.get('confirm') != 'CLEAR':
        return _send(h, 400, 'bad confirm', 'text/plain')
    if path == '/api/history/clear' and (o.get('ring') not in RINGS or set(o) != {'ring', 'confirm'}):
        return _send(h, 400, 'bad ring', 'text/plain')
    if path == '/api/log/clear' and set(o) != {'confirm'}:
        return _send(h, 400, 'bad body', 'text/plain')
    if S['mode'] == 'hold':
        S['ev'].wait(20)
    if S['mode'] == 'busy':
        return _send(h, 503, 'busy', 'text/plain')
    if path == '/api/log/clear':
        return _send(h, 200, {'ok': True, 'removed': 12})
    r = o['ring']
    rem = {k: {'raw': 1200, 'agg': 80, 'env': 400}[k] for k in ('raw', 'agg', 'env') if r in (k, 'all')}
    return _send(h, 200, {'ok': True, 'removed': rem, 'persisted': S['mode'] != 'nopersist'})


def handle(h, method, path, query):
    if path == '/__mock/clear' and method == 'POST':
        S['mode'] = urllib.parse.parse_qs(query).get('mode', ['ok'])[0]
        S['log'] = []
        S['ev'].clear()
        return _send(h, 200, {'ok': True})
    if path == '/__mock/clear-release' and method == 'POST':
        S['ev'].set()
        return _send(h, 200, {'ok': True})
    if path == '/__mock/clear-log' and method == 'GET':
        return _send(h, 200, S['log'])
    if method == 'POST' and path in ('/api/history/clear', '/api/log/clear'):
        return _post(h, path)
    return False
