#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cerberus_protocol.h"
int main(void)
{
    assert(sizeof(cerberus_wifi_ap_station_begin_t)==21);
    assert(sizeof(cerberus_wifi_ap_station_chunk_t)==22);
    cerberus_wifi_ap_job_t job={.job_id=17,.generation=4,.index=0,.bssid={2,0,0,0,0,1},
        .channel=36,.seconds=10,.scope=CERBERUS_AP_STATION_LIST,.mode=CERBERUS_AP_BAD_MESSAGE};
    cerberus_wifi_ap_station_set_t set={.count=32};
    for(unsigned i=0;i<32;++i){set.mac[i][0]=2;set.mac[i][4]=3;set.mac[i][5]=(uint8_t)(i+1);}
    assert(cerberus_wifi_ap_job_valid(&job)&&cerberus_wifi_ap_station_set_valid(&job,&set));
    job.seconds=0;assert(cerberus_wifi_ap_job_valid(&job)&&cerberus_wifi_ap_station_set_valid(&job,&set));
    job.seconds=30;assert(!cerberus_wifi_ap_job_valid(&job));job.seconds=10;
    uint8_t frame[160]={0},nonce[32]={1};
    assert(!cerberus_wifi_ap_bad_message(&job,false,1,nonce,frame,sizeof(frame)));
    job.mode=CERBERUS_AP_ASSOC_SLEEP;
    assert(cerberus_wifi_ap_job_valid(&job)&&cerberus_wifi_ap_station_set_valid(&job,&set));
    assert(!cerberus_wifi_ap_association(&job,(const uint8_t*)"AP",2,frame,sizeof(frame)));
    job.mode=CERBERUS_AP_DEAUTH;
    assert(cerberus_wifi_ap_job_valid(&job)&&cerberus_wifi_ap_station_set_valid(&job,&set));
    assert(!cerberus_wifi_ap_frame(&job,false,frame,sizeof(frame)));
    set.count=1;assert(!cerberus_wifi_ap_station_set_valid(&job,&set));
    set.count=0;assert(!cerberus_wifi_ap_station_set_valid(&job,&set));
    set.count=32;memcpy(set.mac[1],set.mac[0],6);assert(!cerberus_wifi_ap_station_set_valid(&job,&set));
    set.mac[1][5]=2;set.mac[1][0]=1;assert(!cerberus_wifi_ap_station_set_valid(&job,&set));
    set.mac[1][0]=2;memcpy(set.mac[1],job.bssid,6);assert(!cerberus_wifi_ap_station_set_valid(&job,&set));
    set.mac[1][5]=2;set.mac[1][4]=3;assert(cerberus_wifi_ap_station_set_valid(&job,&set));
    job.station[0]=2;assert(!cerberus_wifi_ap_job_valid(&job));job.station[0]=0;
    job.mode=CERBERUS_AP_PROBE_REQUEST;assert(!cerberus_wifi_ap_job_valid(&job));
    job.mode=CERBERUS_AP_QUIET;assert(!cerberus_wifi_ap_job_valid(&job));
    job.mode=CERBERUS_AP_DEAUTH;job.scope=CERBERUS_AP_ONE_STATION;
    memcpy(job.station,set.mac[0],6);assert(cerberus_wifi_ap_job_valid(&job)&&!cerberus_wifi_ap_station_set_valid(&job,&set));
    puts("AP station-list contract passed: 32 distinct clients, invalid sets, precise modes and single-client frame builders.");
}
