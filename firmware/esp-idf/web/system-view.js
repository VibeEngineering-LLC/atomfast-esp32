function setBig(id, text, level) {
    var el = document.getElementById(id);
    el.textContent = text;
    el.className = 'big' + (level === 'ok' || level === 'warn' || level === 'bad' ? ' ' + level : '');
}
function renderSystem(st, sy) {
    var m = AFU.sysModel(st, sy);
    document.getElementById('sy_wifi').textContent = AFU.yesNo(st ? st.wifi_connected : undefined);
    document.getElementById('sy_ble').textContent = AFU.yesNo(st ? st.ble_ready : undefined);
    document.getElementById('sy_fw').textContent = m.fw;
    document.getElementById('sy_part').textContent = m.part;
    document.getElementById('sy_up').textContent = m.uptime;
    document.getElementById('sy_boot').textContent = m.boots;
    document.getElementById('sy_reset').textContent = m.reset;
    document.getElementById('sy_time').textContent = m.timeSource;
    document.getElementById('sy_sntp').textContent = m.sntp;
    document.getElementById('sy_net').textContent = m.netMode;
    document.getElementById('sy_ssid').textContent = m.ssid;
    // e16i b29 (V-20): пароль полевой точки доступа не менялся (константа исходника) - предупреждение; смена - решение оператора.
    document.getElementById('sy_ap_warn').hidden = !(sy && sy.ap_pass_default === true);
    document.getElementById('sy_psram').textContent = m.psramFree + ' / ' + m.psramTotal;
    document.getElementById('sy_heap').textContent = m.heapFree;
    document.getElementById('sy_heap_min').textContent = m.heapMin;
    setBig('sy_heap_big', m.heapFree, m.heapLevel);
    document.getElementById('sy_int').textContent = m.intFree;
    document.getElementById('sy_int_min').textContent = m.intMin;
    setBig('sy_int_big', m.intFree, m.intLevel);
    var flashPctStr = m.flashPct === null ? '—' : m.flashPct + ' %';
    document.getElementById('sy_flash_pct').textContent = flashPctStr;
    document.getElementById('sy_flash_txt').textContent = m.flashText;
    setBig('sy_flash_big', flashPctStr, m.flashLevel);
    document.getElementById('sy_flash_bar').style.width = (m.flashPct === null ? 0 : m.flashPct) + '%';
    document.getElementById('sy_flash_barwrap').className = 'bar' + (m.flashLevel === 'warn' || m.flashLevel === 'bad' ? ' ' + m.flashLevel : '');
    document.getElementById('sy_rssi').textContent = m.rssiText;
    document.getElementById('sy_rssi_label').textContent = m.rssi.label;
    setBig('sy_rssi_big', m.rssiText, m.rssi.level);
    document.getElementById('sy_rssi_bar').style.width = (m.rssi.pct === null ? 0 : m.rssi.pct) + '%';
    document.getElementById('sy_rssi_barwrap').className = 'bar' + (m.rssi.level === 'warn' || m.rssi.level === 'bad' ? ' ' + m.rssi.level : '');
}
function setSysResult(cls, text) {
    var el = document.getElementById('sysResult');
    el.className = 'result ' + cls;
    el.textContent = text;
}
