#ifdef NDEBUG
#undef NDEBUG
#endif
#include "csi_motion_model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

static cerberus_csi_motion_model_t model;
static uint32_t sequence;

/* Independent literal layout from the vendor guide, before the common mask. */
static int tone(unsigned secondary, unsigned slot)
{ return secondary == 0 ? (slot <= 26 ? (int)slot : (int)slot - 53) : secondary == 1 ? (int)slot - 53 : (int)slot; }

static cerberus_csi_sample_t sample(uint64_t time, unsigned secondary,
    float gain, float phase, float slope, float shape, float phase_shape)
{
    cerberus_csi_sample_t s={.source_us=time,.sequence=++sequence,.rssi=-45,.channel=6,
        .secondary=(uint8_t)secondary,.phy=1,.flags=CERBERUS_CSI_SAMPLE_ESTIMATE_VALID,
        .format=CERBERUS_CSI_FORMAT_LLTF_I8};
    for(unsigned i=0;i<53;++i){
        int k=tone(secondary,i);
        float a=gain*(40.0f+shape*sinf((float)k*0.47f));
        float p=phase+slope*k+phase_shape*sinf((float)k*0.37f);
        s.data[i*2]=(int8_t)lroundf(a*sinf(p));
        s.data[i*2+1]=(int8_t)lroundf(a*cosf(p));
    }
    return s;
}

static void reset(uint64_t t)
{ sequence=0;cerberus_csi_motion_reset(&model,t); }

static void feed_window(uint64_t start, unsigned secondary, unsigned kind, unsigned n)
{
    for(unsigned i=0;i<n;++i){
        float sign=(i&1)?1.0f:-1.0f;
        cerberus_csi_sample_t s=sample(start+(uint64_t)i*1000000U/n,secondary,
            kind==1?((i&1)?2.0f:1.0f):1.0f,
            kind==1?sign*2.9f:0.0f,kind==1?sign*0.03f:0.0f,
            kind==2?sign*23.0f:0.0f,kind==3?sign*1.1f:0.0f);
        if(kind==1 && (i&1))s.flags|=CERBERUS_CSI_SAMPLE_FIRST_INVALID;
        CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    }
    CHECK(cerberus_csi_motion_advance(&model,start+1000000U,model.view.dropped));
}

static void calibrate_at(uint64_t start,unsigned secondary)
{
    reset(start);
    for(unsigned i=0;i<30;++i){
        feed_window(start+(uint64_t)i*1000000U,secondary,0,20);
        CHECK(model.view.calibration_windows==i+1);
        CHECK(model.view.calibrated==(i==29));
        CHECK(model.view.calibration_percent==(i+1)*100U/30U);
    }
    CHECK(model.view.state==CERBERUS_CSI_MOTION_STILL && model.view.score==0);
    CHECK(model.view.reliability==100 && model.view.samples_per_second==20);
}

static void check_layouts(void)
{
    for(unsigned layout=0;layout<3;++layout){
        bool seen[53]={0};int last=-100;
        for(unsigned i=0;i<51;++i){
            uint8_t offset=255;int8_t k=127;
            CHECK(cerberus_csi_motion_carrier((uint8_t)layout,i,&offset,&k));
            CHECK(!(offset&1)&&offset>=4&&offset<=104&&!seen[offset/2]);seen[offset/2]=true;
            CHECK(k==tone(layout,offset/2)&&k!=0&&k>last);last=k;
        }
        CHECK(!seen[0]&&!seen[1]);
    }
    for(unsigned bad=0;bad<3;++bad){uint8_t offset=99;int8_t k=99;
        CHECK(!cerberus_csi_motion_carrier(bad==0?3:0,bad==1?51:0,bad==2?NULL:&offset,&k));
        CHECK(offset==99&&k==99);}
    reset(0);
    cerberus_csi_sample_t s=sample(0,0,1,0,0,0,0);
    for(size_t length=0;length<=sizeof(s)+1;++length){
        reset(0);s.sequence=1;
        CHECK(cerberus_csi_motion_observe(&model,&s,length)==(length==sizeof(s)));
    }
    for(unsigned bad=0;bad<12;++bad){
        reset(0);s=sample(0,0,1,0,0,0,0);
        if(bad==0)s.format=2;
        if(bad==1)s.flags=0;
        if(bad==2)s.flags|=4;
        if(bad==3)s.secondary=3;
        if(bad==4)s.phy=0;
        if(bad==5)s.phy=8;
        if(bad==6)s.channel=0;
        if(bad==7)s.channel=178;
        if(bad==8)s.rssi=127;
        if(bad==9)s.rssi=-128;
        if(bad==10)s.sequence=0;
        if(bad==11)memset(s.data,0,sizeof(s.data));
        CHECK(!cerberus_csi_motion_observe(&model,&s,sizeof(s)));
        CHECK(model.view.samples_rejected==1&&!model.view.samples_used&&!model.window_samples);
    }
    /* All documented OFDM PHY codes preserve the forced LLTF identity. */
    for(unsigned phy=0;phy<256;++phy){
        reset(0);s=sample(0,0,1,0,0,0,0);s.phy=(uint8_t)phy;
        CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s))==((phy>=1&&phy<=7)||phy==11));
    }
    /* Signed endpoints, invalid-first-word toggles and the exact unused mask. */
    reset(0);s=sample(0,0,1,0,0,0,0);
    for(unsigned i=2;i<53;++i){s.data[2*i]=-128;s.data[2*i+1]=127;}
    CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    cerberus_csi_motion_model_t before=model;
    s.sequence++;s.source_us=1;s.flags|=CERBERUS_CSI_SAMPLE_FIRST_INVALID;
    s.data[0]=-128;s.data[1]=127;s.data[2]=-99;s.data[3]=99;
    CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    CHECK(!model.view.layout_resets && model.window_pairs==1);
    for(unsigned i=0;i<51;++i){
        CHECK(model.previous_real[i]==127&&model.previous_imag[i]==-128);
        CHECK(model.previous_amplitude[i]==before.previous_amplitude[i]);
    }
}

