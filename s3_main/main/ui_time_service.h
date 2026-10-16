#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_NOTIFICATION_CAPACITY 8U
#define CERBERUS_NOTIFICATION_TEXT_MAX 40U

typedef struct {
    uint32_t uptime_seconds;
    char text[CERBERUS_NOTIFICATION_TEXT_MAX];
} cerberus_notification_t;

void cerberus_ui_time_service_init(int64_t now_ms);
bool cerberus_ui_time_service_tick(int64_t now_ms);
void cerberus_ui_timer_start(uint32_t seconds, int64_t now_ms);
void cerberus_ui_timer_cancel(void);
bool cerberus_ui_timer_is_running(void);
uint32_t cerberus_ui_timer_remaining(int64_t now_ms);
uint32_t cerberus_ui_uptime_seconds(int64_t now_ms);
size_t cerberus_ui_notification_count(void);
bool cerberus_ui_notification_get(size_t newest_offset, cerberus_notification_t *notification);
void cerberus_ui_notifications_clear(void);
/* UI-task caller; copy bounded local notification text to the volatile ring. */
void cerberus_ui_notification_add(int64_t now_ms, const char *text);
