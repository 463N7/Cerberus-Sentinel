#ifdef NDEBUG
#undef NDEBUG
#endif
#include "s3_ble_list.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static cerberus_c5_ble_result_t peers[CERBERUS_BLE_SCAN_MAX_RESULTS];
static uint8_t categories[CERBERUS_BLE_SCAN_MAX_RESULTS];

static void initialize(void)
{
    memset(peers, 0, sizeof(peers));
    memset(categories, 0, sizeof(categories));
    const char *names[] = {"zeta", "alpha", "Beta", "ALPHA", "", "\033alpha", "gamma", "A\xff"};
    const int8_t rssis[] = {-60, -30, -30, -30, -60, 127, -128, -30};
    for (size_t i = 0U; i < 8U; ++i) {
        memcpy(peers[i].name, names[i], strlen(names[i]) + 1U);
        peers[i].rssi = rssis[i];
        peers[i].address[0] = (uint8_t)(i + 1U);
        peers[i].address_type = 0U;
    }
    /* Equal visible names and equal address bytes are still distinct typed
     * identities; a presentation sorter must never deduplicate them. */
    memcpy(peers[3].address, peers[1].address, 6U);
    peers[3].address_type = 1U;
    peers[4].address[0] = 1U;
    categories[0] = CERBERUS_S3_BLE_FOCUS_FIND_MY;
    categories[1] = CERBERUS_S3_BLE_FOCUS_IBEACON;
    categories[2] = CERBERUS_S3_BLE_FOCUS_FIND_MY | CERBERUS_S3_BLE_FOCUS_IBEACON;
    categories[3] = CERBERUS_S3_BLE_FOCUS_FIND_MY;
    categories[4] = CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT;
    categories[5] = CERBERUS_S3_BLE_FOCUS_META_HINT;
}

static void check_labels(void)
{
    char label[40];
    assert(cerberus_s3_ble_list_label(&peers[4], label, sizeof(label)) == 17U);
    assert(strcmp(label, "00:00:00:00:00:01") == 0);
    assert(cerberus_s3_ble_list_label(&peers[5], label, sizeof(label)) == 6U);
    assert(strcmp(label, "?alpha") == 0);
    assert(cerberus_s3_ble_list_label(&peers[7], label, sizeof(label)) == 2U);
    assert(strcmp(label, "A?") == 0);
    assert(cerberus_s3_ble_list_label(&peers[5], NULL, 0U) == 6U);
    memset(label, '#', sizeof(label));
    assert(cerberus_s3_ble_list_label(&peers[1], label, 1U) == 5U);
    assert(label[0] == '\0' && label[1] == '#');
    assert(cerberus_s3_ble_list_label(&peers[4], label, 5U) == 17U);
    assert(strcmp(label, "00:0") == 0 && label[5] == '#');
    cerberus_c5_ble_result_t damaged = peers[0];
    memset(damaged.name, 'x', sizeof(damaged.name));
    assert(cerberus_s3_ble_list_label(&damaged, label, sizeof(label)) == 32U);
    assert(strlen(label) == 32U && label[32] == '\0');
    damaged.name[1] = '\0';
    assert(cerberus_s3_ble_list_label(&damaged, label, sizeof(label)) == 1U);
}

static void check_orders_and_pages(void)
{
    cerberus_c5_ble_result_t original[8];
    uint8_t original_categories[8];
    memcpy(original, peers, sizeof(original));
    memcpy(original_categories, categories, sizeof(original_categories));
    const uint8_t signal[] = {7,1,3,2,4,0,5,6};
    const uint8_t name[] = {4,5,7,1,3,2,6,0};
    const uint8_t discovery[] = {0,1,2,3,4,5,6,7};
    const uint8_t *orders[] = {signal,name,discovery};
    for (unsigned mode = 0U; mode < CERBERUS_S3_BLE_SORT_COUNT; ++mode) {
        uint8_t output[12];
        memset(output, 0xee, sizeof(output));
        assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
            CERBERUS_S3_BLE_FILTER_ALL, (cerberus_s3_ble_sort_t)mode,
            0U, output, sizeof(output)) == 8U);
        assert(memcmp(output, orders[mode], 8U) == 0 && output[8] == 0xee);
        for (size_t offset = 0U; offset < 10U; ++offset) {
            memset(output, 0xee, sizeof(output));
            assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
                CERBERUS_S3_BLE_FILTER_ALL, (cerberus_s3_ble_sort_t)mode,
                offset, output, 3U) == 8U);
            size_t remaining = offset < 8U ? 8U - offset : 0U;
            size_t copied = remaining < 3U ? remaining : 3U;
            assert(!copied || memcmp(output, orders[mode] + offset, copied) == 0);
            assert(output[copied] == 0xee);
        }
    }
    uint8_t focused[4] = {255U,255U,255U,255U};
    const uint8_t expected[] = {3,2,0};
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
        CERBERUS_S3_BLE_FILTER_FIND_MY, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        0U, focused, sizeof(focused)) == 3U);
    assert(memcmp(focused, expected, 3U) == 0 && focused[3] == 255U);
    /* Original records—including address types, UUIDs, RSSI and flags—remain
     * byte-for-byte unchanged when presentation order and filters change. */
    assert(memcmp(original, peers, sizeof(original)) == 0);
    assert(memcmp(original_categories, categories, sizeof(original_categories)) == 0);
}

