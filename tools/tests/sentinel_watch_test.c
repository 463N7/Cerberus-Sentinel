#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sentinel_watch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static sentinel_watch_t watch;
static const sentinel_watch_match_t axon = {SENTINEL_WATCH_AXON,
    SENTINEL_WATCH_CONFIDENCE_MEDIUM, "test-axon", "Possible Axon", "Test signature"};

static bool observe(unsigned id, const sentinel_watch_match_t *match, uint32_t now)
{
    uint8_t address[6] = {0,1,2,3,4,(uint8_t)id};
    return sentinel_watch_observe(&watch, SENTINEL_WATCH_RADIO_BLE, address, 1,
        "Example", 7, -52, match, now);
}

static void test_dedup_and_rearm(void)
{
    sentinel_watch_init(&watch);
    assert(observe(1, &axon, 0));
    assert(watch.history_count == 1 && watch.matches == 1 && watch.queue_count == 1);
    for (uint32_t t = 1000; t <= 400000; t += 1000) assert(!observe(1, &axon, t));
    assert(watch.history_count == 1 && watch.matches == 1 && watch.queue_count == 1);
    assert(watch.history[0].sightings == 401);
    assert(observe(1, &axon, 700000)); /* Return after five minutes absent. */
    assert(watch.queue_count == 2 && watch.matches == 1);
    assert(!observe(2, &axon, 700001)); /* Different identity, same category cooldown. */
    assert(watch.matches == 2 && watch.suppressed == 1 && watch.history_count == 2);
    assert(observe(3, &axon, 720000));
    assert(watch.queue_count == 3 && watch.observations == 404);
}

static void test_queue_and_history_bounds(void)
{
    sentinel_watch_init(&watch);
    for (unsigned i = 0; i < 100; ++i) observe(i, &axon, i * 21000U);
    assert(watch.history_count == SENTINEL_WATCH_HISTORY_CAPACITY);
    assert(watch.queue_count == SENTINEL_WATCH_QUEUE_CAPACITY);
    assert(watch.matches == 100 && watch.queue_dropped == 92);
    assert(sentinel_watch_history(&watch, 0)->address[5] == 99);
    assert(sentinel_watch_history(&watch, 31)->address[5] == 68);
    assert(sentinel_watch_history(&watch, 32) == NULL);
    assert(sentinel_watch_history(NULL, 0) == NULL);
    uint32_t t = 3000000;
    for (unsigned i = 0; i < SENTINEL_WATCH_QUEUE_CAPACITY; ++i) {
        assert(sentinel_watch_tick(&watch, t));
        assert(watch.alert_active && watch.alert.address[5] == i);
        assert(!sentinel_watch_tick(&watch, t + 7999));
        t += SENTINEL_WATCH_ALERT_MS;
    }
    assert(sentinel_watch_tick(&watch, t));
    assert(!watch.alert_active && watch.queue_count == 0);
    assert(!sentinel_watch_tick(&watch, t + 10000));
}

static void test_rollover_and_isolation(void)
{
    sentinel_watch_init(&watch);
    uint32_t now = UINT32_MAX - 4000U;
    assert(observe(1, &axon, now));
    assert(sentinel_watch_tick(&watch, now));
    assert(!sentinel_watch_tick(&watch, now + 7999U));
    assert(sentinel_watch_tick(&watch, now + 8000U));
    assert(!observe(2, &axon, now + 19999U));
    assert(observe(3, &axon, now + 20000U));
    assert(observe(1, &axon, now + SENTINEL_WATCH_RETURN_MS));
    sentinel_watch_dismiss(&watch);
    assert(!watch.alert_active);
    assert(sentinel_watch_tick(&watch, now + SENTINEL_WATCH_RETURN_MS));
    sentinel_watch_init(&watch);
    assert(!watch.history_count && !watch.observations && !watch.queue_count);
    /* Categories, radio and address type must not collapse different hints. */
    assert(observe(1, &axon, 0));
    sentinel_watch_match_t tracker = axon;
    tracker.category = SENTINEL_WATCH_TRACKER;
    assert(observe(1, &tracker, 1));
    uint8_t addr[6] = {0,1,2,3,4,1};
    sentinel_watch_observe(&watch, SENTINEL_WATCH_RADIO_WIFI, addr, 1, "A", 1, -60, &axon, 2);
    sentinel_watch_observe(&watch, SENTINEL_WATCH_RADIO_BLE, addr, 0, "A", 1, -60, &axon, 3);
    assert(watch.history_count == 4 && watch.matches == 4);
}

