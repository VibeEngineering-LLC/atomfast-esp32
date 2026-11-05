#pragma once
/* e16-LOG: журнал событий платы - публичный интерфейс (чистая логика - af_log.h). */
#include <stddef.h>
#include <stdint.h>
#include "af_log.h"

/* Инициализация (кольцо в PSRAM, при отказе - малое кольцо во внутренней RAM). Вызывать самым первым в app_main. */
void af_log_init(void);
/* Событие: level 1 info, 2 warn, 3 error. Безопасно из любой задачи; не из ISR. Пароли/токены/MAC/IP маскируются. */
void af_log_event(int level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* Из колбэка неудачной аллокации: только атомарные счётчики, без блокировок. */
void af_log_note_alloc_fail(size_t size, uint32_t caps);
/* Раз в секунду из задачи af_store: ленивое подключение флеша, батч-запись по расписанию. */
void af_log_tick(void);
/* Принудительный сброс на флеш (перед перезагрузкой). */
void af_log_flush_now(void);
/* Очистка журнала (файлы + ОЗУ); нумерация продолжается. 0 готово, -1 нет доступа. */
int af_log_clear(uint32_t *files, uint32_t *bytes);

/* Обход записей с seq > since и уровнем >= min_level, не более limit; cb вернул !=0 - стоп. Возврат: число отданных. */
typedef int (*af_log_emit_t)(void *ctx, const af_log_ent_t *e);
uint32_t af_log_iter(uint32_t since, int min_level, uint32_t limit, af_log_emit_t cb, void *ctx);

typedef struct {
    uint32_t next_seq, flushed, lost, suppressed, collapsed;
    uint32_t files, bytes, wr_err, dropped_space, flushes, fail_n, pending;
    int fs_ready;
} af_log_stats_t;
void af_log_stats(af_log_stats_t *s);
