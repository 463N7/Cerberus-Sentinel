#include "ui_time_service.h"

#include <stdio.h>
#include <string.h>

static int64_t service_start_ms;
static int64_t timer_deadline_ms;
static bool timer_running;
static cerberus_notification_t notifications[CERBERUS_NOTIFICATION_CAPACITY];
static size_t notification_count;
static size_t notification_next;

static void add_notification(int64_t now_ms, const char *text)
{
    cerberus_notification_t *entry = &notifications[notification_next];
    memset(entry, 0, sizeof(*entry));
    entry->uptime_seconds = cerberus_ui_uptime_seconds(now_ms);
    snprintf(entry->text, sizeof(entry->text), "%s", text);
    notification_next = (notification_next + 1U) % CERBERUS_NOTIFICATION_CAPACITY;
    if (notification_count < CERBERUS_NOTIFICATION_CAPACITY) {
        ++notification_count;
    }
}

void cerberus_ui_notification_add(int64_t now_ms, const char *text)
{
    if (text != NULL) add_notification(now_ms, text);
}

void cerberus_ui_time_service_init(int64_t now_ms)
{
    service_start_ms = now_ms;
    timer_deadline_ms = 0;
    timer_running = false;
    notification_count = 0U;
    notification_next = 0U;
    memset(notifications, 0, sizeof(notifications));
}

bool cerberus_ui_time_service_tick(int64_t now_ms)
{
    if (!timer_running || now_ms < timer_deadline_ms) {
        return false;
    }
    timer_running = false;
    timer_deadline_ms = 0;
    add_notification(now_ms, "TIMER COMPLETE");
    return true;
}

void cerberus_ui_timer_start(uint32_t seconds, int64_t now_ms)
{
    if (seconds == 0U || seconds > 24U * 60U * 60U) {
        return;
    }
    timer_deadline_ms = now_ms + (int64_t)seconds * 1000LL;
    timer_running = true;
}

void cerberus_ui_timer_cancel(void)
{
    timer_running = false;
    timer_deadline_ms = 0;
}

bool cerberus_ui_timer_is_running(void)
{
    return timer_running;
}

uint32_t cerberus_ui_timer_remaining(int64_t now_ms)
{
    if (!timer_running || now_ms >= timer_deadline_ms) {
        return 0U;
    }
    return (uint32_t)((timer_deadline_ms - now_ms + 999LL) / 1000LL);
}

uint32_t cerberus_ui_uptime_seconds(int64_t now_ms)
{
    return now_ms > service_start_ms ? (uint32_t)((now_ms - service_start_ms) / 1000LL) : 0U;
}

size_t cerberus_ui_notification_count(void)
{
    return notification_count;
}

bool cerberus_ui_notification_get(size_t newest_offset, cerberus_notification_t *notification)
{
    if (notification == NULL || newest_offset >= notification_count) {
        return false;
    }
    size_t index = (notification_next + CERBERUS_NOTIFICATION_CAPACITY - 1U - newest_offset) %
        CERBERUS_NOTIFICATION_CAPACITY;
    *notification = notifications[index];
    return true;
}

void cerberus_ui_notifications_clear(void)
{
    notification_count = 0U;
    notification_next = 0U;
    memset(notifications, 0, sizeof(notifications));
}
