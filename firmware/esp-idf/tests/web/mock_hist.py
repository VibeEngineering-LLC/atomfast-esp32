import json
import time
import urllib.parse

DATA = {'raw': [], 'agg': []}
ENV = {'rows': None}
LOG = {'q': [], 'inflight': 0, 'max_inflight': 0, 'status': 0}
CFG = {'delay_first_ms': 0, 'served': 0}
HEADER = 'ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n\n'
ENV_HEADER = 'ts,kind,temp_c,press_mmhg,press_pa,flags\n'


def configure(data, env_rows):
    DATA['raw'] = data['raw']
    DATA['agg'] = data['agg']
    ENV['rows'] = env_rows


def _send(h, code, body, ctype='text/plain', extra=None):
    raw = body.encode('utf-8') if isinstance(body, str) else body
    h.send_response(code)
    h.send_header('Content-Type', ctype)
    h.send_header('Cache-Control', 'no-store')
    for k, v in (extra or {}).items():
        h.send_header(k, v)
    h.send_header('Content-Length', str(len(raw)))
    h.end_headers()
    h.wfile.write(raw)


def _ts(line):
    try:
        return int(line.split(',', 1)[0])
    except ValueError:
        return 0


def _int(q, key):
    if key not in q:
        return None
    v = q[key][0]
    if not v.isdigit() or len(v) > 10:
        raise ValueError(key)
    return int(v)


def _history(h, query):
    q = urllib.parse.parse_qs(query)
    kind = q.get('kind', ['raw'])[0]
    LOG['q'].append(query)
    LOG['inflight'] += 1
    LOG['max_inflight'] = max(LOG['max_inflight'], LOG['inflight'])
    try:
        if kind not in ('raw', 'agg', 'env'):
            return _send(h, 400, 'bad kind')
        try:
            frm = _int(q, 'from')
            to = _int(q, 'to')
            limit = _int(q, 'limit')
        except ValueError as e:
            return _send(h, 400, 'bad ' + str(e))
        if frm is not None and to is not None and frm > to:
            return _send(h, 400, 'bad range')
        if CFG['delay_first_ms'] and CFG['served'] == 0:
            time.sleep(CFG['delay_first_ms'] / 1000.0)
        CFG['served'] += 1
        if kind == 'env':
            rows = ENV['rows']() if ENV['rows'] else []
            header = ENV_HEADER
        else:
            rows = DATA[kind]
            header = HEADER
        out = [r for r in rows if _ts(r) > 0 and (frm is None or _ts(r) >= frm) and (to is None or _ts(r) < to)]
        if limit is not None:
            out = out[:limit]
        return _send(h, 200, header + ''.join(out), 'text/csv', {'X-AF-Seq': str(len(out))})
    finally:
        LOG['inflight'] -= 1


def handle(h, method, path, query):
    try:
        if path == '/api/history/status':
            LOG['status'] += 1
        if method == 'GET' and path == '/__mock/hist-log':
            _send(h, 200, json.dumps(LOG), 'application/json')
            return True
        if method == 'POST' and path == '/__mock/hist':
            q = urllib.parse.parse_qs(query)
            LOG['q'] = []
            LOG['max_inflight'] = 0
            LOG['status'] = 0
            CFG['served'] = 0
            CFG['delay_first_ms'] = int(q.get('delay', ['0'])[0])
            _send(h, 200, json.dumps({'ok': True}), 'application/json')
            return True
        if method == 'GET' and path == '/api/history.csv' and ('from=' in query or 'to=' in query):
            _history(h, query)
            return True
        return False
    except Exception:
        try:
            _send(h, 500, 'Internal Server Error')
        except Exception:
            pass
        return True
