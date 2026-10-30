#include "s3_ble_discovery_session.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#error Assertions must remain enabled.
#endif

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "CHECK failed line %u: %s\n", __LINE__, #x); exit(1); \
} } while (0)
enum { IO_ERROR = -81, BUSY_ERROR = -82, MISSING = -83 };

typedef struct {
    cerberus_ble_discovery_observation_t meta;
    uint64_t next_epoch, committed_epoch, committed_revision, final_epoch;
    unsigned calls, starts, copies, polls, stops, releases, publications;
    unsigned copy_errors, poll_errors, stop_errors, release_errors;
    bool attached, hold_stop, rejected_start, accepted_start_error;
    bool bad_start, instant_complete;
    unsigned corrupt;
    char last;
} fake_t;

static cerberus_ble_discovery_operation_t ok(void)
{
    return (cerberus_ble_discovery_operation_t){0};
}

static cerberus_ble_discovery_operation_t error(int32_t value)
{
    return (cerberus_ble_discovery_operation_t){.error = value};
}

static cerberus_ble_discovery_operation_t absent(void)
{
    return (cerberus_ble_discovery_operation_t){.error = MISSING, .released = true};
}

static void call(fake_t *f, char name)
{
    ++f->calls;
    f->last = name;
}

static void finish(fake_t *f)
{
    f->meta.state = CERBERUS_BLE_DISCOVERY_COMPLETE;
    f->meta.owned = false;
    f->meta.stopping = false;
    ++f->meta.revision;
}

static cerberus_ble_discovery_operation_t start(void *context, uint8_t family,
    uint64_t previous, cerberus_ble_discovery_observation_t *out)
{
    fake_t *f = context;
    call(f, 'S');
    ++f->starts;
    CHECK(family < CERBERUS_BLE_DISCOVERY_FAMILY_COUNT);
    CHECK(previous ? (f->attached && previous == f->meta.scan_epoch) : !f->attached);
    if (previous) {
        CHECK(!f->meta.owned && !f->meta.stopping);
        CHECK(f->meta.state == CERBERUS_BLE_DISCOVERY_COMPLETE);
        CHECK(f->meta.family == family);
        CHECK(f->final_epoch == previous); /* Final commit precedes every rearm. */
    }
    if (f->rejected_start || f->next_epoch == UINT64_MAX) return error(BUSY_ERROR);
    if (f->bad_start) return ok();
    f->attached = true;
    f->meta = (cerberus_ble_discovery_observation_t){
        .scan_epoch = ++f->next_epoch, .family = family,
        .state = CERBERUS_BLE_DISCOVERY_RUNNING, .owned = true,
    };
    if (f->instant_complete) finish(f);
    *out = f->meta;
    return f->accepted_start_error ? error(IO_ERROR) : ok();
}

static void corrupt(fake_t *f, cerberus_ble_discovery_observation_t *out)
{
    *out = f->meta;
    switch (f->corrupt) {
        case 1: ++out->scan_epoch; break;
        case 2: out->family = (uint8_t)((out->family + 1U) % 7U); break;
        case 3: out->state = 255; break;
        case 4: out->revision = 0; break;
        case 5: out->owned = false; out->stopping = false; break;
        default: break;
    }
}

static cerberus_ble_discovery_operation_t poll(void *context, uint64_t epoch,
    cerberus_ble_discovery_observation_t *out)
{
    fake_t *f = context;
    call(f, 'P');
    ++f->polls;
    if (!f->attached || epoch != f->meta.scan_epoch) return absent();
    if (f->poll_errors) { --f->poll_errors; return error(IO_ERROR); }
    corrupt(f, out);
    return ok();
}

static cerberus_ble_discovery_operation_t copy(void *context, uint64_t epoch,
    uint8_t family, cerberus_ble_discovery_observation_t *out)
{
    fake_t *f = context;
    call(f, 'C');
    ++f->copies;
    if (!f->attached || epoch != f->meta.scan_epoch) return absent();
    CHECK(family == f->meta.family);
    if (f->copy_errors) { --f->copy_errors; return error(IO_ERROR); }
    if (f->meta.state == CERBERUS_BLE_DISCOVERY_ERROR ||
        f->meta.state == CERBERUS_BLE_DISCOVERY_IDLE) return error(IO_ERROR);
    corrupt(f, out);
    if (!f->corrupt) {
        /* The caller commits a distinct immutable presentation only when its
         * epoch/revision changes. Repeated reads do not invent fresh data. */
        if (f->committed_epoch != epoch || f->committed_revision != out->revision) {
            ++f->publications;
            f->committed_epoch = epoch;
            f->committed_revision = out->revision;
        }
        if (out->state == CERBERUS_BLE_DISCOVERY_COMPLETE) f->final_epoch = epoch;
    }
    return ok();
}

