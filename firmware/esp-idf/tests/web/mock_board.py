import json
import time
import urllib.parse

import mock_gh

TOKEN = 'mock'
HTML = 'https://github.com/VibeEngineering-LLC/atomfast-esp32/releases/tag/firmware-v'
LOG = {'board': []}
S = {}


def _check(cur, latest, newer, installable, reason):
    return {'state': 'done', 'current': cur, 'latest': latest, 'newer': newer, 'installable': installable,
            'reason': reason, 'html_url': (HTML + latest) if latest else ''}


CHECKS = {
    'ok': _check('1.2.39', '1.2.40', True, True, ''),
    'rc': _check('1.2.39', '1.2.40-rc1', True, True, ''),
    'up_to_date': _check('1.2.39', '1.2.39', False, False, 'up_to_date'),
    'downgrade_blocked': _check('1.2.39', '1.2.30', False, False, 'downgrade_blocked'),
    'too_old': _check('1.2.20', '1.2.25', True, False, 'too_old_for_wifi_ota'),
    'missing_assets': _check('1.2.39', '1.2.41', True, False, 'release_missing_assets'),
    'no_internet': _check('?', '', False, False, 'no_internet'),
    'network_error': _check('?', '', False, False, 'network_error'),
    'no_releases': _check('1.2.39', '', False, False, 'no_releases'),
}


def _p(state, b=0, t=0, err=''):
    return {'state': state, 'bytes': b, 'total': t, 'error': err}


FULL = [_p('checking'), _p('downloading', 100000, 1000000), _p('downloading', 600000, 1000000),
        _p('verifying', 1000000, 1000000), _p('installing', 1000000, 1000000), _p('done', 1000000, 1000000)]

SEQS = {
    'ok': FULL,
    'stale': FULL,
    'noreboot': FULL,
    'fail_sha': [_p('checking'), _p('downloading', 300000, 1000000), _p('verifying', 1000000, 1000000),
                 _p('error', 1000000, 1000000, 'sha256_mismatch')],
    'hold': [_p('checking'), _p('downloading', 600000, 1000000)],
    'fail_oom': [_p('checking'), _p('error', 0, 0, 'oom')],
    'reboot_down': [_p('checking'), _p('downloading', 500000, 1000000), _p('installing', 1000000, 1000000), 'DOWN'],
    'reboot_idle': [_p('checking'), _p('downloading', 500000, 1000000), _p('installing', 1000000, 1000000), 'IDLE'],
}


def reset():
    S.clear()
    S.update({'check': 'ok', 'install': 'ok', 'polls': 2, 'calls': 0, 'installing': False, 'i': 0,
              'stale_left': 0, 'pre': False, 'sys': None, 'baseline': None, 'rebooted': False})
    LOG['board'] = []


reset()


def _csrf_ok(h):
    return h.headers.get('X-CSRF-Token') == TOKEN


def _log(h, method, path, body=''):
    LOG['board'].append({'method': method, 'path': path, 'csrf': h.headers.get('X-CSRF-Token'), 'body': body})


def _reboot():
    if S['rebooted']:
        return
    S['rebooted'] = True
    st = mock_gh.STATE
    st['done_at'] = time.time()
    st['down_left'] = 3
    st['grace'] = 2
    st['new_version'] = st['version'] if S['install'] == 'noreboot' else '1.2.40'


def _progress(h):
    if not S['installing']:
        return mock_gh._send_json(h, 200, S['baseline'] or _p('idle'))
    if S['stale_left'] > 0:
        S['stale_left'] -= 1
        return mock_gh._send_json(h, 200, S['baseline'])
    seq = SEQS[S['install']]
    item = seq[min(S['i'], len(seq) - 1)]
    S['i'] += 1
    if item == 'DOWN':
        _reboot()
        return mock_gh._send_text(h, 503, 'rebooting')
    if item == 'IDLE':
        _reboot()
        return mock_gh._send_json(h, 200, _p('idle'))
    if item['state'] == 'done':
        _reboot()
    return mock_gh._send_json(h, 200, item)


def _configure(h, query):
    q = urllib.parse.parse_qs(query)
    reset()
    st = mock_gh.STATE
    mock_gh.LOG['gh'] = []
    mock_gh.LOG['ota'] = []
    st['done_at'] = None
    st['down_left'] = 0
    st['grace'] = 0
    st['version'] = '1.2.39'
    st['new_version'] = '1.2.40'
    S['check'] = q.get('check', ['ok'])[0]
    S['install'] = q.get('install', ['ok'])[0]
    S['polls'] = int(q.get('polls', ['2'])[0])
    if 'free' in q:
        S['sys'] = {'int_free': int(q['free'][0]), 'int_largest': int(q.get('largest', ['40000'])[0]), 'int_min': 30000,
                    'free_heap': 3298560, 'psram_free': 7712768, 'psram_total': 8386560}
    if S['install'] == 'stale':
        S['baseline'] = _p('error', 5, 10, 'sha256_mismatch')
    if q.get('pre', ['0'])[0] == '1':
        S['pre'] = True
    mock_gh._send_json(h, 200, {'ok': True})


def handle(h, method, path, query):
    try:
        if method == 'GET' and path == '/__mock/board-log':
            mock_gh._send_json(h, 200, LOG['board'])
            return True
        if method == 'POST' and path == '/__mock/board':
            _configure(h, query)
            return True
        if method == 'GET' and path == '/api/system' and S['sys']:
            mock_gh._send_json(h, 200, S['sys'])
            return True
        if method == 'GET' and path == '/api/ota/github/check':
            _log(h, method, path)
            if not _csrf_ok(h) or S['check'] == 'forbidden':
                mock_gh._send_text(h, 403, 'Bad or missing CSRF token')
                return True
            if S['check'] == 'badjson':
                mock_gh._send_text(h, 200, 'oops', 'application/json')
                return True
            S['calls'] += 1
            if S['calls'] <= S['polls']:
                mock_gh._send_json(h, 200, {'state': 'checking'})
                return True
            S['calls'] = 0
            mock_gh._send_json(h, 200, CHECKS[S['check']])
            return True
        if method == 'GET' and path == '/api/ota/github/progress':
            _log(h, method, path)
            _progress(h)
            return True
        if method == 'POST' and path == '/api/ota/github/install':
            _log(h, method, path)
            if not _csrf_ok(h):
                mock_gh._send_text(h, 403, 'Bad or missing CSRF token')
                return True
            if S['install'] == 'conflict':
                mock_gh._send_text(h, 409, 'Install already running or OOM')
                return True
            S['installing'] = True
            S['i'] = 0
            S['stale_left'] = 2 if S['install'] == 'stale' else 0
            mock_gh._send_json(h, 200, {'ok': True})
            return True
        if method == 'GET' and path == '/api/ota/github/channel':
            _log(h, method, path)
            mock_gh._send_json(h, 200, {'prerelease': S['pre']})
            return True
        if method == 'POST' and path == '/api/ota/github/channel':
            n = int(h.headers.get('Content-Length', 0))
            body = h.rfile.read(n).decode('utf-8') if n > 0 else ''
            _log(h, method, path, body)
            if not _csrf_ok(h):
                mock_gh._send_text(h, 403, 'Bad or missing CSRF token')
                return True
            S['pre'] = '"prerelease":true' in body
            mock_gh._send_json(h, 200, {'ok': True})
            return True
        return False
    except Exception:
        try:
            mock_gh._send_text(h, 500, 'Internal Server Error')
        except Exception:
            pass
        return True