static void check_windows(void)
{
    reset(0);
    cerberus_csi_sample_t s=sample(0,0,1,0,0,0,0);
    CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    s=sample(999999,0,1,0,0,0,0);CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    CHECK(model.window_samples==2&&!model.view.history_count);
    s=sample(1000000,0,1,0,0,0,0);CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    CHECK(model.view.samples_per_second==2&&model.window_samples==1&&model.window_pairs==0);
    CHECK(model.view.history_count==1&&model.view.history[0]==255&&model.view.state==CERBERUS_CSI_MOTION_LOW_DATA);
    /* Elapsed time alone never completes calibration; neither do dense bursts
     * within a single second or two sparse samples in every second. */
    reset(0);CHECK(cerberus_csi_motion_advance(&model,30000000,0));
    CHECK(!model.view.calibrated&&!model.view.calibration_windows&&model.view.state==CERBERUS_CSI_MOTION_LOW_DATA);
    reset(0);for(unsigned w=0;w<40;++w)feed_window((uint64_t)w*1000000,0,0,2);
    CHECK(!model.view.calibrated&&!model.view.calibration_windows);
    reset(0);feed_window(0,0,0,1000);CHECK(model.view.calibration_windows==1);
    CHECK(cerberus_csi_motion_advance(&model,60000000,0));CHECK(!model.view.calibrated&&model.view.calibration_windows==1);
    /* Gaps produce real invalid history positions, including full-width huge
     * jumps; bounded execution must not iterate billions of seconds. */
    calibrate_at(0,0);
    const uint64_t distant=(UINT64_C(1)<<50)+12345;
    CHECK(cerberus_csi_motion_advance(&model,distant,0));
    CHECK(model.view.history_count==160&&model.view.state==CERBERUS_CSI_MOTION_LOW_DATA&&!model.view.samples_per_second);
    for(unsigned i=0;i<160;++i)CHECK(model.view.history[i]==255);
    CHECK(model.window_us<=distant&&distant-model.window_us<1000000);
    CHECK(cerberus_csi_motion_advance(&model,UINT64_MAX,0));
    CHECK(model.latest_us==UINT64_MAX&&model.view.state==CERBERUS_CSI_MOTION_LOW_DATA);
    CHECK(!cerberus_csi_motion_advance(&model,UINT64_MAX-1,0));
    calibrate_at((UINT64_C(1)<<32)*1000,0);
    CHECK(model.view.calibrated);
    /* The graph is a chronological ring of completed seconds, not render
     * ticks; repeated same-time advances cannot append duplicate points. */
    calibrate_at(0,0);
    uint8_t expected[160];memset(expected,255,sizeof(expected));expected[29]=0;
    for(unsigned w=30;w<225;++w){
        bool silent=(w%7U)==0U;
        if(silent)CHECK(cerberus_csi_motion_advance(&model,(uint64_t)(w+1)*1000000,0));
        else feed_window((uint64_t)w*1000000,0,0,20);
        expected[w%160U]=silent?255:0;
        CHECK(model.view.history_head==(w+1U)%160U);
        CHECK(model.view.history_count==((w+1U)<160U?w+1U:160U));
        CHECK(!memcmp(expected,model.view.history,sizeof(expected)));
        const cerberus_csi_motion_view_t previous=model.view;
        CHECK(cerberus_csi_motion_advance(&model,(uint64_t)(w+1)*1000000,0));
        CHECK(!memcmp(&previous,&model.view,sizeof(previous)));
    }
}