static void test_owned_names_and_invalid_inputs(void)
{
    sentinel_watch_init(&watch);
    uint8_t addr[6] = {1};
    char name[40]; memset(name, 'X', sizeof(name)); name[1] = '\n'; name[2] = (char)255;
    assert(sentinel_watch_observe(&watch, SENTINEL_WATCH_RADIO_BLE, addr, 0, name, sizeof(name), -71, &axon, 0));
    memset(name, 'Q', sizeof(name));
    assert(strlen(watch.history[0].name) == 32 && strcmp(watch.history[0].name, watch.queue[0].name) == 0);
    assert(watch.history[0].name[0] == 'X' && watch.history[0].name[1] == '?' && watch.history[0].name[2] == '?');
    watch.history[0].sightings = UINT16_MAX;
    watch.observations = UINT32_MAX;
    assert(!sentinel_watch_observe(&watch, SENTINEL_WATCH_RADIO_BLE, addr, 0, NULL, 0, -65, &axon, 1));
    assert(watch.history[0].sightings == UINT16_MAX && watch.observations == UINT32_MAX);
    assert(!sentinel_watch_observe(&watch, SENTINEL_WATCH_RADIO_BLE, NULL, 0, NULL, 0, 0, &axon, 1));
    assert(!sentinel_watch_observe(&watch, 99, addr, 0, NULL, 0, 0, &axon, 1));
    assert(!sentinel_watch_observe(&watch, SENTINEL_WATCH_RADIO_BLE, addr, 0, NULL, 5, 0, &axon, 1));
    sentinel_watch_match_t invalid = axon;
    invalid.category = SENTINEL_WATCH_CATEGORY_COUNT;
    assert(!observe(2, &invalid, 99999));
    invalid = axon; invalid.confidence = SENTINEL_WATCH_CONFIDENCE_NONE;
    assert(!observe(2, &invalid, 99999));
    invalid = axon; invalid.rule_id = NULL;
    assert(!observe(2, &invalid, 99999));
    assert(!observe(2, NULL, 99999));
    assert(watch.history_count == 1);
}

static void test_category_preferences(void)
{
    sentinel_watch_init(&watch);
    for (int category=SENTINEL_WATCH_NONE+1; category<SENTINEL_WATCH_CATEGORY_COUNT; ++category)
        assert(sentinel_watch_get_mode(&watch,category)==(category==SENTINEL_WATCH_BEACON?
            SENTINEL_WATCH_MODE_HISTORY:SENTINEL_WATCH_MODE_ALERT));
    sentinel_watch_match_t beacon=axon; beacon.category=SENTINEL_WATCH_BEACON;
    assert(!observe(1,&beacon,0));
    assert(watch.history_count==1 && watch.matches==1 && watch.observations==1 && !watch.queue_count);
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_AXON,SENTINEL_WATCH_MODE_OFF);
    assert(!observe(2,&axon,1));
    assert(watch.history_count==1 && watch.matches==1 && watch.observations==2);
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_AXON,SENTINEL_WATCH_MODE_HISTORY);
    assert(!observe(2,&axon,2));
    assert(watch.history_count==2 && watch.matches==2 && watch.observations==3 && !watch.queue_count);
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_AXON,SENTINEL_WATCH_MODE_ALERT);
    assert(observe(3,&axon,3));
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_AXON,SENTINEL_WATCH_MODE_HISTORY);
    assert(!watch.queue_count && watch.history_count==3);
    sentinel_watch_entry_t original=watch.history[1];
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_AXON,SENTINEL_WATCH_MODE_OFF);
    assert(!observe(2,&axon,500000));
    assert(!memcmp(&original,&watch.history[1],sizeof(original))); /* OFF retains old evidence without updates. */
    sentinel_watch_t saved=watch;
    sentinel_watch_set_mode(NULL,SENTINEL_WATCH_AXON,SENTINEL_WATCH_MODE_OFF);
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_NONE,SENTINEL_WATCH_MODE_ALERT);
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_CATEGORY_COUNT,SENTINEL_WATCH_MODE_ALERT);
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_AXON,(sentinel_watch_mode_t)-1);
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_AXON,(sentinel_watch_mode_t)99);
    assert(!memcmp(&saved,&watch,sizeof(saved)));
    assert(sentinel_watch_get_mode(NULL,SENTINEL_WATCH_AXON)==SENTINEL_WATCH_MODE_OFF);
    assert(sentinel_watch_get_mode(&watch,(sentinel_watch_category_t)-1)==SENTINEL_WATCH_MODE_OFF);
}

