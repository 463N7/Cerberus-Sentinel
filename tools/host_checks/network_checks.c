#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cerberus_protocol.h"

static void put16(uint8_t *p, uint16_t n) { p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n; }
static uint16_t sum16(const uint8_t *p,size_t n)
{
    uint32_t sum=0;
    for(size_t i=0;i<n;i+=2)sum+=(uint32_t)p[i]*256U+(i+1<n?p[i+1]:0);
    while(sum>>16)sum=(sum&65535U)+(sum>>16);
    return (uint16_t)~sum;
}
static void fix_checksum(uint8_t *p,size_t n,unsigned offset)
{p[offset]=p[offset+1]=0;put16(p+offset,sum16(p,n));}
static void check_echo(void)
{
    const uint8_t source[]={192,168,1,1},own[]={192,168,1,42};
    uint8_t bytes[77]={0},*p=bytes+1; /* Deliberately unaligned. */
    const uint16_t id=0x1234,sequence=0xabcd;const uint32_t nonce=0x98765432;
    for(unsigned header=20;header<=60;header+=4){
        memset(p,0,76);p[0]=(uint8_t)(0x40+header/4);p[8]=64;p[9]=1;
        put16(p+2,(uint16_t)(header+CERBERUS_NET_ICMP_BYTES));
        memcpy(p+12,source,4);memcpy(p+16,own,4);fix_checksum(p,header,10);
        cerberus_net_icmp_request(p+header,id,sequence,nonce);
        assert(sum16(p+header,CERBERUS_NET_ICMP_BYTES)==0&&p[header]==8);
        assert(!cerberus_net_icmp_reply(p,76,source,own,id,sequence,nonce));
        p[header]=0;fix_checksum(p+header,CERBERUS_NET_ICMP_BYTES,2);
        assert(cerberus_net_icmp_reply(p,76,source,own,id,sequence,nonce));
        for(unsigned n=0;n<header+CERBERUS_NET_ICMP_BYTES;++n)
            assert(!cerberus_net_icmp_reply(p,n,source,own,id,sequence,nonce));
        assert(!cerberus_net_icmp_reply(p,76,own,source,id,sequence,nonce));
        assert(!cerberus_net_icmp_reply(p,76,source,own,id+1,sequence,nonce));
        assert(!cerberus_net_icmp_reply(p,76,source,own,id,sequence+1,nonce));
        assert(!cerberus_net_icmp_reply(p,76,source,own,id,sequence,nonce+1));
        for(unsigned bit=0;bit<(header+CERBERUS_NET_ICMP_BYTES)*8;++bit){
            p[bit/8]^=1U<<(bit%8);assert(!cerberus_net_icmp_reply(p,76,source,own,id,sequence,nonce));p[bit/8]^=1U<<(bit%8);
        }
        put16(p+6,0x4000);fix_checksum(p,header,10); /* Don't-fragment is legal. */
        assert(cerberus_net_icmp_reply(p,76,source,own,id,sequence,nonce));
        const uint16_t fragments[]={0x2000,0x8000,1,8191};
        for(unsigned i=0;i<4;++i){put16(p+6,fragments[i]);fix_checksum(p,header,10);
            assert(!cerberus_net_icmp_reply(p,76,source,own,id,sequence,nonce));}
    }
    assert(!cerberus_net_icmp_reply(NULL,76,source,own,id,sequence,nonce));
}
static void check_wire_results(void)
{
    cerberus_net_scan_result_t r={.job_id=1,.index=2,.ipv4={192,168,1,2},.port=80};
    assert(cerberus_net_scan_result_valid(&r,1,2));
    assert(!cerberus_net_scan_result_valid(&r,2,2)&&!cerberus_net_scan_result_valid(&r,1,3));
    r.flags=CERBERUS_NET_SCAN_ICMP_REPLY;assert(!cerberus_net_scan_result_valid(&r,1,2));
    r.port=0;assert(cerberus_net_scan_result_valid(&r,1,2));
    r.banner[0]=1;assert(!cerberus_net_scan_result_valid(&r,1,2));r.banner[0]=0;
    r.flags=CERBERUS_NET_SCAN_ARP_RESOLVED;assert(!cerberus_net_scan_result_valid(&r,1,2));
    memcpy(r.banner,"\x02\x12\x34\xab\xcd\xef",6);assert(cerberus_net_scan_result_valid(&r,1,2));
    r.flags|=CERBERUS_NET_SCAN_ARP_CACHED;assert(cerberus_net_scan_result_valid(&r,1,2));
    r.banner[6]=1;assert(!cerberus_net_scan_result_valid(&r,1,2));r.banner[6]=0;
    r.banner[0]=1;assert(!cerberus_net_scan_result_valid(&r,1,2));r.banner[0]=2;
    r.flags|=CERBERUS_NET_SCAN_SSH_BANNER;assert(!cerberus_net_scan_result_valid(&r,1,2));
    r.flags=128;assert(!cerberus_net_scan_result_valid(&r,1,2));
    r.flags=0;assert(!cerberus_net_scan_result_valid(&r,1,2));
}