static cerberus_ble_discovery_operation_t stop(void *context, uint64_t epoch,
    cerberus_ble_discovery_observation_t *out)
{
    fake_t *f = context;
    call(f, 'X');
    ++f->stops;
    if (!f->attached || epoch != f->meta.scan_epoch) return absent();
    if (f->stop_errors) { --f->stop_errors; return error(IO_ERROR); }
    if (!f->meta.owned && !f->meta.stopping) { *out = f->meta; return ok(); }
    if (f->hold_stop) f->meta.stopping = true;
    else if (f->meta.state == CERBERUS_BLE_DISCOVERY_ERROR) {
        f->meta.owned = false;
        f->meta.stopping = false;
    } else {
        finish(f);
        f->meta.state = CERBERUS_BLE_DISCOVERY_IDLE; /* Native cancelled finish. */
    }
    *out = f->meta;
    return ok();
}

static cerberus_ble_discovery_operation_t release(void *context, uint64_t epoch)
{
    fake_t *f = context;
    call(f, 'R');
    ++f->releases;
    if (!f->attached || epoch != f->meta.scan_epoch) return absent();
    CHECK(!f->meta.owned && !f->meta.stopping);
    if (f->release_errors) { --f->release_errors; return error(IO_ERROR); }
    f->attached = false;
    return ok();
}

static cerberus_ble_discovery_ops_t ops(fake_t *f)
{
    return (cerberus_ble_discovery_ops_t){f, start, poll, copy, stop, release};
}

static void tick(cerberus_ble_discovery_session_t *s, fake_t *f, uint64_t now)
{
    unsigned before = f->calls;
    cerberus_ble_discovery_ops_t api = ops(f);
    cerberus_ble_discovery_session_tick(s, now, &api);
    CHECK(f->calls - before <= 1U);
}

static void begin(cerberus_ble_discovery_session_t *s, fake_t *f, uint8_t family)
{
    memset(f, 0, sizeof(*f));
    cerberus_ble_discovery_session_init(s, family);
    CHECK(cerberus_ble_discovery_session_start(s));
    CHECK(f->calls == 0);
    tick(s, f, 0);
    CHECK(f->starts == 1 && s->epoch == 1 && s->enabled);
    CHECK(s->active_family == family && !s->faulted);
}

static void idle_and_continuous(void)
{
    cerberus_ble_discovery_session_t s;
    fake_t f = {0};
    cerberus_ble_discovery_session_init(&s, 0);
    for (unsigned family = 0; family < 7; ++family) {
        CHECK(cerberus_ble_discovery_session_select(&s, (uint8_t)family));
        tick(&s, &f, family);
        CHECK(f.calls == 0 && !s.enabled && !s.epoch);
    }
    CHECK(!cerberus_ble_discovery_session_select(&s, 7));
    CHECK(s.requested_family == 6);
    begin(&s, &f, 3);
    tick(&s, &f, 0);
    CHECK(f.copies == 1 && f.publications == 1);
    for (uint64_t time = 1; time < 200; ++time) tick(&s, &f, time);
    CHECK(f.copies == 1);
    tick(&s, &f, 200);
    CHECK(f.copies == 2 && f.publications == 1);
    ++f.meta.revision;
    tick(&s, &f, 400);
    CHECK(f.publications == 2 && s.published_revision == 1);

    uint64_t now = 600;
    for (unsigned sweep = 0; sweep < 300; ++sweep) {
        const uint64_t old = s.epoch;
        finish(&f);
        tick(&s, &f, now);
        CHECK(f.last == 'C' && s.final_copied && s.epoch == old);
        tick(&s, &f, ++now);
        CHECK(f.last == 'S' && s.epoch == old + 1 && !s.final_copied);
        now += 8000;
    }
    CHECK(s.completed_sweeps == 300 && f.releases == 0 && f.stops == 0);
    CHECK(s.epoch == 301); /* Public byte rollover cannot alias local identity. */
}