static void check_bounds_and_capacity(void)
{
    uint8_t output[33];
    memset(output, 0xee, sizeof(output));
    assert(cerberus_s3_ble_focus_page_sorted(NULL, NULL, 0U,
        CERBERUS_S3_BLE_FILTER_ALL, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        0U, NULL, 0U) == 0U);
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
        CERBERUS_S3_BLE_FILTER_ALL, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        0U, NULL, 0U) == 8U);
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
        CERBERUS_S3_BLE_FILTER_ALL, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        SIZE_MAX, output, sizeof(output)) == 8U && output[0] == 0xee);
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 33U,
        CERBERUS_S3_BLE_FILTER_ALL, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        0U, output, sizeof(output)) == 0U && output[0] == 0xee);
    assert(cerberus_s3_ble_focus_page_sorted(NULL, categories, 8U,
        CERBERUS_S3_BLE_FILTER_ALL, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        0U, output, sizeof(output)) == 0U);
    assert(cerberus_s3_ble_focus_page_sorted(peers, NULL, 8U,
        CERBERUS_S3_BLE_FILTER_ALL, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        0U, output, sizeof(output)) == 0U);
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
        (cerberus_s3_ble_filter_t)-1, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        0U, output, sizeof(output)) == 0U);
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
        CERBERUS_S3_BLE_FILTER_ALL, (cerberus_s3_ble_sort_t)-1,
        0U, output, sizeof(output)) == 0U);
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
        CERBERUS_S3_BLE_FILTER_ALL, CERBERUS_S3_BLE_SORT_COUNT,
        0U, output, sizeof(output)) == 0U);
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories, 8U,
        CERBERUS_S3_BLE_FILTER_COUNT, CERBERUS_S3_BLE_SORT_SIGNAL_DESC,
        0U, output, sizeof(output)) == 0U);
    for (size_t i = 0U; i < CERBERUS_BLE_SCAN_MAX_RESULTS; ++i) {
        peers[i].rssi = (int8_t)(-100 + i);
        categories[i] = CERBERUS_S3_BLE_FOCUS_IBEACON;
    }
    assert(cerberus_s3_ble_focus_page_sorted(peers, categories,
        CERBERUS_BLE_SCAN_MAX_RESULTS, CERBERUS_S3_BLE_FILTER_IBEACON,
        CERBERUS_S3_BLE_SORT_SIGNAL_DESC, 0U, output, sizeof(output)) == 32U);
    for (size_t i = 0U; i < 32U; ++i) assert(output[i] == 31U - i);
    assert(output[32] == 0xee);
}

