#include "mac_monitor_inventory.h"
#include <limits.h>
#include <string.h>

static uint32_t add(uint32_t a,uint32_t b){return UINT32_MAX-a<b?UINT32_MAX:a+b;}
static bool key(const cerberus_mac_row_t *a,const cerberus_mac_row_t *b)
{return a->radio==b->radio && a->address_type==b->address_type && !memcmp(a->address,b->address,6);}
bool cerberus_mac_monitor_source_equal(const cerberus_mac_monitor_record_t *a,const cerberus_mac_monitor_record_t *b)
{return a && b && a->valid && b->valid && key(&a->row,&b->row);}
void cerberus_mac_monitor_inventory_init(cerberus_mac_monitor_inventory_t *i)
{if(i)memset(i,0,sizeof(*i));}
static int find(const cerberus_mac_monitor_bank_t *b,const cerberus_mac_row_t *r)
{for(unsigned n=0;n<CERBERUS_MAC_CAPACITY;++n)if(b->records[n].valid && key(&b->records[n].row,r))return (int)n;return -1;}

static void selection_refresh(cerberus_mac_monitor_inventory_t *i,uint8_t radio)
{
    cerberus_mac_monitor_selection_t *s=&i->selected;
    if(!s->valid || s->frozen.row.radio!=radio)return;
    cerberus_mac_monitor_bank_t *b=&i->banks[radio];
    int index=find(b,&s->frozen.row);
    s->unavailable=b->unavailable;s->present=!b->unavailable && index>=0 && b->records[index].present;
    if(s->present)s->latest=b->records[index];
}
void cerberus_mac_monitor_inventory_unavailable(cerberus_mac_monitor_inventory_t *i,uint8_t radio,bool unavailable)
{if(i && radio<2){i->banks[radio].unavailable=unavailable;selection_refresh(i,radio);}}

unsigned cerberus_mac_monitor_inventory_publish(cerberus_mac_monitor_inventory_t *i,
    uint8_t radio,uint64_t epoch,const cerberus_mac_snapshot_t *s,
    uint64_t request,uint64_t received,bool timed)
{
    if(!i || radio>1 || !epoch || !s || s->header.radio!=radio ||
        !cerberus_mac_snapshot_valid(s) || (timed?request>received:request!=0))return CERBERUS_MAC_INVALID;
    cerberus_mac_monitor_bank_t *b=&i->banks[radio];
    bool same=b->published && b->epoch==epoch;
    if(same && s->header.snapshot_id<b->header.snapshot_id)return CERBERUS_MAC_STALE;
    if(same && s->header.snapshot_id==b->header.snapshot_id){
        if(memcmp(&s->header,&b->header,sizeof(b->header)))return CERBERUS_MAC_INVALID;
        for(unsigned n=0;n<s->header.count;++n){
            int index=find(b,&s->rows[n]);
            if(index<0 || !b->records[index].present || b->records[index].producer_index!=n ||
                memcmp(&b->records[index].row,&s->rows[n],sizeof(s->rows[n])))return CERBERUS_MAC_INVALID;
        }
        return CERBERUS_MAC_DUPLICATE;
    }
    if(same && (s->header.count<b->header.count || s->header.source_now_us<b->header.source_now_us ||
        s->header.observations<b->header.observations || s->header.invalid<b->header.invalid ||
        s->header.replacements<b->header.replacements))return CERBERUS_MAC_INVALID;
    uint32_t largest=0;
    if(same)for(unsigned n=0;n<CERBERUS_MAC_CAPACITY;++n)
        if(b->records[n].valid && b->records[n].epoch==epoch && b->records[n].row.incarnation>largest)
            largest=b->records[n].row.incarnation;
    /* Validate every cumulative transition before modifying any record. */
    for(unsigned n=0;same && n<s->header.count;++n){
        const cerberus_mac_row_t *r=&s->rows[n];int index=find(b,r);
        const cerberus_mac_monitor_record_t *old=index<0?NULL:&b->records[index];
        if(old && old->epoch==epoch && old->row.incarnation==r->incarnation){
            if(!old->present || r->observations<old->row.observations || r->responses<old->row.responses ||
                r->first_us!=old->row.first_us || r->last_us<old->row.last_us ||
                (r->type_mask&old->row.type_mask)!=old->row.type_mask)return CERBERUS_MAC_INVALID;
            if(r->observations==old->row.observations && r->observations!=UINT32_MAX &&
                memcmp(r,&old->row,sizeof(*r)))return CERBERUS_MAC_INVALID;
        }else if(r->incarnation<=largest)return CERBERUS_MAC_INVALID;
    }
    bool needed[CERBERUS_MAC_CAPACITY]={0};
    for(unsigned n=0;n<s->header.count;++n){int index=find(b,&s->rows[n]);if(index>=0)needed[index]=true;}
    for(unsigned n=0;n<CERBERUS_MAC_CAPACITY;++n)b->records[n].present=false;
    for(unsigned n=0;n<s->header.count;++n){
        const cerberus_mac_row_t *r=&s->rows[n];int index=find(b,r);
        if(index<0){
            for(unsigned q=0;q<CERBERUS_MAC_CAPACITY;++q)if(!b->records[q].valid){index=(int)q;break;}
            if(index<0)for(unsigned q=0;q<CERBERUS_MAC_CAPACITY;++q)if(!needed[q]){index=(int)q;break;}
            /* Capacity is proven by the unique fully validated input bank. */
            memset(&b->records[index],0,sizeof(b->records[index]));
        }
        needed[index]=true;cerberus_mac_monitor_record_t *out=&b->records[index];
        bool continuing=out->valid && out->epoch==epoch && out->row.incarnation==r->incarnation;
        uint32_t observations=continuing?r->observations-out->row.observations:r->observations;
        uint32_t responses=continuing?r->responses-out->row.responses:r->responses;
        out->observations=add(out->observations,observations);out->responses=add(out->responses,responses);
        out->row=*r;out->epoch=epoch;out->snapshot_id=s->header.snapshot_id;
        out->source_now_us=s->header.source_now_us;out->request_started_us=timed?request:0;
        out->timing_valid=timed;out->valid=out->present=true;out->producer_index=(uint8_t)n;
    }
    b->header=s->header;b->epoch=epoch;b->published=true;b->unavailable=false;
    b->publications=add(b->publications,1);selection_refresh(i,radio);
    return CERBERUS_MAC_PUBLISHED;
}