static void stop_and_family(void)
{
    for (unsigned action = 0; action < 4; ++action) {
        cerberus_ble_discovery_session_t s;
        fake_t f;
        begin(&s, &f, 2);
        tick(&s, &f, 0);
        /* Stop, frozen selection, Back and sleep share the same immediate
         * disable contract; none may wait until the next refresh deadline. */
        cerberus_ble_discovery_session_stop(&s);
        CHECK(!s.enabled && s.drain_requested);
        tick(&s, &f, 1);
        CHECK(f.last == 'X' && f.stops == 1);
        for (uint64_t time = 2; time < 200; ++time) {
            cerberus_ble_discovery_session_stop(&s); /* Idempotent every loop. */
            tick(&s, &f, time);
        }
        CHECK(f.stops == 1 && f.releases == 0);
        tick(&s, &f, 201);
        CHECK(f.releases == 1 && cerberus_ble_discovery_session_released(&s));
        for (uint64_t time = 202; time < 1000; ++time) tick(&s, &f, time);
        CHECK(f.starts == 1 && !f.attached);
        CHECK(cerberus_ble_discovery_session_select(&s, 4));
        tick(&s, &f, 1000);
        CHECK(f.starts == 1);
        CHECK(cerberus_ble_discovery_session_start(&s));
        tick(&s, &f, 1001);
        CHECK(f.starts == 2 && s.active_family == 4);
    }

    cerberus_ble_discovery_session_t s;
    fake_t f;
    begin(&s, &f, 0);
    f.hold_stop = true;
    CHECK(cerberus_ble_discovery_session_select(&s, 1));
    CHECK(s.requested_family == 1 && s.active_family == 0);
    tick(&s, &f, 1);
    CHECK(f.last == 'X' && s.epoch == 1);
    CHECK(cerberus_ble_discovery_session_select(&s, 6));
    tick(&s, &f, 2);
    CHECK(f.starts == 1 && f.releases == 0 && s.active_family == 0);
    /* A released host with a queued old event still cannot be freed/rearmed. */
    f.meta.owned = false;
    f.meta.state = CERBERUS_BLE_DISCOVERY_COMPLETE;
    for (uint64_t now = 202; now <= 802; now += 200) tick(&s, &f, now);
    CHECK(f.releases == 0 && f.starts == 1);
    finish(&f);
    tick(&s, &f, 1002);
    tick(&s, &f, 1202);
    tick(&s, &f, 1402);
    CHECK(f.releases == 1 && f.starts == 2 && s.active_family == 6);
    CHECK(!s.drain_requested);

    /* Stop before a committed COMPLETE is rearmed wins immediately. */
    finish(&f);
    tick(&s, &f, 1602);
    CHECK(s.final_copied);
    cerberus_ble_discovery_session_stop(&s);
    tick(&s, &f, 1603);
    CHECK(f.last == 'R' && f.starts == 2 && !s.epoch);
}

static void uncertain_start_and_errors(void)
{
    cerberus_ble_discovery_session_t s;
    fake_t f = {.rejected_start = true};
    cerberus_ble_discovery_session_init(&s, 1);
    CHECK(cerberus_ble_discovery_session_start(&s));
    tick(&s, &f, 0);
    CHECK(s.faulted && !s.enabled && !s.epoch && !f.attached);
    for (uint64_t now = 1; now < 100; ++now) tick(&s, &f, now);
    CHECK(f.calls == 1 && !f.polls && !f.stops);

    memset(&f, 0, sizeof(f));
    f.accepted_start_error = true;
    cerberus_ble_discovery_session_init(&s, 5);
    cerberus_ble_discovery_session_start(&s);
    tick(&s, &f, 0);
    CHECK(s.epoch == 1 && s.faulted && !s.enabled && f.attached);
    CHECK(!cerberus_ble_discovery_session_released(&s));
    tick(&s, &f, 1);
    CHECK(f.stops == 1 && f.last == 'X');
    tick(&s, &f, 201);
    CHECK(!s.epoch && !f.attached && f.starts == 1);

    begin(&s, &f, 0);
    f.meta.state = CERBERUS_BLE_DISCOVERY_ERROR;
    f.meta.error = -99;
    tick(&s, &f, 1); /* Copy refuses ERROR; scoped status recovers its cause. */
    tick(&s, &f, 251);
    CHECK(s.faulted && s.error == -99 && s.epoch == 1);
    tick(&s, &f, 451);
    CHECK(f.stops == 1 && !f.meta.owned);
    tick(&s, &f, 651);
    CHECK(!s.epoch && f.releases == 1 && f.starts == 1);

    begin(&s, &f, 0);
    f.copy_errors = 1;
    tick(&s, &f, 1);
    CHECK(s.enabled && !s.faulted && !f.publications);
    tick(&s, &f, 251);
    tick(&s, &f, 451);
    CHECK(s.enabled && !s.faulted && !s.error && f.publications == 1);

    f.copy_errors = 3;
    for (uint64_t now = 651; now < 3000 && !s.faulted; now += 250) tick(&s, &f, now);
    CHECK(s.faulted && !s.enabled && s.epoch);
    CHECK(s.failures == 3 && f.publications == 1);
    for (uint64_t now = 3000; now < 5000; now += 250) tick(&s, &f, now);
    CHECK(!s.epoch && f.starts == 1 && f.releases == 1);
}