static void check_motion_and_invariance(void)
{
    for(unsigned layout=0;layout<3;++layout){
        calibrate_at(0,layout);
        /* Gain, common phase, linear carrier slope, PHY and first-word flag
         * changes alone should stay below the possible-motion threshold. */
        for(unsigned w=30;w<38;++w)feed_window((uint64_t)w*1000000,layout,1,20);
        CHECK(model.view.state==CERBERUS_CSI_MOTION_STILL&&model.view.score<12&&!model.view.layout_resets);
        for(unsigned w=38;w<44;++w)feed_window((uint64_t)w*1000000,layout,2,20);
        CHECK(model.view.state==CERBERUS_CSI_MOTION_DETECTED&&model.view.score>=28);
        CHECK(model.view.feature_scores[0]>70&&model.view.feature_scores[1]>70&&model.view.feature_scores[3]>70);
        for(unsigned w=44;w<54;++w)feed_window((uint64_t)w*1000000,layout,0,20);
        CHECK(model.view.state==CERBERUS_CSI_MOTION_STILL&&model.view.score<12);
        for(unsigned w=54;w<60;++w)feed_window((uint64_t)w*1000000,layout,3,20);
        CHECK(model.view.feature_scores[2]>70&&model.view.state==CERBERUS_CSI_MOTION_DETECTED);
        CHECK(cerberus_csi_motion_advance(&model,61000000,UINT32_MAX));
        CHECK(model.view.state==CERBERUS_CSI_MOTION_LOW_DATA&&model.view.history[(model.view.history_head+159)%160]==255);
        feed_window(61000000,layout,2,20);
        CHECK(model.view.state==CERBERUS_CSI_MOTION_LOW_DATA&&!model.view.reliability);
    }
    /* A changed actual carrier layout must not borrow another layout's
     * reference or previous vector. Recalibration explicitly clears history. */
    calibrate_at(0,0);
    cerberus_csi_sample_t s=sample(30000000,1,1,0,0,0,0);
    CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    CHECK(!model.view.calibrated&&!model.view.calibration_windows&&!model.view.history_count&&
        model.view.layout_resets==1&&model.window_samples==1&&!model.window_pairs);
    s=sample(30000001,1,1,0,0,0,0);s.channel=149;
    CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s))&&model.view.layout_resets==2&&!model.window_pairs);
    cerberus_csi_motion_reset(&model,30000001);
    CHECK(!model.view.samples_used&&!model.view.samples_rejected&&!model.view.dropped&&!model.view.layout_resets);
    CHECK(!model.view.history_count&&!model.last_sequence&&!model.view.calibration_windows);
}

static void check_quality_and_transaction_boundaries(void)
{
    reset(0);
    cerberus_csi_sample_t s=sample(0,0,1,0,0,0,0);
    CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    CHECK(!cerberus_csi_motion_observe(&model,&s,sizeof(s))&&model.view.samples_used==1);
    s.sequence++;s.source_us=10;
    CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    s.sequence++;s.source_us=9;
    CHECK(!cerberus_csi_motion_observe(&model,&s,sizeof(s))&&model.latest_us==10&&model.view.state==CERBERUS_CSI_MOTION_LOW_DATA);
    CHECK(cerberus_csi_motion_advance(&model,20,3));
    CHECK(!cerberus_csi_motion_advance(&model,21,2)&&model.view.dropped==3);
    s.source_us=21;s.sequence=UINT32_MAX;CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    s.sequence=1;s.source_us=22;CHECK(!cerberus_csi_motion_observe(&model,&s,sizeof(s)));
    model.view.samples_rejected=UINT32_MAX;
    CHECK(!cerberus_csi_motion_observe(&model,NULL,0)&&model.view.samples_rejected==UINT32_MAX);
    for(unsigned bad=0;bad<2;++bad){
        reset(0);
        for(unsigned i=0;i<20;++i){s=sample(i*50000,0,1,0,0,0,0);
            s.rssi=bad?-110:((i&1)?-30:-90);
            CHECK(cerberus_csi_motion_observe(&model,&s,sizeof(s)));}
        CHECK(cerberus_csi_motion_advance(&model,1000000,100));
        CHECK(model.view.state==CERBERUS_CSI_MOTION_LOW_DATA&&!model.view.calibration_windows);
    }
    /* Robust baseline ignores a minority of changed windows instead of an
     * elapsed-only baseline; all features and scales remain finite. */
    reset(0);
    for(unsigned w=0;w<30;++w)feed_window((uint64_t)w*1000000,0,w==5||w==12?2:0,20);
    CHECK(model.view.calibrated);
    for(unsigned i=0;i<4;++i)CHECK(isfinite(model.baseline[i])&&model.baseline[i]<0.001f&&model.scale[i]>0);
    cerberus_csi_motion_view_t view;
    cerberus_csi_motion_view(&model,&view);CHECK(!memcmp(&view,&model.view,sizeof(view)));
    cerberus_csi_motion_view(NULL,&view);CHECK(view.state==CERBERUS_CSI_MOTION_LOW_DATA&&view.rssi==127);
    cerberus_csi_motion_reset(NULL,0);cerberus_csi_motion_view(&model,NULL);
    CHECK(!cerberus_csi_motion_observe(NULL,&s,sizeof(s))&&!cerberus_csi_motion_advance(NULL,0,0));
}

int main(void)
{
    check_layouts();check_windows();check_motion_and_invariance();check_quality_and_transaction_boundaries();
    printf("CSI motion model: %u checks passed; model=%zu view=%zu; synthetic evidence only, no hardware detection claim.\n",
        checks,sizeof(model),sizeof(cerberus_csi_motion_view_t));
    return 0;
}
