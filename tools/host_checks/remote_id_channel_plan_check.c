#undef NDEBUG
#include "remote_id_channel_plan.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static bool zero(const void *p, size_t size)
{ const uint8_t *b = p; for (size_t i = 0; i < size; ++i) if (b[i]) return false; return true; }

static void invalid_arguments(void)
{
    uint8_t channels[65]; memset(channels, 6, sizeof(channels));
    cerberus_remote_id_channel_plan_t plan;
    CHECK(!cerberus_remote_id_channel_plan_init(NULL, channels, 1, 1, 1, 0));
    for (unsigned defect = 0; defect < 8; ++defect) {
        memset(&plan, 0xa5, sizeof(plan));
        uint8_t bad[] = {6, 149, 0};
        CHECK(!cerberus_remote_id_channel_plan_init(&plan,
            defect == 0 ? NULL : defect == 7 ? bad : channels,
            defect == 1 ? 0 : defect == 2 ? 65 : defect == 7 ? 3 : 1,
            defect == 3 ? 0 : defect == 4 ? (uint32_t)INT32_MAX + 1U : 1,
            defect == 5 ? 0 : defect == 6 ? (uint32_t)INT32_MAX + 1U : 1, 0));
        CHECK(zero(&plan, sizeof(plan)));
    }
    cerberus_remote_id_channel_slot_t slot; memset(&slot, 0xa5, sizeof(slot));
    CHECK(cerberus_remote_id_channel_plan_poll(NULL, 0, &slot) == CERBERUS_REMOTE_ID_PLAN_INVALID);
    CHECK(zero(&slot, sizeof(slot)));
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 0, &slot) == CERBERUS_REMOTE_ID_PLAN_INVALID);
    CHECK(cerberus_remote_id_channel_plan_init(&plan, channels, 64, 1, 10, 0));
    CHECK(plan.count == 1 && plan.channels[0] == 6);
    cerberus_remote_id_channel_plan_t before = plan;
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 0, NULL) == CERBERUS_REMOTE_ID_PLAN_INVALID);
    CHECK(!memcmp(&plan, &before, sizeof(plan)));
}