static void cleanup_faults(void)
{
    cerberus_ble_discovery_session_t s;
    fake_t f;
    begin(&s, &f, 4);
    f.stop_errors = 2;
    f.poll_errors = 8;
    f.release_errors = 2;
    cerberus_ble_discovery_session_stop(&s);
    tick(&s, &f, 1);
    CHECK(f.stops == 1 && s.epoch);
    for (uint64_t now = 251; now < 1001; now += 250) tick(&s, &f, now);
    CHECK(f.stops == 1 && f.releases == 0);
    tick(&s, &f, 1001);
    CHECK(f.stops == 2); /* Status failures cannot prevent scoped Stop retry. */
    for (uint64_t now = 1251; now <= 2001; now += 250) tick(&s, &f, now);
    CHECK(f.stops == 3 && !f.meta.owned && s.epoch);
    tick(&s, &f, 2201);
    CHECK(f.releases == 1 && s.epoch && f.attached);
    tick(&s, &f, 2451);
    CHECK(f.releases == 2 && s.epoch);
    tick(&s, &f, 2701);
    CHECK(f.releases == 3 && !s.epoch && !f.attached);
    CHECK(f.starts == 1);

    begin(&s, &f, 0);
    f.meta.state = CERBERUS_BLE_DISCOVERY_IDLE;
    f.meta.owned = false;
    tick(&s, &f, 1); /* No active/complete bank can be copied. */
    tick(&s, &f, 251); /* Exact IDLE is a terminal fault without Stop intent. */
    CHECK(s.faulted && !s.enabled && s.observation.state == CERBERUS_BLE_DISCOVERY_IDLE);
    tick(&s, &f, 252);
    CHECK(!s.epoch && f.releases == 1 && f.starts == 1 && !f.stops);

    /* An explicit new Start during cleanup is queued, never a cancellation
     * of the old Stop or permission to overlap two procedures. */
    begin(&s, &f, 0);
    f.hold_stop = true;
    cerberus_ble_discovery_session_stop(&s);
    tick(&s, &f, 1);
    CHECK(cerberus_ble_discovery_session_start(&s));
    CHECK(s.drain_requested && s.enabled);
    tick(&s, &f, 2);
    CHECK(f.starts == 1);
    finish(&f);
    tick(&s, &f, 202);
    tick(&s, &f, 402);
    tick(&s, &f, 602);
    CHECK(f.releases == 1 && f.starts == 2 && s.epoch == 2);
}