static void check_visibility_masks(void)
{
    const uint8_t orders[3][8] = {{7,1,3,2,4,0,5,6}, {4,5,7,1,3,2,6,0}, {0,1,2,3,4,5,6,7}};
    cerberus_c5_ble_result_t original[8]; uint8_t original_categories[8];
    memcpy(original, peers, sizeof(original)); memcpy(original_categories, categories, sizeof(original_categories));
    /* Independent static sort orders are intersected with every 8-row mask
     * and every family; this reference does not call the production comparator. */
    for (unsigned mask=0U; mask<256U; ++mask) {
        for (unsigned filter=0U; filter<CERBERUS_S3_BLE_FILTER_COUNT; ++filter) {
            for (unsigned sort=0U; sort<CERBERUS_S3_BLE_SORT_COUNT; ++sort) {
                uint8_t expected[8]; size_t count=0U;
                for (unsigned position=0U; position<8U; ++position) {
                    uint8_t index=orders[sort][position];
                    if (!(mask & (1U << index))) continue;
                    if (filter && !(categories[index] & (1U << (filter-1U)))) continue;
                    expected[count++]=index;
                }
                for (size_t offset=0U; offset<=9U; ++offset) {
                    uint8_t output[5]; memset(output, 0xee, sizeof(output));
                    assert(cerberus_s3_ble_focus_page_sorted_masked(peers,categories,8U,
                        (cerberus_s3_ble_filter_t)filter,(cerberus_s3_ble_sort_t)sort,
                        (uint32_t)mask,offset,output,3U)==count);
                    size_t copied=offset<count ? count-offset : 0U; if(copied>3U)copied=3U;
                    assert(!copied || !memcmp(output,expected+offset,copied));
                    for(size_t i=copied;i<sizeof(output);++i)assert(output[i]==0xee);
                }
                assert(cerberus_s3_ble_focus_page_sorted_masked(peers,categories,8U,
                    (cerberus_s3_ble_filter_t)filter,(cerberus_s3_ble_sort_t)sort,
                    (uint32_t)mask,0U,NULL,0U)==count);
            }
        }
    }
    assert(!memcmp(original,peers,sizeof(original)) && !memcmp(original_categories,categories,sizeof(original_categories)));
    uint8_t output[4]={0xee,0xee,0xee,0xee};
    assert(cerberus_s3_ble_focus_page_sorted_masked(peers,categories,33U,CERBERUS_S3_BLE_FILTER_ALL,
        CERBERUS_S3_BLE_SORT_DISCOVERY,0U,0U,output,sizeof(output))==0U && output[0]==0xee);
    assert(cerberus_s3_ble_focus_page_sorted_masked(peers,categories,32U,CERBERUS_S3_BLE_FILTER_ALL,
        CERBERUS_S3_BLE_SORT_DISCOVERY,UINT32_C(0x80000001),0U,output,sizeof(output))==2U);
    assert(output[0]==0U && output[1]==31U && output[2]==0xee);
    memset(output,0xee,sizeof(output));
    assert(cerberus_s3_ble_focus_page_sorted_masked(peers,categories,8U,CERBERUS_S3_BLE_FILTER_ALL,
        CERBERUS_S3_BLE_SORT_DISCOVERY,UINT32_C(0x80000000),0U,output,sizeof(output))==0U && output[0]==0xee);
    /* Expiration reduces total matches without silently reinterpreting an old
     * page offset. Caller clamps/resets its page and publishes the new mapping. */
    assert(cerberus_s3_ble_focus_page_sorted_masked(peers,categories,8U,CERBERUS_S3_BLE_FILTER_FIND_MY,
        CERBERUS_S3_BLE_SORT_SIGNAL_DESC,UINT32_C(0xff),2U,output,1U)==3U && output[0]==0U);
    memset(output,0xee,sizeof(output));
    assert(cerberus_s3_ble_focus_page_sorted_masked(peers,categories,8U,CERBERUS_S3_BLE_FILTER_FIND_MY,
        CERBERUS_S3_BLE_SORT_SIGNAL_DESC,UINT32_C(0xf7),2U,output,1U)==2U && output[0]==0xee);
    assert(cerberus_s3_ble_focus_page_sorted_masked(peers,categories,8U,CERBERUS_S3_BLE_FILTER_FIND_MY,
        CERBERUS_S3_BLE_SORT_SIGNAL_DESC,UINT32_C(0xf7),0U,output,1U)==2U && output[0]==2U);
}

static void check_rssi_boundaries(void)
{
    assert(CERBERUS_BLE_RSSI_UNAVAILABLE==127);
    assert(!cerberus_ble_rssi_valid(INT_MIN)&&!cerberus_ble_rssi_valid(INT_MAX));
    assert(!cerberus_ble_rssi_valid(-129)&&!cerberus_ble_rssi_valid(276));
    for(int value=-128;value<=127;++value)
        assert(cerberus_ble_rssi_valid(value)==(value>=-127&&value<=20));
    cerberus_c5_ble_result_t rows[10]={0},before[10];uint8_t bits[10]={0},indexes[12];
    const int8_t values[]={127,-128,21,20,-127,-40,126,20,-127,-40};
    const uint8_t expected[]={3,7,5,9,4,8,0,1,2,6};
    for(size_t i=0;i<10U;++i){rows[i].rssi=values[i];memcpy(rows[i].name,"Equal",6U);rows[i].address[0]=(uint8_t)i;}
    memcpy(before,rows,sizeof(rows));memset(indexes,0xee,sizeof(indexes));
    assert(cerberus_s3_ble_focus_page_sorted(rows,bits,10U,CERBERUS_S3_BLE_FILTER_ALL,
        CERBERUS_S3_BLE_SORT_SIGNAL_DESC,0U,indexes,sizeof(indexes))==10U);
    assert(!memcmp(indexes,expected,sizeof(expected))&&indexes[10]==0xee);
    assert(!memcmp(before,rows,sizeof(rows))); /* Sorting cannot repair or rewrite measurements. */
    memset(indexes,0xee,sizeof(indexes));
    assert(cerberus_s3_ble_focus_page_sorted(rows,bits,10U,CERBERUS_S3_BLE_FILTER_ALL,
        CERBERUS_S3_BLE_SORT_SIGNAL_DESC,4U,indexes,3U)==10U);
    assert(indexes[0]==4U&&indexes[1]==8U&&indexes[2]==0U&&indexes[3]==0xee);
}

int main(void)
{
    initialize();
    check_labels();
    check_orders_and_pages();
    check_visibility_masks();
    check_bounds_and_capacity();
    check_rssi_boundaries();
    puts("BLE stable index sort, all 256 masks/families/sorts/pages, bounded labels, exact RSSI and immutable records: pass");
    return 0;
}
