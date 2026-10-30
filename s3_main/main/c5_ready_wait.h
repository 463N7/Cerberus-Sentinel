#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The epoch is latched by the GPIO ISR, so a complete low/high pulse during
 * an SPI transfer remains visible after that transfer returns. Each transfer
 * must take its own baseline; a prior transfer's edge is never reusable. */
typedef struct {
    uint32_t (*now_us)(void);
    void (*pause_us)(uint32_t);
    bool (*level_high)(void);
    uint32_t (*fall_epoch)(void);
} c5_ready_wait_io_t;

static inline bool c5_ready_wait_high(const c5_ready_wait_io_t *io,
    uint32_t timeout_us, uint32_t poll_us)
{
    uint32_t started = io->now_us();
    for (;;) {
        if (io->level_high()) return true;
        uint32_t elapsed = io->now_us() - started;
        if (elapsed >= timeout_us) return false;
        uint32_t remaining = timeout_us - elapsed;
        io->pause_us(remaining < poll_us ? remaining : poll_us);
    }
}

static inline bool c5_ready_wait_fall(const c5_ready_wait_io_t *io,
    uint32_t baseline, uint32_t timeout_us, uint32_t poll_us)
{
    uint32_t started = io->now_us();
    for (;;) {
        /* Do not accept the low level alone: its ISR might still be pending
         * and would then be mistaken for the following transfer's edge. */
        if (io->fall_epoch() != baseline) return true;
        uint32_t elapsed = io->now_us() - started;
        if (elapsed >= timeout_us) return false;
        uint32_t remaining = timeout_us - elapsed;
        io->pause_us(remaining < poll_us ? remaining : poll_us);
    }
}
