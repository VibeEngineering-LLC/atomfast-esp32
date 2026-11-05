import hashlib
import json
import time


def image(n, magic=0xE9):
    b = bytearray(n)
    b[0] = magic
    for i in range(1, n):
        b[i] = (i * 7 + 3) & 0xFF
    return b


def sha(b):
    return hashlib.sha256(bytes(b)).hexdigest()


FILES = {
    1001: image(65536),
    1002: image(66000, 0x00),
    1003: image(65000),
    1004: image(70000),
    1006: image(67000),
}

LOG = {'gh': [], 'ota': []}
STATE = {'done_at': None, 'version': '1.2.39', 'new_version': '1.2.40-rc1'}


def releases_json():
    return [
        {
            'id': 1001,
            'tag_name': 'firmware-v1.2.40-rc1',
            'name': 'firmware-v1.2.40-rc1',
            'draft': False,
            'prerelease': True,
            'created_at': '2024-01-01T00:00:00Z',
            'published_at': '2024-01-01T00:00:00Z',
            'html_url': 'https://github.com/VibeEngineering-LLC/atomfast-esp32/releases/tag/firmware-v1.2.40-rc1',
            'assets': [
                {
                    'id': 1001,
                    'name': 'atomfast-gw-idf-1.2.40-rc1.bin',
                    'size': len(FILES[1001]),
                    'digest': 'sha256:' + sha(FILES[1001]),
                    'url': 'https://api.github.com/repos/VibeEngineering-LLC/atomfast-esp32/releases/assets/1001'
                }
            ]
        },
        {
            'id': 1004,
            'tag_name': 'firmware-v1.2.39',
            'name': 'firmware-v1.2.39',
            'draft': False,
            'prerelease': False,
            'created_at': '2024-01-01T00:00:00Z',
            'published_at': '2024-01-01T00:00:00Z',
            'html_url': 'https://github.com/VibeEngineering-LLC/atomfast-esp32/releases/tag/firmware-v1.2.39',
            'assets': [
                {
                    'id': 1004,
                    'name': 'atomfast-gw-idf-1.2.39.bin',
                    'size': len(FILES[1004]),
                    'digest': 'sha256:' + '0' * 64,
                    'url': 'https://api.github.com/repos/VibeEngineering-LLC/atomfast-esp32/releases/assets/1004'
                }
            ]
        },
        {
            'id': 1002,
            'tag_name': 'firmware-v1.2.30',
            'name': 'firmware-v1.2.30',
            'draft': False,
            'prerelease': False,
            'created_at': '2024-01-01T00:00:00Z',
            'published_at': '2024-01-01T00:00:00Z',
            'html_url': 'https://github.com/VibeEngineering-LLC/atomfast-esp32/releases/tag/firmware-v1.2.30',
            'assets': [
                {
                    'id': 1002,
                    'name': 'atomfast-gw-s3.bin',
                    'size': len(FILES[1002]),
                    'digest': 'sha256:' + sha(FILES[1002]),
                    'url': 'https://api.github.com/repos/VibeEngineering-LLC/atomfast-esp32/releases/assets/1002'
                },
                {
                    'id': 1006,
                    'name': 'atomfast-gw-c3.bin',
                    'size': len(FILES[1006]),
                    'digest': 'sha256:' + sha(FILES[1006]),
                    'url': 'https://api.github.com/repos/VibeEngineering-LLC/atomfast-esp32/releases/assets/1006'
                }
            ]
        },
        {
            'id': 1003,
            'tag_name': 'firmware-v1.2.26-rc1',
            'name': 'firmware-v1.2.26-rc1',
            'draft': False,
            'prerelease': True,
            'created_at': '2024-01-01T00:00:00Z',
            'published_at': '2024-01-01T00:00:00Z',
            'html_url': 'https://github.com/VibeEngineering-LLC/atomfast-esp32/releases/tag/firmware-v1.2.26-rc1',
            'assets': [
                {
                    'id': 1003,
                    'name': 'atomfast-esp32.bin',
                    'size': len(FILES[1003]),
                    'digest': None,
                    'url': 'https://api.github.com/repos/VibeEngineering-LLC/atomfast-esp32/releases/assets/1003'
                }
            ]
        },
        {
            'id': 1005,
            'tag_name': 'firmware-v1.2.20-draft',
            'name': 'firmware-v1.2.20-draft',
            'draft': True,
            'prerelease': False,
            'created_at': '2024-01-01T00:00:00Z',
            'published_at': None,
            'html_url': 'https://github.com/VibeEngineering-LLC/atomfast-esp32/releases/tag/firmware-v1.2.20-draft',
            'assets': []
        }
    ]