static void test_filter_removes_wrapped_queue_and_current_alert(void)
{
    sentinel_watch_init(&watch);
    for(unsigned i=0;i<8;++i){
        sentinel_watch_match_t match=axon; match.category=(sentinel_watch_category_t)(i%3+1);
        assert(observe(i,&match,i*21000U));
    }
    for(unsigned i=0;i<5;++i){
        assert(sentinel_watch_tick(&watch,300000+i*8000U));
        assert(watch.alert.address[5]==i);
    }
    assert(watch.queue_head==5 && watch.queue_count==3 && watch.alert_active);
    for(unsigned i=0;i<5;++i){
        sentinel_watch_match_t match=axon; match.category=(sentinel_watch_category_t)(i%3+1);
        assert(observe(8+i,&match,500000+i*21000U));
    }
    assert(watch.queue_count==8);
    sentinel_watch_entry_t expected[8];unsigned count=0;
    for(unsigned i=0;i<watch.queue_count;++i){
        const sentinel_watch_entry_t *entry=&watch.queue[(watch.queue_head+i)%SENTINEL_WATCH_QUEUE_CAPACITY];
        if(entry->match.category!=SENTINEL_WATCH_AXON)expected[count++]=*entry;
    }
    sentinel_watch_set_mode(&watch,SENTINEL_WATCH_AXON,SENTINEL_WATCH_MODE_HISTORY);
    assert(watch.queue_count==count && watch.alert_active && watch.history_count==13);
    for(unsigned i=0;i<count;++i)
        assert(!memcmp(&expected[i],&watch.queue[(watch.queue_head+i)%SENTINEL_WATCH_QUEUE_CAPACITY],sizeof(expected[i])));
    sentinel_watch_category_t current=watch.alert.match.category;
    sentinel_watch_set_mode(&watch,current,SENTINEL_WATCH_MODE_OFF);
    assert(!watch.alert_active && watch.history_count==13);
    for(unsigned i=0;i<watch.queue_count;++i)
        assert(watch.queue[(watch.queue_head+i)%SENTINEL_WATCH_QUEUE_CAPACITY].match.category!=current);
    while(watch.queue_count){assert(sentinel_watch_tick(&watch,900000));sentinel_watch_dismiss(&watch);}
    assert(!sentinel_watch_tick(&watch,900000));
}

static void test_owned_metadata_and_snapshot_isolation(void)
{
    sentinel_watch_init(&watch);
    uint8_t address[6]={1,2,3,4,5,6};
    sentinel_watch_match_t drone=axon;drone.category=SENTINEL_WATCH_DRONE;
    sentinel_watch_details_t details={.protocol={.kind=SENTINEL_WATCH_PROTOCOL_DRONE,
        .transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN,
        .flags=SENTINEL_WATCH_PROTOCOL_HAS_ID|SENTINEL_WATCH_PROTOCOL_HAS_POSITION|SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE,
        .latitude_e7=-1234567,.longitude_e7=7654321,.altitude_dm=-5,
        .altitude_kind=SENTINEL_WATCH_ALTITUDE_GEODETIC},.has_activity=true,
        .activity={4000,500,480,20,120}};
    strcpy(details.protocol.id,"ORIGINAL-REMOTE-ID");
    assert(sentinel_watch_observe_details(&watch,SENTINEL_WATCH_RADIO_WIFI,address,0,
        "Original",8,-50,&drone,&details,1000));
    sentinel_watch_entry_t original=watch.history[0];
    memset(&details,0xa5,sizeof(details));
    assert(!memcmp(&original.details,&watch.history[0].details,sizeof(details)));
    assert(!memcmp(&original.details,&watch.queue[0].details,sizeof(details)));
    details=original.details;strcpy(details.protocol.id,"NEW-REPORT");details.protocol.latitude_e7=90000000;
    assert(!sentinel_watch_observe_details(&watch,SENTINEL_WATCH_RADIO_WIFI,address,0,
        "Updated",7,-20,&drone,&details,2000));
    assert(watch.history[0].details.protocol.latitude_e7==90000000 && watch.history[0].sightings==2);
    assert(!memcmp(&original,&watch.queue[0],sizeof(original)));
    assert(sentinel_watch_tick(&watch,3000));assert(!memcmp(&original,&watch.alert,sizeof(original)));
    assert(!sentinel_watch_observe(&watch,SENTINEL_WATCH_RADIO_WIFI,address,0,"Latest",6,-70,&drone,4000));
    sentinel_watch_details_t empty={0};
    assert(!memcmp(&empty,&watch.history[0].details,sizeof(empty)));
    assert(!memcmp(&original,&watch.alert,sizeof(original)));
    memset(&details,0,sizeof(details));memset(details.protocol.id,'X',sizeof(details.protocol.id));
    address[5]++;
    assert(sentinel_watch_observe_details(&watch,SENTINEL_WATCH_RADIO_ENVIRONMENT,address,0,
        "Area",4,0,&drone,&details,30000));
    assert(watch.history[1].details.protocol.id[20]==0 && strlen(watch.history[1].details.protocol.id)==20);
}

int main(void)
{
    test_dedup_and_rearm();
    test_queue_and_history_bounds();
    test_rollover_and_isolation();
    test_owned_names_and_invalid_inputs();
    test_category_preferences(); test_filter_removes_wrapped_queue_and_current_alert();
    test_owned_metadata_and_snapshot_isolation();
    puts("PASS: Watch bounded queues/history, category modes, wrapped queue removal, immutable owned metadata, deduplication, pacing, rollover, sanitization and saturation.");
    return 0;
}
