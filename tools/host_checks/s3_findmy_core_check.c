#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "s3_findmy_core.h"

static const uint8_t fmna[] = {0x03, 0x03, 0x44, 0xFD};
static const uint8_t dult[] = {
    0x11, 0x07, 0x85, 0x2A, 0x9F, 0x57, 0xC5, 0x2A, 0xED, 0x88,
    0x26, 0xC2, 0xF4, 0x12, 0x01, 0x00, 0x19, 0x15,
};

static void classification_checks(void)
{
    uint8_t apple[31] = {0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19};
    assert(cerberus_findmy_classify_ad(apple, sizeof(apple), true, false) ==
        CERBERUS_FINDMY_PROFILE_AIRTAG);
    assert(cerberus_findmy_classify_ad(fmna, sizeof(fmna), true, false) ==
        CERBERUS_FINDMY_PROFILE_FMNA);
    assert(cerberus_findmy_classify_ad(dult, sizeof(dult), true, false) ==
        CERBERUS_FINDMY_PROFILE_DULT);
    assert(cerberus_findmy_classify_ad(apple, sizeof(apple), false, false) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_classify_ad(apple, sizeof(apple), true, true) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_classify_ad(apple, sizeof(apple) - 1U, true, false) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    apple[3] = 0x01;
    assert(cerberus_findmy_classify_ad(apple, sizeof(apple), true, false) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    apple[3] = 0x00;
    uint8_t mixed[sizeof(apple) + sizeof(fmna)];
    memcpy(mixed, apple, sizeof(apple));
    memcpy(mixed + sizeof(apple), fmna, sizeof(fmna));
    assert(cerberus_findmy_classify_ad(mixed, sizeof(mixed), true, false) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    uint8_t hidden[] = {0x03, 0x09, 0x44, 0xFD};
    assert(cerberus_findmy_classify_ad(hidden, sizeof(hidden), true, false) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    uint8_t malformed[] = {0x03, 0x03, 0x44, 0xFD, 0x03, 0x09, 0x41};
    assert(cerberus_findmy_classify_ad(malformed, sizeof(malformed), true, false) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    uint8_t odd_16[] = {0x04, 0x03, 0x44, 0xFD, 0x00};
    assert(cerberus_findmy_classify_ad(odd_16, sizeof(odd_16), true, false) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    uint8_t wrong_endian[sizeof(dult)];
    memcpy(wrong_endian, dult, sizeof(dult));
    wrong_endian[2] = 0x15;
    assert(cerberus_findmy_classify_ad(wrong_endian, sizeof(wrong_endian), true, false) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    uint8_t nonzero_after_end[] = {0x03, 0x03, 0x44, 0xFD, 0x00, 0x01};
    assert(cerberus_findmy_classify_ad(nonzero_after_end,
        sizeof(nonzero_after_end), true, false) == CERBERUS_FINDMY_PROFILE_NONE);
}

static bool observe(cerberus_findmy_queue_t *queue, uint8_t id,
    uint8_t type, int8_t rssi, uint32_t now_ms)
{
    const uint8_t address[6] = {id, 2U, 3U, 4U, 5U, 6U};
    return cerberus_findmy_queue_observe(queue, address, type, rssi,
        fmna, sizeof(fmna), true, false, now_ms);
}

static void rotation_and_cooldown_checks(void)
{
    cerberus_findmy_queue_t queue;
    cerberus_findmy_queue_init(&queue);
    cerberus_findmy_queue_start(&queue);
    cerberus_findmy_queue_begin_sweep(&queue, 100U);
    assert(observe(&queue, 1U, 0U, -70, 100U));
    assert(observe(&queue, 2U, 0U, -60, 100U));
    assert(observe(&queue, 3U, 0U, -50, 100U));
    assert(queue.count == 3U);
    cerberus_findmy_selection_t selected;
    assert(cerberus_findmy_queue_claim_next(&queue, 100U, &selected));
    assert(selected.address[0] == 1U && selected.profile == CERBERUS_FINDMY_PROFILE_FMNA);
    uint32_t ticket = selected.ticket;
    assert(!cerberus_findmy_queue_claim_next(&queue, 100U, &selected));
    assert(!cerberus_findmy_queue_finish_attempt(&queue, ticket + 1U, true, 100U));
    assert(cerberus_findmy_queue_finish_attempt(&queue, ticket, true, 100U));
    assert(!cerberus_findmy_queue_claim_next(&queue, 100U, &selected));
    cerberus_findmy_queue_begin_sweep(&queue, 200U);
    assert(observe(&queue, 1U, 0U, -45, 200U));
    assert(observe(&queue, 2U, 0U, -60, 200U));
    assert(observe(&queue, 3U, 0U, -50, 200U));
    assert(queue.candidates[0].rssi == -45);
    assert(cerberus_findmy_queue_claim_next(&queue, 200U, &selected));
    assert(selected.address[0] == 2U);
    assert(cerberus_findmy_queue_finish_attempt(&queue, selected.ticket, false, 200U));
    cerberus_findmy_queue_begin_sweep(&queue, 300U);
    assert(observe(&queue, 1U, 0U, -45, 300U));
    assert(observe(&queue, 2U, 0U, -60, 300U));
    assert(observe(&queue, 3U, 0U, -50, 300U));
    assert(cerberus_findmy_queue_claim_next(&queue, 300U, &selected));
    assert(selected.address[0] == 3U);
    assert(cerberus_findmy_queue_finish_attempt(&queue, selected.ticket, true, 300U));
    cerberus_findmy_queue_begin_sweep(&queue, 400U);
    assert(observe(&queue, 1U, 0U, -45, 400U));
    assert(observe(&queue, 2U, 0U, -60, 400U));
    assert(observe(&queue, 3U, 0U, -50, 400U));
    assert(!cerberus_findmy_queue_claim_next(&queue, 400U, &selected));
    cerberus_findmy_queue_begin_sweep(&queue, 5200U);
    assert(observe(&queue, 1U, 0U, -45, 5200U));
    assert(observe(&queue, 2U, 0U, -60, 5200U));
    assert(observe(&queue, 3U, 0U, -50, 5200U));
    assert(cerberus_findmy_queue_claim_next(&queue, 5200U, &selected));
    assert(selected.address[0] == 2U);
    assert(cerberus_findmy_queue_finish_attempt(&queue, selected.ticket, false, 5200U));
    cerberus_findmy_queue_begin_sweep(&queue, 15200U);
    assert(observe(&queue, 1U, 0U, -45, 15200U));
    assert(observe(&queue, 2U, 0U, -60, 15200U));
    assert(observe(&queue, 3U, 0U, -50, 15200U));
    assert(cerberus_findmy_queue_claim_next(&queue, 15200U, &selected));
    assert(selected.address[0] == 2U);
    assert(cerberus_findmy_queue_finish_attempt(&queue, selected.ticket, true, 15200U));
    cerberus_findmy_queue_begin_sweep(&queue, 30300U);
    assert(observe(&queue, 1U, 0U, -45, 30300U));
    assert(observe(&queue, 2U, 0U, -60, 30300U));
    assert(observe(&queue, 3U, 0U, -50, 30300U));
    assert(cerberus_findmy_queue_claim_next(&queue, 30300U, &selected));
    assert(selected.address[0] == 3U);
    assert(cerberus_findmy_queue_finish_attempt(&queue, selected.ticket, true, 30300U));
}

static void identity_capacity_and_stop_checks(void)
{
    cerberus_findmy_queue_t queue;
    cerberus_findmy_queue_init(&queue);
    cerberus_findmy_queue_start(&queue);
    cerberus_findmy_queue_begin_sweep(&queue, 100U);
    assert(observe(&queue, 7U, 0U, -60, 100U));
    assert(observe(&queue, 7U, 1U, -61, 100U));
    assert(queue.count == 2U); /* Address type is part of identity. */
    cerberus_findmy_queue_begin_sweep(&queue, 101U);
    cerberus_findmy_selection_t not_seen;
    assert(!cerberus_findmy_queue_claim_next(&queue, 101U, &not_seen));
    cerberus_findmy_queue_start(&queue);
    cerberus_findmy_queue_begin_sweep(&queue, 100U);
    for (uint8_t i = 0U; i < CERBERUS_FINDMY_MAX_CANDIDATES; ++i)
        assert(observe(&queue, i, 0U, -60, (uint32_t)i + 100U));
    assert(queue.count == CERBERUS_FINDMY_MAX_CANDIDATES);
    assert(observe(&queue, 99U, 0U, -55, 200U));
    assert(queue.count == CERBERUS_FINDMY_MAX_CANDIDATES);
    assert(queue.candidates[0].address[0] == 99U); /* Oldest is evicted. */
    cerberus_findmy_selection_t selected;
    assert(cerberus_findmy_queue_claim_next(&queue, 200U, &selected));
    uint32_t obsolete_ticket = selected.ticket;
    cerberus_findmy_queue_stop(&queue);
    assert(!queue.active && queue.count == 0U && !queue.in_flight);
    assert(!cerberus_findmy_queue_finish_attempt(&queue, obsolete_ticket, true, 201U));
    cerberus_findmy_queue_start(&queue);
    cerberus_findmy_queue_begin_sweep(&queue, 300U);
    assert(observe(&queue, 3U, 0U, -50, 300U));
    assert(cerberus_findmy_queue_claim_next(&queue, 300U, &selected));
    assert(selected.ticket != obsolete_ticket);
    assert(!cerberus_findmy_queue_finish_attempt(&queue, obsolete_ticket, true, 301U));
    assert(cerberus_findmy_queue_finish_attempt(&queue, selected.ticket, true, 301U));
}

static void expiry_timeout_and_wrap_checks(void)
{
    cerberus_findmy_queue_t queue;
    cerberus_findmy_queue_init(&queue);
    cerberus_findmy_queue_start(&queue);
    const uint32_t first = UINT32_MAX - 1000U;
    cerberus_findmy_queue_begin_sweep(&queue, first);
    assert(observe(&queue, 1U, 0U, -50, first));
    cerberus_findmy_queue_begin_sweep(&queue, first + 1001U);
    assert(queue.count == 1U);
    assert(observe(&queue, 1U, 0U, -50, first + 1001U));
    cerberus_findmy_selection_t selected;
    assert(cerberus_findmy_queue_claim_next(&queue, first + 1001U, &selected));
    uint32_t lost_ticket = selected.ticket;
    cerberus_findmy_queue_begin_sweep(&queue,
        first + 1001U + CERBERUS_FINDMY_ATTEMPT_TIMEOUT_MS);
    assert(!queue.in_flight && queue.candidates[0].failures == 1U);
    assert(!cerberus_findmy_queue_finish_attempt(&queue, lost_ticket, true,
        first + 1001U + CERBERUS_FINDMY_ATTEMPT_TIMEOUT_MS));
    assert(!cerberus_findmy_queue_claim_next(&queue,
        first + 1001U + CERBERUS_FINDMY_ATTEMPT_TIMEOUT_MS, &selected));
    cerberus_findmy_queue_begin_sweep(&queue,
        first + 1001U + CERBERUS_FINDMY_STALE_MS);
    assert(queue.count == 0U);
}

int main(void)
{
    classification_checks();
    rotation_and_cooldown_checks();
    identity_capacity_and_stop_checks();
    expiry_timeout_and_wrap_checks();
    puts("Find My complete-AD profiles, bounded fair queue, backoff, expiry and Stop: pass");
}
