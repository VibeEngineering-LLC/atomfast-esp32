import json
import time
import urllib.parse

S = {'name': 'normal', 'n': 0}
T0 = 1792853245
REASONS = {0x08: ('supervision timeout', 'timeout'), 0x13: ('remote terminated', 'remote'), 0x16: ('local terminated', 'local'),
           0x3E: ('conn failed to establish', 'establish'), 0x22: ('LL timeout', 'timeout')}


def _send(h, code, body, ctype='application/json'):
    raw = body if isinstance(body, bytes) else (body if isinstance(body, str) else json.dumps(body, ensure_ascii=False)).encode('utf-8')
    h.send_response(code)
    h.send_header('Content-Type', ctype + ('; charset=utf-8' if 'text' in ctype or 'json' in ctype else ''))
    h.send_header('Content-Length', str(len(raw)))
    h.end_headers()
    h.wfile.write(raw)
    return True


def sessions(name):
    if name in ('nodata', 'old'):
        return []
    n = 32 if name == 'many' else (3 if name == 'normal' else 2)
    out = []
    end = int(time.time()) - 600
    for i in range(n):
        dur = [5432.123, 1801.5, 12.25, 9000.0, 301.0, 60.0, 3600.5, 45.0][i % 8] + i * 0.5
        code = [0x08, 0x13, 0x16, 0x3E, 0x22][i % 5]
        out.append({'idx': i, 'start_utc': (end - int(dur)) if i != 1 else None, 'end_utc': end if i != 1 else None, 'dur_s': dur,
                    'pkts': int(dur / 2), 'bad': i % 3, 'reason_code': code, 'reason': REASONS[code][0],
                    'rssi_min_dbm': -88 - i % 5, 'rssi_avg_dbm': -72 - i % 4, 'rssi_max_dbm': -60, 'setup_ms': (1200 + i * 30) if i % 4 else None})
        end -= int(dur) + 120
    return out


def ble(name):
    if name == 'nodata':
        return {'ready': False}
    many = name == 'many'
    err = name == 'errors'
    nd = None
    b = {'ready': True, 'connected': not err, 'uptime_s': 86400 * 2 + 3600,
         'link': {'session_s': 0.0 if err else 5432.123, 'session_start_utc': nd if err else T0 + 34500, 'up_boot_pct': 61.5 if err else 98.7,
                  'up_24h_pct': 40.2 if err else (83.4 if many else 99.2),
                  'connects': 41 if many else 3, 'disconnects': 40 if many else 2, 'attempts': 48 if many else 3, 'fails': 7 if many else (9 if err else 0),
                  'fail_streak': 5 if err else 0, 'connects_total': 410 if many else 17, 'disconnects_total': 409 if many else 16,
                  'attempts_total': 480 if many else 17, 'fails_total': 70 if many else 1, 'conn_total_s': 912345,
                  'last_reason_code': 0x3E if err else 0x08, 'last_reason': 'conn failed to establish' if err else 'supervision timeout',
                  'last_reason_group': 'establish' if err else 'timeout',
                  'reasons': {'other': 0, 'timeout': 20 if many else 1, 'remote': 10 if many else 1, 'local': 5 if many else 0, 'establish': 7 if many else 9 if err else 0, 'l2cap': 0},
                  'reasons_total': {'other': 1, 'timeout': 200 if many else 9, 'remote': 100 if many else 5, 'local': 50 if many else 2, 'establish': 70 if many else 1, 'l2cap': 2},
                  'last_session_s': 1801.5, 'longest_session_s': 9000.0, 'shortest_session_s': 12.25, 'avg_session_s': 3120.4, 'median_session_s': 1801.5, 'ring_n': 32 if many else 3},
         'rate': {'pps_inst': nd if err else 0.5, 'pps_1m': nd if err else 0.499, 'pps_10m': nd if err else 0.4992, 'pps_sess': nd if err else 0.4985,
                  'bps_inst': nd if err else 6.5, 'bps_1m': nd if err else 6.49, 'bps_10m': nd if err else 6.49, 'bps_sess': nd if err else 6.48,
                  'pkts_session': 2710, 'bad_session': 1, 'pkts_boot': 86000, 'bad_boot': 12, 'bad_pct': 0.014, 'last_bad_age_s': 1234.5,
                  'last_pkt_age_ms': nd if err else 800, 'nominal_ms': 2000, 'iv_last_ms': nd if err else 2001, 'iv_min_ms': 1900, 'iv_max_ms': 8200,
                  'iv_avg_ms': 2004.2, 'jitter_ms': 35.7, 'pauses': 3, 'pause_max_ms': 8200, 'iv10_min_ms': 1950, 'iv10_max_ms': 2300, 'iv10_avg_ms': 2001.5},
         'params': {'interval_ms': nd if err else 30.0, 'latency': nd if err else 0, 'timeout_ms': nd if err else 4000, 'mtu': nd if err else 23,
                    'phy_tx': None, 'phy_rx': None, 'phy_note': 'n/a: BLE 5.0 API off in this build', 'tx_set_dbm': 20, 'tx_eff_level': 15,
                    'rssi_dbm': nd if err else -71, 'rssi_min_dbm': -90, 'rssi_avg_dbm': -72, 'rssi_max_dbm': -58,
                    'rssi_hist': [0, 2, 10, 120, 800, 400, 20, 0], 'rssi_bin_db': 10, 'setup_ms': 1250},
         'scan': {'interval_ms': 100.0, 'window_ms': 99.0, 'scans': 120, 'adv_last': 14, 'found_last': 1, 'detect_ms': 1250},
         'device': {'addr': 'AA:BB:**:**:CC:DD', 'addr_type': 'public', 'name': 'AtomFast'}}
    if name == 'noscan':
        b['scan'].update({'scans': 0, 'adv_last': 0, 'found_last': 0, 'detect_ms': None})
        b['device']['name'] = None
    return b


