#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "network_worker_platform.h"
#include "network_scanner.c"

enum {GOOD,UNRELATED,TIMEOUT,CANCEL,LINK_LOSS,LEASE,SOCKET_FAIL,BIND_FAIL,SEND_FAIL,RECV_FAIL,TCP_REFUSED,TCP_TIMEOUT};
static unsigned behavior,live_sockets,opened,closed,received,sent,locked,created,deleted,mutexes;
static uint32_t clock_ms,epoch=7;
static bool task_failure,in_core,arp_cached,arp_timeout,core_failure,arp_failure;
static unsigned arp_queries,arp_lookups;
static uint8_t echo_request[CERBERUS_NET_ICMP_BYTES],last_target[4];
static void (*task_fn)(void*);
static cerberus_wifi_connection_status_payload_t link;
static struct netif interface;
static esp_netif_t station={.index=1};
static struct eth_addr mac={{2,0x12,0x34,0xab,0xcd,0xef}};
static ip4_addr_t resolved;

SemaphoreHandle_t xSemaphoreCreateMutex(void){++mutexes;return (void*)1;}
int xSemaphoreTake(SemaphoreHandle_t h,unsigned wait){(void)wait;assert(h&&!locked);++locked;return pdTRUE;}
int xSemaphoreGive(SemaphoreHandle_t h){assert(h&&locked==1);--locked;return pdTRUE;}
int xTaskCreate(void(*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out)
{assert(!strcmp(name,"network_scan")&&stack==5120&&!arg&&priority==3);if(task_failure)return 0;++created;task_fn=fn;*out=(void*)2;return pdPASS;}
void vTaskDelete(void *task){assert(!task);++deleted;}
void vTaskDelay(unsigned ms){clock_ms+=ms;}
int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
uint32_t esp_random(void){return 0x12345678;}
uint32_t cerberus_wifi_connection_snapshot(cerberus_wifi_connection_status_payload_t *s){*s=link;return epoch;}

int socket(int family,int type,int protocol)
{assert(family==AF_INET&&((type==SOCK_RAW&&protocol==IPPROTO_ICMP)||(type==SOCK_STREAM&&protocol==IPPROTO_TCP)));if(behavior==SOCKET_FAIL)return -1;assert(!live_sockets);++live_sockets;++opened;return 3;}
int close(int fd){assert(fd==3&&live_sockets==1);--live_sockets;++closed;return 0;}
int fcntl(int fd,int op,int flags){assert(fd==3&&op==F_SETFL&&flags==O_NONBLOCK);return 0;}
int bind(int fd,const struct sockaddr *address,socklen_t size)
{assert(fd==3&&size==sizeof(struct sockaddr_in));const struct sockaddr_in *a=(const void*)address;assert(!memcmp(&a->sin_addr.s_addr,link.ipv4,4));return behavior==BIND_FAIL?-1:0;}
int connect(int fd,const struct sockaddr *address,socklen_t size)
{assert(fd==3&&address&&size==sizeof(struct sockaddr_in));if(behavior==TCP_REFUSED){errno=ECONNREFUSED;return -1;}if(behavior==TCP_TIMEOUT){errno=EINPROGRESS;return -1;}return 0;}
int select(int n,fd_set *reading,fd_set *writing,fd_set *except,struct timeval *timeout)
{
    assert(n==4&&(reading||writing)&&!except&&timeout->tv_sec==0&&timeout->tv_usec<=100000);
    if(behavior==CANCEL){stop_requested=true;clock_ms+=10;return 0;}
    if(behavior==LINK_LOSS){++epoch;clock_ms+=10;return 0;}
    if(behavior==LEASE){clock_ms+=5001;return 0;}
    if(behavior==TIMEOUT||behavior==TCP_TIMEOUT){clock_ms+=(uint32_t)timeout->tv_usec/1000;return 0;}
    clock_ms+=10;return 1;
}
int recv(int fd,void *buffer,size_t length,int flags)
{assert(fd==3&&buffer&&length&&!flags);return 0;}
int getsockopt(int fd,int level,int opt,void *value,socklen_t *length)
{assert(fd==3&&level==SOL_SOCKET&&opt==SO_ERROR&&*length==sizeof(int));*(int*)value=0;return 0;}
int sendto(int fd,const void *buffer,size_t length,int flags,const struct sockaddr *target,socklen_t size)
{assert(fd==3&&length==sizeof(echo_request)&&!flags&&size==sizeof(struct sockaddr_in));++sent;
 memcpy(echo_request,buffer,length);const struct sockaddr_in *a=(const void*)target;memcpy(last_target,&a->sin_addr.s_addr,4);
 if(behavior==SEND_FAIL){errno=ENOMEM;return -1;}return (int)length;}
static void fix_sum(uint8_t *p,size_t n,unsigned offset)
{
    p[offset]=p[offset+1]=0;uint32_t sum=0;
    for(size_t i=0;i<n;i+=2)sum+=(uint32_t)p[i]*256+p[i+1];
    while(sum>>16)sum=(sum&65535)+(sum>>16);
    p[offset]=(uint8_t)(~sum>>8);p[offset+1]=(uint8_t)~sum;
}
int recvfrom(int fd,void *buffer,size_t cap,int flags,struct sockaddr *from,socklen_t *length)
{
    assert(fd==3&&cap>=36&&!flags&&*length==sizeof(struct sockaddr_in));++received;
    if(behavior==RECV_FAIL){errno=EIO;return -1;}
    uint8_t *p=buffer;memset(p,0,36);p[0]=0x45;p[3]=36;p[8]=64;p[9]=1;
    memcpy(p+12,last_target,4);memcpy(p+16,link.ipv4,4);memcpy(p+20,echo_request,sizeof(echo_request));p[20]=0;
    if(behavior==UNRELATED&&received==1)p[27]^=1; /* Valid but stale sequence. */
    fix_sum(p,20,10);fix_sum(p+20,16,2);
    struct sockaddr_in *a=(void*)from;a->sin_family=AF_INET;memcpy(&a->sin_addr.s_addr,last_target,4);
    return 36;
}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key){assert(!strcmp(key,"WIFI_STA_DEF")&&!in_core);return &station;}
int esp_netif_get_netif_impl_index(esp_netif_t *n){assert(n==&station&&!in_core);return n->index;}
struct netif *netif_get_by_index(uint8_t index){assert(in_core);return index==1?&interface:NULL;}
int etharp_find_addr(struct netif *n,const ip4_addr_t *ip,struct eth_addr **out_mac,const ip4_addr_t **out_ip)
{assert(in_core&&n==&interface);++arp_lookups;if(!arp_cached&&(arp_timeout||arp_lookups<3))return -1;resolved=*ip;*out_ip=&resolved;*out_mac=&mac;return 0;}
err_t etharp_request(struct netif *n,const ip4_addr_t *ip){assert(in_core&&n==&interface&&ip);++arp_queries;return arp_failure?ERR_MEM:ERR_OK;}
err_t tcpip_callback_wait(void(*fn)(void*),void *argument)
{assert(!in_core);if(core_failure)return ERR_MEM;in_core=true;fn(argument);in_core=false;if(behavior==CANCEL)stop_requested=true;if(behavior==LINK_LOSS)++epoch;return ERR_OK;}

