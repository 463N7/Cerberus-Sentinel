#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "s3_ble_tracker.h"

static void check_rssi_boundaries(void)
{
    const uint8_t address[6]={1,2,3,4,5,6};
    assert(CERBERUS_BLE_RSSI_UNAVAILABLE==127);
    assert(!cerberus_ble_rssi_valid(INT_MIN)&&!cerberus_ble_rssi_valid(INT_MAX));
    assert(!cerberus_ble_rssi_valid(276)); /* No narrowing256+20 into a valid sample. */
    for(int value=-128;value<=127;++value){
        cerberus_s3_ble_tracker_t tracker,before;
        cerberus_s3_ble_tracker_begin(&tracker,address,2U);
        assert(cerberus_s3_ble_tracker_observe(&tracker,address,2U,-60,77U));
        before=tracker;
        bool valid=value>=-127&&value<=20;
        assert(cerberus_ble_rssi_valid(value)==valid);
        assert(cerberus_s3_ble_tracker_observe(&tracker,address,2U,(int8_t)value,100U)==valid);
        if(valid)assert(tracker.seen&&tracker.rssi_dbm==value&&tracker.reports==2U&&tracker.last_seen_ms==100U);
        else assert(!memcmp(&tracker,&before,sizeof(tracker))); /* Invalid cannot refresh age/count. */
    }
}

int main(void)
{
    check_rssi_boundaries();
    const uint8_t target[6] = {0x12U, 0x34U, 0x56U, 0x78U, 0x9AU, 0xBCU};
    uint8_t other[6];
    memcpy(other, target, sizeof(other));
    other[0] ^= 1U;
    cerberus_s3_ble_tracker_t tracker = {0};
    cerberus_s3_ble_track_status_t status = {0};
    cerberus_s3_ble_tracker_begin(&tracker, target, 1U);
    cerberus_s3_ble_tracker_snapshot(&tracker, 100U, &status);
    assert(status.enabled && !status.seen && !status.fresh);
    assert(!cerberus_s3_ble_tracker_observe(&tracker, other, 1U, -50, 110U));
    assert(!cerberus_s3_ble_tracker_observe(&tracker, target, 0U, -50, 110U));
    assert(!cerberus_s3_ble_tracker_observe(&tracker, target, 1U, 127, 110U));
    assert(!tracker.seen && tracker.reports == 0U);

    assert(cerberus_s3_ble_tracker_observe(&tracker, target, 1U, -40, 120U));
    assert(cerberus_s3_ble_tracker_observe(&tracker, target, 1U, -75, 180U));
    cerberus_s3_ble_tracker_snapshot(&tracker, 200U, &status);
    assert(status.seen && status.fresh && status.rssi_dbm == -75);
    assert(status.age_ms == 20U && status.reports == 2U);
    cerberus_s3_ble_tracker_snapshot(&tracker, 5180U, &status);
    assert(status.fresh && status.age_ms == CERBERUS_S3_BLE_TRACK_STALE_MS);
    cerberus_s3_ble_tracker_snapshot(&tracker, 5181U, &status);
    assert(!status.fresh && status.seen && status.rssi_dbm == -75);

    cerberus_s3_ble_tracker_begin(&tracker, target, 3U);
    assert(!tracker.seen && tracker.reports == 0U);
    assert(cerberus_s3_ble_tracker_observe(&tracker, target, 3U, -63,
        UINT32_MAX - 100U));
    cerberus_s3_ble_tracker_snapshot(&tracker, 150U, &status);
    assert(status.fresh && status.age_ms == 251U);
    cerberus_s3_ble_tracker_host_reset(&tracker);
    cerberus_s3_ble_tracker_snapshot(&tracker, 151U, &status);
    assert(status.enabled && !status.seen && !status.fresh && status.reports == 0U);
    cerberus_s3_ble_tracker_end(&tracker);
    cerberus_s3_ble_tracker_snapshot(&tracker, 152U, &status);
    assert(!status.enabled && !status.seen && !status.fresh);
    return 0;
}