static void rotation(const uint8_t *channels, size_t count, unsigned rounds, uint32_t start)
{
    /* Independent materialized expected schedule: optional priority prefix,
     * followed by exactly one successive background channel per cycle. */
    uint8_t priority[3]; unsigned priorities = 0;
    bool six = false, one49 = false;
    for (size_t i = 0; i < count; ++i) { six |= channels[i] == 6; one49 |= channels[i] == 149; }
    if (six) priority[priorities++] = 6;
    if (one49) priority[priorities++] = 149;
    if (six) priority[priorities++] = 6;
    unsigned visits[256] = {0};
    struct { uint32_t guard1; cerberus_remote_id_channel_plan_t plan; uint32_t guard2; } guarded;
    memset(&guarded, 0, sizeof(guarded)); guarded.guard1 = 0x12345678; guarded.guard2 = 0xabcdef01;
    CHECK(cerberus_remote_id_channel_plan_init(&guarded.plan, channels, count, 17, 1000000, start));
    uint32_t now = start;
    for (unsigned cycle = 0; cycle < rounds * count; ++cycle) {
        for (unsigned slot_index = 0; slot_index <= priorities; ++slot_index) {
            cerberus_remote_id_channel_slot_t slot;
            CHECK(cerberus_remote_id_channel_plan_poll(&guarded.plan, now, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
            CHECK(slot.channel == (slot_index < priorities ? priority[slot_index] : channels[cycle % count]));
            CHECK(slot.kind == (slot_index < priorities ? CERBERUS_REMOTE_ID_CHANNEL_PRIORITY : CERBERUS_REMOTE_ID_CHANNEL_BACKGROUND));
            CHECK(slot.dwell_remaining_ms == 17);
            CHECK(slot.budget_remaining_ms == 1000000U - (uint32_t)(now - start));
            if (slot.kind == CERBERUS_REMOTE_ID_CHANNEL_BACKGROUND) ++visits[slot.channel];
            cerberus_remote_id_channel_plan_t before = guarded.plan;
            cerberus_remote_id_channel_slot_t repeated;
            CHECK(cerberus_remote_id_channel_plan_poll(&guarded.plan, now, &repeated) == CERBERUS_REMOTE_ID_PLAN_WAIT);
            CHECK(!memcmp(&slot, &repeated, sizeof(slot)));
            CHECK(!memcmp(&before, &guarded.plan, sizeof(before)));
            ++now;
            CHECK(cerberus_remote_id_channel_plan_poll(&guarded.plan, now, &repeated) == CERBERUS_REMOTE_ID_PLAN_WAIT);
            CHECK(repeated.channel == slot.channel && repeated.dwell_remaining_ms == 16);
            now += 16;
        }
    }
    for (size_t i = 0; i < count; ++i) CHECK(visits[channels[i]] == rounds);
    CHECK(guarded.guard1 == 0x12345678 && guarded.guard2 == 0xabcdef01);
}

static void plans_and_fairness(void)
{
    /* Representative approved plans cover both priority channels, either
     * missing priority, and neither. The pure planner does not approve them. */
    const uint8_t base[] = {1, 6, 36, 149, 177};
    for (unsigned mask = 1; mask < 32; ++mask) {
        uint8_t selected[5]; size_t count = 0;
        for (unsigned i = 0; i < 5; ++i) if (mask & (1U << i)) selected[count++] = base[i];
        rotation(selected, count, 4, 0);
        rotation(selected, count, 4, UINT32_MAX - 55U);
        for (size_t i = 0; i < count / 2; ++i) {
            uint8_t t = selected[i]; selected[i] = selected[count - 1 - i]; selected[count - 1 - i] = t;
        }
        rotation(selected, count, 2, 21);
    }
    /* Exercise the hard bound with opaque pre-approved channel identifiers.
     * This asserts only bounded storage, not SDK/regulatory validity. */
    uint8_t maximum[64]; for (unsigned i = 0; i < 64; ++i) maximum[i] = (uint8_t)(i + 1);
    rotation(maximum, 64, 2, UINT32_MAX - 100);
    uint8_t duplicates[] = {149, 6, 149, 177, 6, 1};
    const uint8_t expected[] = {149, 6, 177, 1};
    cerberus_remote_id_channel_plan_t plan;
    CHECK(cerberus_remote_id_channel_plan_init(&plan, duplicates, sizeof(duplicates), 1, 100, 0));
    CHECK(plan.count == sizeof(expected) && !memcmp(plan.channels, expected, sizeof(expected)));
    memset(duplicates, 0, sizeof(duplicates));
    CHECK(!memcmp(plan.channels, expected, sizeof(expected)));
}

static void timing_and_deadlines(void)
{
    const uint8_t channels[] = {1, 6, 149};
    cerberus_remote_id_channel_plan_t plan;
    cerberus_remote_id_channel_slot_t slot;
    for (unsigned wrapped = 0; wrapped < 2; ++wrapped) {
        uint32_t start = wrapped ? UINT32_MAX - 123U : 0;
        CHECK(cerberus_remote_id_channel_plan_init(&plan, channels, 3, 100, 250, start));
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, start, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
        CHECK(slot.channel == 6 && slot.dwell_remaining_ms == 100 && slot.budget_remaining_ms == 250);
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, start + 99U, &slot) == CERBERUS_REMOTE_ID_PLAN_WAIT);
        CHECK(slot.dwell_remaining_ms == 1 && slot.budget_remaining_ms == 151);
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, start + 100U, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
        CHECK(slot.channel == 149 && slot.dwell_remaining_ms == 100);
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, start + 200U, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
        CHECK(slot.channel == 6 && slot.dwell_remaining_ms == 50);
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, start + 249U, &slot) == CERBERUS_REMOTE_ID_PLAN_WAIT);
        CHECK(slot.dwell_remaining_ms == 1 && slot.budget_remaining_ms == 1);
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, start + 250U, &slot) == CERBERUS_REMOTE_ID_PLAN_DONE);
        CHECK(zero(&slot, sizeof(slot)));
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, start, &slot) == CERBERUS_REMOTE_ID_PLAN_DONE);
    }
    CHECK(cerberus_remote_id_channel_plan_init(&plan, channels, 3, 100, 1000, 0));
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 0, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 350, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
    CHECK(slot.channel == 149 && slot.dwell_remaining_ms == 100 && slot.budget_remaining_ms == 650);
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 449, &slot) == CERBERUS_REMOTE_ID_PLAN_WAIT);
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 1000, &slot) == CERBERUS_REMOTE_ID_PLAN_DONE);
    CHECK(cerberus_remote_id_channel_plan_init(&plan, channels, 3, 100, 25, 0));
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 10, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
    CHECK(slot.dwell_remaining_ms == 15 && slot.budget_remaining_ms == 15);
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 25, &slot) == CERBERUS_REMOTE_ID_PLAN_DONE);
    CHECK(cerberus_remote_id_channel_plan_init(&plan, channels, 3, 100, 25, 0));
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, 25, &slot) == CERBERUS_REMOTE_ID_PLAN_DONE);
    CHECK(cerberus_remote_id_channel_plan_init(&plan, channels, 3, INT32_MAX, INT32_MAX, UINT32_MAX));
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, UINT32_MAX, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
    CHECK(slot.dwell_remaining_ms == INT32_MAX);
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, (uint32_t)INT32_MAX - 2U, &slot) == CERBERUS_REMOTE_ID_PLAN_WAIT);
    CHECK(slot.dwell_remaining_ms == 1);
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, (uint32_t)INT32_MAX - 1U, &slot) == CERBERUS_REMOTE_ID_PLAN_DONE);
    for (unsigned backward = 0; backward < 2; ++backward) {
        CHECK(cerberus_remote_id_channel_plan_init(&plan, channels, 3, 1, INT32_MAX, 100));
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, backward ? 99 : 100U + UINT32_C(0x80000000), &slot) == CERBERUS_REMOTE_ID_PLAN_INVALID);
        CHECK(zero(&slot, sizeof(slot)));
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, 100, &slot) == CERBERUS_REMOTE_ID_PLAN_INVALID);
    }
}