bool cerberus_mac_monitor_age(const cerberus_mac_monitor_record_t *r,uint64_t now,uint64_t *age)
{
    if(!age)return false;
    *age=0;
    if(!r || !r->valid || !r->timing_valid || now<r->request_started_us || r->source_now_us<r->row.last_us)return false;
    uint64_t source=r->source_now_us-r->row.last_us,elapsed=now-r->request_started_us;
    if(UINT64_MAX-source<elapsed)return false;
    uint64_t total=source+elapsed;*age=total/1000U+(total%1000U!=0);return true;
}
static bool better(const cerberus_mac_monitor_record_t *a,const cerberus_mac_monitor_record_t *b,uint8_t sort,uint64_t now)
{
    if(sort==CERBERUS_MAC_SORT_COUNT && a->observations!=b->observations)return a->observations>b->observations;
    if(sort==CERBERUS_MAC_SORT_SIGNAL){
        bool av=(a->row.flags&CERBERUS_MAC_RSSI_VALID)!=0,bv=(b->row.flags&CERBERUS_MAC_RSSI_VALID)!=0;
        if(av!=bv)return av;
        if(av && a->row.rssi!=b->row.rssi)return a->row.rssi>b->row.rssi;
    }
    uint64_t aa,ba;bool av=cerberus_mac_monitor_age(a,now,&aa),bv=cerberus_mac_monitor_age(b,now,&ba);
    if(av!=bv)return av;
    if(av && aa!=ba)return aa<ba;
    if(a->observations!=b->observations)return a->observations>b->observations;
    if(a->row.radio!=b->row.radio)return a->row.radio<b->row.radio;
    if(a->row.address_type!=b->row.address_type)return a->row.address_type<b->row.address_type;
    return memcmp(a->row.address,b->row.address,6)<0;
}
bool cerberus_mac_monitor_inventory_page(const cerberus_mac_monitor_inventory_t *i,uint8_t sort,
    size_t offset,uint64_t now,cerberus_mac_monitor_page_t *page)
{
    if(!page)return false;
    memset(page,0,sizeof(*page));
    if(!i || sort>CERBERUS_MAC_SORT_RECENT)return false;
    uint8_t indexes[2*CERBERUS_MAC_CAPACITY],count=0;
    for(unsigned n=0;n<2*CERBERUS_MAC_CAPACITY;++n){
        const cerberus_mac_monitor_record_t *r=&i->banks[n/CERBERUS_MAC_CAPACITY].records[n%CERBERUS_MAC_CAPACITY];
        if(!r->valid)continue;
        unsigned pos=count++;
        while(pos){unsigned old=indexes[pos-1];const cerberus_mac_monitor_record_t *previous=&i->banks[old/CERBERUS_MAC_CAPACITY].records[old%CERBERUS_MAC_CAPACITY];
            if(!better(r,previous,sort,now))break;
            indexes[pos]=indexes[pos-1];--pos;}
        indexes[pos]=(uint8_t)n;
    }
    page->total=count;page->sort=sort;
    page->offset=count && offset>=count?((count-1U)/CERBERUS_MAC_MONITOR_PAGE_ROWS)*CERBERUS_MAC_MONITOR_PAGE_ROWS:count?offset:0;
    for(size_t n=page->offset;n<count && page->copied<CERBERUS_MAC_MONITOR_PAGE_ROWS;++n){unsigned index=indexes[n];
        page->rows[page->copied++]=i->banks[index/CERBERUS_MAC_CAPACITY].records[index%CERBERUS_MAC_CAPACITY];}
    return true;
}
bool cerberus_mac_monitor_inventory_select(cerberus_mac_monitor_inventory_t *i,const cerberus_mac_monitor_record_t *p)
{
    if(!i || !p || !p->valid || p->row.radio>1 || !p->epoch || !p->snapshot_id ||
        !cerberus_mac_row_valid(&p->row,p->row.radio,p->source_now_us))return false;
    cerberus_mac_monitor_record_t copy=*p;
    cerberus_mac_monitor_bank_t *b=&i->banks[p->row.radio];int index=find(b,&p->row);
    if(b->epoch==p->epoch && b->header.snapshot_id==p->snapshot_id &&
        (index<0 || memcmp(&b->records[index],p,sizeof(*p))))return false;
    i->selected=(cerberus_mac_monitor_selection_t){.frozen=copy,.latest=copy,.valid=true};
    selection_refresh(i,copy.row.radio);return true;
}
void cerberus_mac_monitor_inventory_clear_selection(cerberus_mac_monitor_inventory_t *i)
{if(i)memset(&i->selected,0,sizeof(i->selected));}