int main(void)
{
    uint8_t ip[4] = {9,9,9,9};
    assert(cerberus_net_parse_ipv4("192.168.10.42", ip) && ip[3] == 42);
    const char *bad[] = {"", "1", "1.2.3", "1.2.3.4.5", "1.2.3.256", "-1.2.3.4", " 1.2.3.4", "1.2.3.4x", "999999999999.0.0.0"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        uint8_t before[4]; memcpy(before,ip,4);
        assert(!cerberus_net_parse_ipv4(bad[i],ip) && !memcmp(before,ip,4));
    }
    uint16_t a,b;
    assert(cerberus_net_parse_ports("1-65535",&a,&b) && a==1 && b==65535);
    assert(cerberus_net_parse_ports("22",&a,&b) && a==22 && b==22);
    const char *bad_ports[]={"0","65536","80-22","1-","1-2-3","22 ","-2","9999999999999"};
    for(unsigned i=0;i<sizeof(bad_ports)/sizeof(bad_ports[0]);++i) assert(!cerberus_net_parse_ports(bad_ports[i],&a,&b));
    uint8_t own[]={192,168,10,42},mask[]={255,255,255,0};
    cerberus_net_scan_request_t r={.job_id=1,.target={192,168,10,1},.timeout_ms=500};
    cerberus_net_scan_plan_t p;
    assert(cerberus_net_scan_plan(&p,&r,own,mask) && p.total==16);
    assert(cerberus_net_scan_probe(&p,1,ip,&a) && a==22 && ip[3]==1);
    assert(!cerberus_net_scan_probe(&p,16,ip,&a));
    r.mode=CERBERUS_NET_SCAN_RANGE;r.first_port=1;r.last_port=65535;
    assert(cerberus_net_scan_plan(&p,&r,own,mask) && p.total==65535);
    for(uint32_t i=0;i<65535;++i) assert(cerberus_net_scan_probe(&p,(uint16_t)i,ip,&a) && a==i+1);
    r.target[3]=0;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    r.target[3]=255;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    r.target[3]=42;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    r.target[2]=11;r.target[3]=1;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    r.target[2]=10;
    r.mode=CERBERUS_NET_SCAN_SSH_LAN;
    assert(cerberus_net_scan_plan(&p,&r,own,mask) && p.total==253);
    bool seen[256]={0};
    for(unsigned i=0;i<p.total;++i) {
        assert(cerberus_net_scan_probe(&p,(uint16_t)i,ip,&a) && a==22 && !memcmp(ip,own,3));
        assert(ip[3]!=0 && ip[3]!=255 && ip[3]!=42 && !seen[ip[3]]); seen[ip[3]]=true;
    }
    mask[3]=252;own[3]=1;r.mode=CERBERUS_NET_SCAN_TELNET_LAN;
    assert(cerberus_net_scan_plan(&p,&r,own,mask) && p.total==1);
    assert(cerberus_net_scan_probe(&p,0,ip,&a) && ip[3]==2 && a==23);
    mask[3]=254;own[3]=0;
    assert(cerberus_net_scan_plan(&p,&r,own,mask) && p.total==1);
    assert(cerberus_net_scan_probe(&p,0,ip,&a) && ip[3]==1);
    mask[3]=255;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    mask[2]=254;mask[3]=0;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    mask[2]=255;mask[3]=128;own[3]=42;
    assert(cerberus_net_scan_plan(&p,&r,own,mask) && p.total==125);
    mask[2]=1;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    mask[2]=255;mask[3]=0;r.timeout_ms=99;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    r.timeout_ms=2001;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    r.timeout_ms=500;r.job_id=0;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    r.job_id=1;
    const uint16_t services[]={22,23,25,53,80,443,3389,0,0};
    for(unsigned mode=CERBERUS_NET_SCAN_SSH_LAN;mode<CERBERUS_NET_SCAN_MODE_COUNT;++mode){
        r.mode=(uint8_t)mode;
        assert(cerberus_net_scan_service_port(r.mode)==services[mode-CERBERUS_NET_SCAN_SSH_LAN]);
        mask[3]=0;own[3]=42;
        assert(cerberus_net_scan_plan(&p,&r,own,mask)&&p.total==253);
        memset(seen,0,sizeof(seen));
        for(unsigned i=0;i<p.total;++i){
            assert(cerberus_net_scan_probe(&p,(uint16_t)i,ip,&a)&&a==services[mode-CERBERUS_NET_SCAN_SSH_LAN]);
            assert(ip[3]!=0&&ip[3]!=255&&ip[3]!=42&&!seen[ip[3]]);seen[ip[3]]=true;
        }
        mask[3]=254;own[3]=0;assert(cerberus_net_scan_plan(&p,&r,own,mask)&&p.total==1);
        assert(cerberus_net_scan_probe(&p,0,ip,&a)&&ip[3]==1&&a==services[mode-CERBERUS_NET_SCAN_SSH_LAN]);
    }
    assert(!cerberus_net_scan_service_port(CERBERUS_NET_SCAN_COMMON));
    assert(!cerberus_net_scan_service_port(255));
    r.mode=CERBERUS_NET_SCAN_MODE_COUNT;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    r.mode=255;assert(!cerberus_net_scan_plan(&p,&r,own,mask));
    check_echo();
    check_wire_results();
    puts("Network checks passed: all ports, service/ping/ARP subnet bounds, strict echo correlation and malformed packets.");
    return 0;
}