static void window_continuity(void)
{
    uint8_t channels[64];
    for (unsigned i = 0; i < 63; ++i) channels[i] = (uint8_t)(i + 1);
    channels[63] = 149;
    unsigned visits[256] = {0};
    uint8_t next = 149;
    uint32_t now = UINT32_MAX - 9000U;
    cerberus_remote_id_channel_plan_t plan;
    cerberus_remote_id_channel_slot_t slot;
    /* Eight 8-second windows provide 64 background slots, one for every
     * channel even though each window restarts the preferred-channel phase. */
    for (unsigned window = 0; window < 8; ++window) {
        CHECK(cerberus_remote_id_channel_plan_init(&plan, channels, 64, 250, 8000, now));
        CHECK(cerberus_remote_id_channel_plan_seek_background(&plan, next));
        CHECK(cerberus_remote_id_channel_plan_next_background(&plan) == next);
        for (unsigned tick = 0; tick < 32; ++tick) {
            CHECK(cerberus_remote_id_channel_plan_poll(&plan, now + tick * 250U, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
            if (slot.kind == CERBERUS_REMOTE_ID_CHANNEL_BACKGROUND) ++visits[slot.channel];
        }
        CHECK(cerberus_remote_id_channel_plan_poll(&plan, now + 8000U, &slot) == CERBERUS_REMOTE_ID_PLAN_DONE);
        next = cerberus_remote_id_channel_plan_next_background(&plan);
        CHECK(next != 0);
        CHECK(!cerberus_remote_id_channel_plan_seek_background(&plan, 1));
        now += 9000U;
    }
    for (unsigned i = 0; i < 64; ++i) CHECK(visits[channels[i]] == 1);
    CHECK(next == 149);
    const uint8_t reordered[] = {177, 149, 6, 1};
    CHECK(cerberus_remote_id_channel_plan_init(&plan, reordered, 4, 1, 10, now));
    CHECK(cerberus_remote_id_channel_plan_seek_background(&plan, next));
    CHECK(cerberus_remote_id_channel_plan_next_background(&plan) == 149);
    cerberus_remote_id_channel_plan_t saved = plan;
    CHECK(!cerberus_remote_id_channel_plan_seek_background(&plan, 40));
    CHECK(!cerberus_remote_id_channel_plan_seek_background(&plan, 0));
    CHECK(!memcmp(&saved, &plan, sizeof(plan)));
    CHECK(cerberus_remote_id_channel_plan_poll(&plan, now, &slot) == CERBERUS_REMOTE_ID_PLAN_TUNE);
    saved = plan;
    CHECK(!cerberus_remote_id_channel_plan_seek_background(&plan, 1));
    CHECK(!memcmp(&saved, &plan, sizeof(plan)));
    const uint8_t changed[] = {36, 40};
    CHECK(cerberus_remote_id_channel_plan_init(&plan, changed, 2, 1, 10, now));
    CHECK(!cerberus_remote_id_channel_plan_seek_background(&plan, next));
    CHECK(cerberus_remote_id_channel_plan_next_background(&plan) == 36);
    CHECK(!cerberus_remote_id_channel_plan_seek_background(NULL, 1));
    CHECK(cerberus_remote_id_channel_plan_next_background(NULL) == 0);
    memset(&plan, 0, sizeof(plan));
    CHECK(cerberus_remote_id_channel_plan_next_background(&plan) == 0);
}

static void follow_rotation_and_configuration(void)
{
    const uint8_t base[] = {1,6,36,149,177};
    for(unsigned mask=1;mask<32;++mask){
        uint8_t channels[5];size_t count=0;uint8_t priority[3];unsigned priorities=0;
        for(unsigned i=0;i<5;++i)if(mask&(1U<<i))channels[count++]=base[i];
        if(mask&2U)priority[priorities++]=6;
        if(mask&8U)priority[priorities++]=149;
        if(mask&2U)priority[priorities++]=6;
        for(size_t target=0;target<count;++target){
            cerberus_remote_id_channel_plan_t plan;cerberus_remote_id_channel_slot_t slot;
            uint32_t now=UINT32_MAX-40U;
            CHECK(cerberus_remote_id_channel_plan_init(&plan,channels,count,10,100000,now));
            CHECK(cerberus_remote_id_channel_plan_configure_follow(&plan,channels[target]));
            unsigned background=0,targets=0;
            for(unsigned cycle=0;cycle<2U*count;++cycle){
                for(unsigned phase=0;phase<=priorities;++phase){
                    CHECK(cerberus_remote_id_channel_plan_poll(&plan,now,&slot)==CERBERUS_REMOTE_ID_PLAN_TUNE);
                    if(phase<priorities){CHECK(slot.kind==CERBERUS_REMOTE_ID_CHANNEL_PRIORITY&&slot.channel==priority[phase]);}
                    else if(!(cycle&1U)){
                        CHECK(slot.kind==CERBERUS_REMOTE_ID_CHANNEL_TARGET&&slot.channel==channels[target]);++targets;
                    }else{
                        CHECK(slot.kind==CERBERUS_REMOTE_ID_CHANNEL_BACKGROUND&&slot.channel==channels[background++]);
                    }
                    CHECK(cerberus_remote_id_channel_plan_next_background(&plan)==channels[background%count]);
                    now+=10U;
                }
            }
            CHECK(targets==count&&background==count);
            cerberus_remote_id_channel_plan_t saved=plan;
            CHECK(!cerberus_remote_id_channel_plan_configure_follow(&plan,0));
            CHECK(!memcmp(&saved,&plan,sizeof(plan)));
        }
    }
    cerberus_remote_id_channel_plan_t plan;cerberus_remote_id_channel_slot_t slot;
    const uint8_t channels[]={6,149,177};
    CHECK(!cerberus_remote_id_channel_plan_configure_follow(NULL,6));
    memset(&plan,0,sizeof(plan));CHECK(!cerberus_remote_id_channel_plan_configure_follow(&plan,6));
    CHECK(cerberus_remote_id_channel_plan_init(&plan,channels,3,250,760,0));
    CHECK(cerberus_remote_id_channel_plan_seek_background(&plan,149));
    CHECK(cerberus_remote_id_channel_plan_configure_follow(&plan,177));
    cerberus_remote_id_channel_plan_t saved=plan;
    CHECK(!cerberus_remote_id_channel_plan_configure_follow(&plan,40));
    CHECK(!memcmp(&saved,&plan,sizeof(plan)));
    for(uint32_t t=0;t<=750;t+=250)CHECK(cerberus_remote_id_channel_plan_poll(&plan,t,&slot)==CERBERUS_REMOTE_ID_PLAN_TUNE);
    CHECK(slot.kind==CERBERUS_REMOTE_ID_CHANNEL_TARGET&&slot.channel==177&&slot.dwell_remaining_ms==10);
    CHECK(cerberus_remote_id_channel_plan_next_background(&plan)==149);
    CHECK(cerberus_remote_id_channel_plan_poll(&plan,760,&slot)==CERBERUS_REMOTE_ID_PLAN_DONE);
    CHECK(!cerberus_remote_id_channel_plan_configure_follow(&plan,0));
    CHECK(cerberus_remote_id_channel_plan_init(&plan,channels,3,1,10,0));
    CHECK(cerberus_remote_id_channel_plan_configure_follow(&plan,177));
    CHECK(cerberus_remote_id_channel_plan_configure_follow(&plan,0));
    for(uint32_t t=0;t<4;++t)CHECK(cerberus_remote_id_channel_plan_poll(&plan,t,&slot)==CERBERUS_REMOTE_ID_PLAN_TUNE);
    CHECK(slot.kind==CERBERUS_REMOTE_ID_CHANNEL_BACKGROUND&&slot.channel==6);
}

static void follow_background_across_windows(void)
{
    uint8_t channels[64];for(unsigned i=0;i<63;++i)channels[i]=(uint8_t)(i+1);channels[63]=149;
    unsigned visits[256]={0};uint8_t next=149;uint32_t now=UINT32_MAX-9000U;
    cerberus_remote_id_channel_plan_t plan;cerberus_remote_id_channel_slot_t slot;
    for(unsigned window=0;window<16;++window){
        CHECK(cerberus_remote_id_channel_plan_init(&plan,channels,64,250,8000,now));
        CHECK(cerberus_remote_id_channel_plan_seek_background(&plan,next));
        CHECK(cerberus_remote_id_channel_plan_configure_follow(&plan,149));
        unsigned targets=0,background=0;
        for(unsigned tick=0;tick<32;++tick){
            CHECK(cerberus_remote_id_channel_plan_poll(&plan,now+tick*250U,&slot)==CERBERUS_REMOTE_ID_PLAN_TUNE);
            if(slot.kind==CERBERUS_REMOTE_ID_CHANNEL_BACKGROUND){++visits[slot.channel];++background;}
            if(slot.kind==CERBERUS_REMOTE_ID_CHANNEL_TARGET){CHECK(slot.channel==149);++targets;}
        }
        CHECK(targets==4&&background==4);
        CHECK(cerberus_remote_id_channel_plan_poll(&plan,now+8000U,&slot)==CERBERUS_REMOTE_ID_PLAN_DONE);
        next=cerberus_remote_id_channel_plan_next_background(&plan);now+=9000U;
    }
    for(unsigned i=0;i<64;++i)CHECK(visits[channels[i]]==1);
    CHECK(next==149);
}

int main(void)
{
    invalid_arguments(); plans_and_fairness(); timing_and_deadlines(); window_continuity();
    follow_rotation_and_configuration();follow_background_across_windows();
    printf("Remote ID channel planner: %u checks passed; state=%zu bytes\n", checks, sizeof(cerberus_remote_id_channel_plan_t));
    return 0;
}