static void reset(unsigned mode)
{
    assert(!live_sockets&&!locked&&!scan_task);behavior=mode;clock_ms=100;epoch=7;stop_requested=false;
    received=sent=arp_queries=arp_lookups=0;arp_cached=arp_timeout=core_failure=arp_failure=task_failure=false;
    link=(cerberus_wifi_connection_status_payload_t){.state=CERBERUS_WIFI_CONNECTION_CONNECTED,.generation=2,.index=3,.ipv4={192,168,1,1},.netmask={255,255,255,252}};
    interface.up=interface.link=true;memcpy(&interface.ip.addr,link.ipv4,4);station.index=1;contact_ms=clock_ms;
}
static cerberus_net_scan_result_t item(void)
{return (cerberus_net_scan_result_t){.job_id=18,.ipv4={192,168,1,2}};}
static void ping_checks(void)
{
    for(unsigned mode=GOOD;mode<=RECV_FAIL;++mode){
        reset(mode);cerberus_net_scan_result_t r=item();esp_err_t err=ESP_OK;
        int outcome=probe_ping(&r,link.ipv4,23,0x12345678,500,7,&err);
        assert(!live_sockets&&opened==closed);
        if(mode==GOOD||mode==UNRELATED){assert(outcome==1&&err==ESP_OK&&r.flags==CERBERUS_NET_SCAN_ICMP_REPLY);assert(received==(mode==GOOD?1U:2U));}
        else if(mode==TIMEOUT){assert(outcome==2&&err==ESP_OK&&clock_ms==600);}
        else if(mode==LEASE){assert(outcome==2||outcome==-1);/* Worker rechecks abort after a deadline. */assert(abort_reason(7)==ESP_ERR_NOT_FINISHED);}
        else assert(outcome==-1&&err!=ESP_OK);
    }
    reset(TIMEOUT);clock_ms=UINT32_MAX-200;contact_ms=clock_ms;cerberus_net_scan_result_t r=item();esp_err_t err=0;
    assert(probe_ping(&r,link.ipv4,23,1,500,7,&err)==2&&clock_ms==299);
}
static void arp_checks(void)
{
    reset(GOOD);cerberus_net_scan_result_t r=item();esp_err_t err=0;
    assert(probe_arp(&r,link.ipv4,500,7,&err)==1&&err==0&&r.flags==CERBERUS_NET_SCAN_ARP_RESOLVED);
    assert(arp_queries==1&&arp_lookups==3&&!memcmp(r.banner,mac.addr,6)&&r.connect_ms==40);
    reset(GOOD);arp_cached=true;r=item();assert(probe_arp(&r,link.ipv4,500,7,&err)==1);
    assert(arp_queries==1&&(r.flags&CERBERUS_NET_SCAN_ARP_CACHED)&&r.connect_ms==0);
    reset(GOOD);arp_timeout=true;assert(probe_arp(&r,link.ipv4,500,7,&err)==2&&clock_ms==600&&arp_queries==1);
    reset(GOOD);core_failure=true;assert(probe_arp(&r,link.ipv4,500,7,&err)==-1&&err==ESP_ERR_NO_MEM);
    reset(GOOD);arp_failure=true;assert(probe_arp(&r,link.ipv4,500,7,&err)==-1&&err==ESP_ERR_NO_MEM);
    reset(GOOD);interface.link=false;assert(probe_arp(&r,link.ipv4,500,7,&err)==-1&&err==ESP_ERR_INVALID_STATE);
    reset(GOOD);interface.ip.addr^=1;assert(probe_arp(&r,link.ipv4,500,7,&err)==-1&&err==ESP_ERR_INVALID_STATE);
    reset(CANCEL);assert(probe_arp(&r,link.ipv4,500,7,&err)==-1&&err==ESP_ERR_NOT_FINISHED);
    reset(LINK_LOSS);assert(probe_arp(&r,link.ipv4,500,7,&err)==-1&&err==ESP_ERR_INVALID_STATE);
    reset(GOOD);clock_ms=UINT32_MAX-200;contact_ms=clock_ms;arp_timeout=true;
    assert(probe_arp(&r,link.ipv4,500,7,&err)==2&&clock_ms==299);
}
static void worker_checks(void)
{
    cerberus_net_scan_request_t request={.job_id=1,.mode=CERBERUS_NET_SCAN_PING_LAN,.timeout_ms=500,.connection_generation=2,.connection_index=3};
    const unsigned scenarios[]={GOOD,UNRELATED,TIMEOUT,CANCEL,LINK_LOSS,LEASE,SOCKET_FAIL};
    for(unsigned mode=CERBERUS_NET_SCAN_PING_LAN;mode<=CERBERUS_NET_SCAN_ARP_LAN;++mode){
        for(unsigned i=0;i<sizeof(scenarios)/sizeof(scenarios[0]);++i){
            reset(scenarios[i]);if(mode==CERBERUS_NET_SCAN_ARP_LAN&&(behavior==TIMEOUT||behavior==LEASE))arp_timeout=true;
            request.mode=(uint8_t)mode;++request.job_id;
            assert(cerberus_network_scanner_start(&request)==ESP_OK&&scan_task);
            unsigned before=created;assert(cerberus_network_scanner_start(&request)==ESP_OK&&created==before);
            ++request.job_id;assert(cerberus_network_scanner_start(&request)==ESP_ERR_INVALID_STATE);--request.job_id;
            if(mode==CERBERUS_NET_SCAN_ARP_LAN&&behavior==LEASE)contact_ms=clock_ms-5001;
            task_fn(NULL);assert(!scan_task&&!live_sockets&&!locked&&created==deleted);
            if(behavior==CANCEL||behavior==LEASE)assert(scan_status.state==CERBERUS_NET_SCAN_CANCELLED&&!scan_status.result_count);
            else if(behavior==LINK_LOSS||(behavior==SOCKET_FAIL&&mode==CERBERUS_NET_SCAN_PING_LAN))assert(scan_status.state==CERBERUS_NET_SCAN_ERROR&&!scan_status.result_count);
            else {assert(scan_status.state==CERBERUS_NET_SCAN_COMPLETE&&scan_status.tested==1);
                assert(scan_status.open==(behavior==TIMEOUT?0:1));
                if(scan_status.open)assert(cerberus_net_scan_result_valid(&results[0],request.job_id,0));}
        }
    }
    reset(GOOD);request.job_id=100;task_failure=true;
    assert(cerberus_network_scanner_start(&request)==ESP_ERR_NO_MEM&&!scan_task&&scan_status.state==CERBERUS_NET_SCAN_ERROR);
    task_failure=false;++request.job_id;assert(cerberus_network_scanner_start(&request)==ESP_OK);
    assert(cerberus_network_scanner_stop(request.job_id)==ESP_OK);task_fn(NULL);assert(scan_status.state==CERBERUS_NET_SCAN_CANCELLED);
    reset(TCP_REFUSED);request.job_id=102;request.mode=CERBERUS_NET_SCAN_RANGE;request.first_port=request.last_port=80;memcpy(request.target,(uint8_t[]){192,168,1,2},4);
    assert(cerberus_network_scanner_start(&request)==ESP_OK);task_fn(NULL);assert(scan_status.closed==1&&!scan_status.open&&!scan_status.filtered);
    reset(TCP_TIMEOUT);++request.job_id;assert(cerberus_network_scanner_start(&request)==ESP_OK);task_fn(NULL);assert(scan_status.filtered==1&&!scan_status.closed);
}
int main(void)
{
    assert(cerberus_network_scanner_init()==ESP_OK);assert(cerberus_network_scanner_init()==ESP_OK&&mutexes==1);
    ping_checks();arp_checks();worker_checks();assert(opened==closed&&created==deleted);
    puts("C5 network worker passed: ICMP correlation, ARP core context/cache, TCP regression, cancellation, lease, link loss, wraparound and resource cleanup.");
}