static void identity_and_time(void)
{
    for (unsigned defect = 1; defect <= 5; ++defect) {
        cerberus_ble_discovery_session_t s;
        fake_t f;
        begin(&s, &f, 2);
        f.meta.revision = 9;
        tick(&s, &f, 0);
        CHECK(s.published_revision == 9);
        f.corrupt = defect;
        tick(&s, &f, 200);
        CHECK(s.faulted && !s.enabled && s.epoch == 1);
        CHECK(s.published_revision == 9 && f.publications == 1);
        f.corrupt = 0;
        tick(&s, &f, 201);
        CHECK(f.stops == 1 && f.meta.scan_epoch == 1);
        tick(&s, &f, 401);
        CHECK(!s.epoch && f.starts == 1);
    }

    cerberus_ble_discovery_session_t s;
    fake_t f;
    begin(&s, &f, 0);
    /* A later foreign attachment is never adopted or stopped. */
    f.meta.scan_epoch = 99;
    f.meta.family = 3;
    tick(&s, &f, 1);
    CHECK(s.faulted && !s.epoch && !s.enabled);
    CHECK(f.stops == 0 && f.releases == 0 && f.meta.scan_epoch == 99);

    begin(&s, &f, 1);
    tick(&s, &f, UINT64_C(0x100000000) + 1000);
    CHECK(!s.faulted && f.copies == 1);
    tick(&s, &f, UINT64_C(0x100000000) + 999);
    CHECK(s.faulted && s.error == CERBERUS_BLE_DISCOVERY_BAD_CLOCK);
    CHECK(f.stops == 1 && !s.enabled);
    tick(&s, &f, UINT64_C(0x100000000) + 1200);
    CHECK(!s.epoch && f.starts == 1);

    memset(&f, 0, sizeof(f));
    f.next_epoch = UINT64_MAX - 2U;
    cerberus_ble_discovery_session_init(&s, 0);
    cerberus_ble_discovery_session_start(&s);
    tick(&s, &f, 0);
    CHECK(s.epoch == UINT64_MAX - 1U);
    finish(&f);
    tick(&s, &f, 1);
    tick(&s, &f, 2);
    CHECK(s.epoch == UINT64_MAX);
    finish(&f);
    tick(&s, &f, 3);
    tick(&s, &f, 4);
    CHECK(s.faulted && s.epoch == UINT64_MAX && !s.enabled);
    tick(&s, &f, 5);
    CHECK(!s.epoch && f.releases == 1);

    begin(&s, &f, 0);
    f.copy_errors = 1;
    tick(&s, &f, UINT64_MAX - 100U);
    CHECK(s.next_action_ms == UINT64_MAX);
    tick(&s, &f, UINT64_MAX - 1U);
    CHECK(f.polls == 0);
    tick(&s, &f, UINT64_MAX);
    CHECK(f.polls == 1);
}

static void adversarial_controls(void)
{
    /* Deterministic independent event stream. The fake adapter checks actual
     * ownership and final commit before every rearm across rapid commands. */
    cerberus_ble_discovery_session_t s;
    fake_t f;
    begin(&s, &f, 0);
    uint32_t random = 7;
    uint64_t now = 0;
    for (unsigned i = 0; i < 4000; ++i) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        switch ((random >> 16) % 9U) {
            case 0: cerberus_ble_discovery_session_stop(&s); break;
            case 1: cerberus_ble_discovery_session_start(&s); break;
            case 2: cerberus_ble_discovery_session_select(&s, (uint8_t)(random % 7U)); break;
            case 3: if (f.attached && f.meta.owned) finish(&f); break;
            case 4: if (f.attached && f.meta.state == CERBERUS_BLE_DISCOVERY_RUNNING) ++f.meta.revision; break;
            default: break;
        }
        now += (random & 255U) + 1U;
        tick(&s, &f, now);
        CHECK(s.requested_family < 7 && s.active_family < 7);
        CHECK(!s.epoch || (f.attached && s.epoch == f.meta.scan_epoch));
        CHECK(!s.epoch || s.active_family == f.meta.family);
        CHECK(s.enabled || !s.epoch || s.drain_requested);
    }
    cerberus_ble_discovery_session_stop(&s);
    for (unsigned i = 0; i < 20; ++i) tick(&s, &f, now += 250);
    CHECK(!s.epoch && !f.attached);
}

int main(void)
{
    idle_and_continuous();
    stop_and_family();
    uncertain_start_and_errors();
    cleanup_faults();
    identity_and_time();
    adversarial_controls();
    cerberus_ble_discovery_session_t s;
    cerberus_ble_discovery_session_init(&s, 255);
    CHECK(s.requested_family == 0);
    cerberus_ble_discovery_session_tick(&s, 0, NULL);
    CHECK(s.faulted && s.error == CERBERUS_BLE_DISCOVERY_BAD_OPS);
    CHECK(cerberus_ble_discovery_session_released(NULL));
    CHECK(!cerberus_ble_discovery_session_start(NULL));
    CHECK(!cerberus_ble_discovery_session_select(NULL, 0));
    cerberus_ble_discovery_session_init(NULL, 0);
    cerberus_ble_discovery_session_stop(NULL);
    cerberus_ble_discovery_session_tick(NULL, 0, NULL);
    printf("BLE discovery controller: %u checks passed; session=%zu bytes\n", checks, sizeof(s));
    return 0;
}