def fw_state():
    if STATE['done_at'] is None:
        return (200, STATE['version'])
    if STATE.get('grace', 0) > 0:
        STATE['grace'] -= 1
        return (200, STATE['version'])
    if STATE.get('down_left', 0) > 0:
        STATE['down_left'] -= 1
        return (503, STATE['version'])
    return (200, STATE['new_version'])


def _send_json(h, code, obj):
    body = json.dumps(obj).encode('utf-8')
    h.send_response(code)
    h.send_header('Content-Type', 'application/json')
    h.send_header('Content-Length', str(len(body)))
    h.end_headers()
    h.wfile.write(body)


def _send_text(h, code, text, ctype='text/plain'):
    body = text.encode('utf-8')
    h.send_response(code)
    h.send_header('Content-Type', ctype)
    h.send_header('Content-Length', str(len(body)))
    h.end_headers()
    h.wfile.write(body)


def _send_bytes(h, code, data, ctype='application/octet-stream'):
    h.send_response(code)
    h.send_header('Content-Type', ctype)
    h.send_header('Content-Length', str(len(data)))
    h.end_headers()
    h.wfile.write(data)


def handle(h, method, path, query):
    try:
        if method == 'GET' and path == '/__mock/gh-log':
            _send_json(h, 200, LOG['gh'])
            return True

        if method == 'GET' and path == '/__mock/ota-log':
            _send_json(h, 200, LOG['ota'])
            return True

        if method == 'POST' and path == '/__mock/reset':
            LOG['gh'] = []
            LOG['ota'] = []
            STATE['done_at'] = None
            STATE['down_left'] = 0
            STATE['grace'] = 0
            STATE['new_version'] = '1.2.40-rc1'
            _send_json(h, 200, {'ok': True})
            return True

        if method == 'GET' and path.startswith('/__gh/repos/'):
            parts = path.split('/')
            # /__gh/repos/<owner>/<name>/releases
            if len(parts) == 6 and parts[5] == 'releases':
                owner = parts[3]
                name = parts[4]
                log_entry = {
                    'path': path + ('?' + query if query else ''),
                    'headers': {k.lower(): v for k, v in h.headers.items()}
                }
                LOG['gh'].append(log_entry)

                if owner == 'missing' and name == 'none':
                    _send_json(h, 404, {'message': 'Not Found'})
                    return True

                if owner == 'limited' and name == 'repo':
                    h.send_response(403)
                    h.send_header('Content-Type', 'application/json')
                    h.send_header('X-RateLimit-Remaining', '0')
                    h.send_header('X-RateLimit-Reset', str(int(time.time() + 120)))
                    body = json.dumps({'message': 'API rate limit exceeded'}).encode('utf-8')
                    h.send_header('Content-Length', str(len(body)))
                    h.end_headers()
                    h.wfile.write(body)
                    return True

                if owner == 'broken' and name == 'json':
                    _send_text(h, 200, 'not json', 'application/json')
                    return True

                if owner == 'empty' and name == 'repo':
                    _send_json(h, 200, [])
                    return True

                _send_json(h, 200, releases_json())
                return True

            # /__gh/repos/<owner>/<name>/releases/assets/<id>
            if len(parts) == 8 and parts[5] == 'releases' and parts[6] == 'assets':
                asset_id_str = parts[7]
                log_entry = {
                    'path': path + ('?' + query if query else ''),
                    'headers': {k.lower(): v for k, v in h.headers.items()}
                }
                LOG['gh'].append(log_entry)

                try:
                    asset_id = int(asset_id_str)
                except ValueError:
                    _send_json(h, 404, {'message': 'Not Found'})
                    return True

                if asset_id in FILES:
                    _send_bytes(h, 200, bytes(FILES[asset_id]), 'application/octet-stream')
                    return True
                else:
                    _send_json(h, 404, {'message': 'Not Found'})
                    return True

        if method == 'POST' and path == '/api/ota':
            content_length = int(h.headers.get('Content-Length', 0))
            body = h.rfile.read(content_length) if content_length > 0 else b''
            first_byte = body[0] if len(body) > 0 else -1
            log_entry = {
                'len': len(body),
                'first': first_byte,
                'csrf': h.headers.get('X-CSRF-Token'),
                'ctype': h.headers.get('Content-Type')
            }
            LOG['ota'].append(log_entry)

            if first_byte != 0xE9:
                _send_text(h, 400, 'Bad magic')
                return True
            else:
                STATE['done_at'] = time.time()
                STATE['down_left'] = 3
                STATE['grace'] = 2
                _send_text(h, 200, 'OK')
                return True

        return False

    except Exception:
        try:
            _send_text(h, 500, 'Internal Server Error')
        except Exception:
            pass
        return True