def csv(name):
    head = 'idx,start_utc,end_utc,dur_s,pkts,bad,reason_code,reason,rssi_min_dbm,rssi_avg_dbm,rssi_max_dbm,setup_ms\n'
    rows = []
    for r in sessions(name):
        f = lambda v: '' if v is None else str(v)
        rows.append(','.join([str(r['idx']), f(r['start_utc']), f(r['end_utc']), '%.3f' % r['dur_s'], str(r['pkts']), str(r['bad']), str(r['reason_code']), r['reason'],
                              str(r['rssi_min_dbm']), str(r['rssi_avg_dbm']), str(r['rssi_max_dbm']), f(r['setup_ms'])]) + '\n')
    return head + ''.join(rows)


def handle(h, method, path, query):
    try:
        if method == 'POST' and path == '/__mock/ble':
            S['name'] = urllib.parse.parse_qs(query).get('scenario', ['normal'])[0]
            return _send(h, 200, {'ok': True, 'name': S['name']})
        if method == 'GET' and path == '/__mock/ble-n':
            return _send(h, 200, {'n': S['n']})
        if method != 'GET' or not path.startswith('/api/ble'):
            return False
        if path == '/api/ble-reconnect':
            return False
        if S['name'] == 'old':
            return _send(h, 404, 'Not Found', 'text/plain')
        if path == '/api/ble':
            S['n'] += 1
            return _send(h, 200, ble(S['name']))
        if path == '/api/ble/sessions':
            lim = 32
            if query:
                q = urllib.parse.parse_qs(query, keep_blank_values=True)
                v = q.get('limit', [''])[0]
                if list(q.keys()) != ['limit'] or not v.isdigit() or len(v) > 2 or not (1 <= int(v) <= 32):
                    return _send(h, 400, 'bad limit', 'text/plain')
                lim = int(v)
            return _send(h, 200, sessions(S['name'])[:lim])
        if path == '/api/ble/sessions.csv':
            return _send(h, 200, csv(S['name']), 'text/csv')
        return False
    except Exception:
        return _send(h, 500, 'Internal Server Error', 'text/plain')
