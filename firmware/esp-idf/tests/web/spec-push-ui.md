# Спека: web/push-ui.js — DOM-обвязка раздела «База данных» (/system#db)

Обычный скрипт (не модуль), вклеивается в страницу после `push-model.js` (глобал `AFPUSH`). Доступны глобальные функции страницы: `fetchJSON(url)` (бросает ошибку со свойством `status`) и `postJSON(url, body)` (возвращает `{ok, status, data}`; `data` — разобранный JSON или null). Стиль: ES5 (`var`, `function`, `async function` допустимы), как `store-clear-ui.js`. Без `window.confirm`. Токен нигде не выводится в текст страницы, в консоль и не хранится в переменных после отправки.

Состояние: `var pushDirty = false, pushFilled = false, pushBusy = false, pushLast = null;`

## Вспомогательные
- `pEl(id)` = `document.getElementById(id)`.
- `pushSay(id, cls, text)`: элементу `id` ставит `className = 'result' + (cls ? ' ' + cls : '')` и `textContent = text`; нет элемента — ничего.
- `pushSet(id, text)`: `textContent` элемента, если он есть.

## pushRender(j)
Принимает ответ `GET /api/push`. Через `AFPUSH`: `p_en` = `statusView(j).enabled`; `p_state` = `stateText(j)`; `p_conn`, `p_sent`, `p_fail`, `p_err` = одноимённые поля `statusView(j)` (conn, sent, fail, lastErr); для каждой записи массива `ringRows(j)` элемент `'p_ring_' + id` получает `text`. `pushTokenHint` = `tokenHint(j)`. Элемент `pushWarn` показывается (`hidden = false`), только если `j.warn && j.warn.public_addr === true`. Если элемента `pushTestResult` ещё не показывал результат проверки пользователя (флаг `pushTestShown` ложен) — ничего не менять; результат проверки выводится только из `pushTest`.

## pushFill(j)
Заполняет форму из `j`, только если `!pushFilled || !pushDirty` и фокус (`document.activeElement`) не в одном из полей формы (`pushEnabled`, `pushHost`, `pushPort`, `pushToken`, `pushTokenClear`): `pushEnabled.checked = j.enabled === true`; `pushHost.value = typeof j.host === 'string' ? j.host : ''`; `pushPort.value = String(j.port)` если `j.port` число; `pushToken.value = ''`; `pushTokenClear.checked = false`; затем `pushFilled = true; pushDirty = false`.

## pushRefresh()
`async`. Если `pushBusy` — выход. `pushBusy = true`; в `try`: `j = await fetchJSON('/api/push')`; `pushLast = j`; `pushRender(j)`; `pushFill(j)`. В `catch (e)`: текст `(e && e.status === 401) ? 'Нужна авторизация' : 'Нет связи с платой'` в элемент `p_state`. В `finally` `pushBusy = false`.

## pushSave()
`async`. Собрать `f` из полей: `enabled` (`pushEnabled.checked`), `host`, `port` (значения), `token` (`pushToken.value`), `clearToken` (`pushTokenClear.checked`). `tokenSet = !!(pushLast && pushLast.token_set === true)`. `v = AFPUSH.validateForm(f, tokenSet)`. Если `!v.ok`: `pushSay('pushResult','bad',v.error)`, фокус в элемент `v.field` (если есть), выход. Иначе: кнопку `btnPushSave` отключить, `pushSay('pushResult','', 'сохраняю…')`; `r = await postJSON('/api/push', v.body)` в `try`; если `r.ok`: текст `(r.data && r.data.persisted === false) ? 'применено, но не записано в память платы (действует до перезагрузки)' : 'сохранено'`, класс `ok`; `pushDirty = false; pushFilled = false;` затем `await pushRefresh()`. Иначе `pushSay('pushResult','bad', AFPUSH.apiErrText(r.status, r.data && r.data.err))`. `catch` -> `pushSay('pushResult','bad','нет связи с платой')`. В `finally`: кнопку включить; `pushToken.value = ''` (всегда очищать поле токена); `pushTokenClear.checked = false`.

## pushTest()
`async`. Кнопку `btnPushTest` отключить; `pushSay('pushTestResult','','проверка идёт…')`. `r = await postJSON('/api/push/test', {})`. Если `!r.ok`: `pushSay('pushTestResult','bad', AFPUSH.apiErrText(r.status, r.data && r.data.err))` и выход (в `finally` кнопку включить). Иначе до 20 раз: пауза 1000 мс (`await new Promise(function (ok) { setTimeout(ok, 1000); })`), `j = await fetchJSON('/api/push')`, `pushLast = j`, `t = j.test`; если `t && t.state === 'done'` — `txt = AFPUSH.testText(t)`, `cls = (t.err ? 'bad' : 'ok')`, `pushSay('pushTestResult', cls, txt)`, `pushRender(j)`, выход; по исчерпании попыток `pushSay('pushTestResult','bad','приёмник не ответил за 20 с')`. Любое исключение -> `pushSay('pushTestResult','bad','нет связи с платой')`. В `finally` кнопку включить.

## Сброс позиции
- `pushResetOpen()`: если `pushLast` нет — `pushSay('pushResult','bad','нет данных от платы')` и выход; иначе `pushDlgText.textContent = AFPUSH.resetConfirmText(pushLast)`; `pushBackdrop.hidden = false`; `pushDlg.hidden = false`; фокус на `pushDlgCancel`.
- `pushResetClose()`: скрыть `pushDlg` и `pushBackdrop`.
- `pushResetOk()`: `async`; `pushResetClose()`; `r = await postJSON('/api/push/reset', {confirm: true})`; `r.ok` -> `pushSay('pushResult','ok','позиция отправки сброшена')`, `await pushRefresh()`; иначе `pushSay('pushResult','bad', AFPUSH.apiErrText(r.status, r.data && r.data.err))`; исключение -> `'нет связи с платой'`.

## initPush()
Если нет элемента `btnPushSave` — выход. Навесить: `click` на `btnPushSave` -> `pushSave`; `btnPushTest` -> `pushTest`; `btnPushReset` -> `pushResetOpen`; `pushDlgCancel` и `pushBackdrop` -> `pushResetClose`; `pushDlgOk` -> `pushResetOk`. На `input` и `change` полей `pushEnabled`, `pushHost`, `pushPort`, `pushToken`, `pushTokenClear` ставить `pushDirty = true`. Клавиша Escape при открытом диалоге закрывает его (`keydown` на `document`). Вызвать `pushRefresh()`. Единственный периодический владелец `/api/push` — `initPush`: `setInterval` каждые 5000 мс, если `document.visibilityState === 'visible'` и элемент `sec-db` существует и не скрыт (`!pEl('sec-db').hidden`). `activate('db')` вызывает немедленный `pushRefresh()`, обычный `system-nav.refresh()` его не вызывает. Сохранение и сброс позиции сохраняют `pushRefresh(true)`.
