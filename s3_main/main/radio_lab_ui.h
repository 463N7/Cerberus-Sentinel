/* UI-task owner for one explicitly started local radio test. */
static cerberus_radio_test_status_t lab_status;
static cerberus_radio_test_config_t lab_config;
static uint8_t lab_mode=CERBERUS_TEST_WIFI_BEACON,lab_channel=6,lab_seconds=10;
static esp_err_t lab_error,lab_save_error;
static uint32_t lab_last_poll;
static bool lab_pending;
static char lab_message[49],lab_name[33]="CERBERUS-LAB";
static bool lab_busy(void)
{return lab_pending||lab_status.state==CERBERUS_TEST_RUNNING||lab_status.state==CERBERUS_TEST_STOPPING;}
static esp_err_t lab_load_name(void)
{
    uint8_t data[35];size_t got=0;uint64_t size=0;
    esp_err_t err=cerberus_storage_read_file_chunk("/CERBERUS/TESTS",lab_mode==CERBERUS_TEST_WIFI_BEACON?"WIFI.TXT":"BLE.TXT",0,data,sizeof(data),&got,&size);
    if(err==ESP_ERR_NOT_FOUND||err==ESP_ERR_INVALID_STATE){
        snprintf(lab_name,sizeof(lab_name),"%s",lab_mode==CERBERUS_TEST_WIFI_BEACON?"CERBERUS-LAB":"Cerberus Lab");return ESP_OK;
    }
    if(err!=ESP_OK)return err;
    if(size!=got||got>34)return ESP_ERR_INVALID_SIZE;
    while(got&&(data[got-1]=='\r'||data[got-1]=='\n'))--got;
    if(!got||got>(lab_mode==CERBERUS_TEST_WIFI_BEACON?32U:26U))return ESP_ERR_INVALID_SIZE;
    for(size_t i=0;i<got;++i)if(data[i]<32||data[i]>126)return ESP_ERR_INVALID_ARG;
    memcpy(lab_name,data,got);lab_name[got]=0;return ESP_OK;
}
static void lab_start(void)
{
    lab_message[0]=0;lab_save_error=ESP_OK;
    lab_error=lab_load_name();if(lab_error!=ESP_OK)return;
    cerberus_radio_test_status_t previous={0};
    if(lab_mode==CERBERUS_TEST_WIFI_BEACON){
        if(!c5_online||!(c5_status.capabilities&CERBERUS_C5_CAP_RADIO_TEST)){lab_error=ESP_ERR_NOT_SUPPORTED;return;}
        lab_error=c5_spi_link_test_status(0,&previous);
        if(lab_error!=ESP_OK)return;
        if(c5_status.radio_state&(CERBERUS_C5_RADIO_WIFI_CONNECTED|CERBERUS_C5_RADIO_WIFI_SCAN|CERBERUS_C5_RADIO_BLE_SCAN|CERBERUS_C5_RADIO_BLE_CONNECTION|CERBERUS_C5_RADIO_IEEE802154_SCAN|CERBERUS_C5_RADIO_WIFI_DEFENSE_SURVEY)){
            snprintf(lab_message,sizeof(lab_message),"DISCONNECT WIFI AND STOP C5 SCANS FIRST");return;
        }
    } else cerberus_ble_test_status(0,&previous);
    if(previous.state==CERBERUS_TEST_RUNNING||previous.state==CERBERUS_TEST_STOPPING){lab_error=ESP_ERR_INVALID_STATE;return;}
    lab_config=(cerberus_radio_test_config_t){.job_id=(uint16_t)(previous.job_id+1U),.mode=lab_mode,
        .channel=lab_mode==CERBERUS_TEST_WIFI_BEACON?lab_channel:0,.seconds=lab_seconds,.name_length=(uint8_t)strlen(lab_name)};
    if(!lab_config.job_id)lab_config.job_id=1;
    memcpy(lab_config.name,lab_name,lab_config.name_length);
    memset(&lab_status,0,sizeof(lab_status));lab_pending=true;
    if(lab_mode==CERBERUS_TEST_WIFI_BEACON){
        lab_error=c5_spi_link_test_start(&lab_config,&lab_status);
        /* A lost START acknowledgement is resolved by status, never assumed stopped. */
        if(lab_error==ESP_OK||lab_status.error_code!=ESP_OK)lab_pending=false;
        if(lab_status.error_code!=ESP_OK)lab_status.state=CERBERUS_TEST_ERROR;
    } else {
        lab_error=cerberus_ble_test_start(&lab_config);lab_pending=false;
        if(lab_error==ESP_OK)cerberus_ble_test_status(lab_config.job_id,&lab_status);
        else {lab_status.job_id=lab_config.job_id;lab_status.state=CERBERUS_TEST_ERROR;lab_status.error_code=lab_error;}
    }
}
static void lab_stop(void)
{
    if(!lab_busy()||!lab_config.job_id)return;
    if(lab_config.mode==CERBERUS_TEST_WIFI_BEACON){
        lab_error=c5_spi_link_test_stop(lab_config.job_id,&lab_status);
        if(lab_error==ESP_OK&&lab_status.job_id==lab_config.job_id)lab_pending=false;
    } else {cerberus_ble_test_stop(lab_config.job_id);cerberus_ble_test_status(0,&lab_status);}
}
static void lab_tick(uint32_t now,bool visible)
{
    if(!lab_config.job_id||!lab_busy()||(uint32_t)(now-lab_last_poll)<250U)return;
    lab_last_poll=now;
    if(!visible){lab_stop();return;}
    cerberus_radio_test_status_t next;
    esp_err_t err=ESP_OK;
    if(lab_config.mode==CERBERUS_TEST_WIFI_BEACON)err=c5_spi_link_test_status(lab_config.job_id,&next);
    else cerberus_ble_test_status(lab_config.job_id,&next);
    if(err!=ESP_OK){lab_error=err;return;}
    if(next.job_id!=lab_config.job_id){if(lab_busy()){lab_pending=false;lab_status.state=CERBERUS_TEST_ERROR;lab_error=ESP_ERR_INVALID_STATE;}return;}
    if(next.state==CERBERUS_TEST_STAGED){
        lab_status=next;lab_pending=false;lab_status.state=CERBERUS_TEST_ERROR;
        lab_status.error_code=lab_error!=ESP_OK?lab_error:ESP_ERR_INVALID_STATE;return;
    }
    lab_status=next;lab_pending=false;lab_error=ESP_OK;
}
static void lab_save(void)
{
    if(lab_busy()||!lab_config.job_id)return;
    char csv[512],name[32];
    int length=snprintf(csv,sizeof(csv),"# Cerberus radio test; discovery advertisement only\njob,processor,mode,channel,seconds,state,elapsed_ms,driver_accepted_frames,counts_valid,error,min_free_heap_kib\n%u,%s,%u,%u,%u,%u,%lu,%lu,%u,%ld,%u\n",
        lab_config.job_id,lab_config.mode==CERBERUS_TEST_WIFI_BEACON?"C5":"S3",lab_config.mode,lab_config.channel,lab_config.seconds,lab_status.state,
        (unsigned long)lab_status.elapsed_ms,(unsigned long)lab_status.accepted_frames,lab_status.counts_valid,(long)lab_status.error_code,lab_status.minimum_heap_kib);
    lab_save_error=length>0&&(size_t)length<sizeof(csv)?cerberus_storage_write_diagnostic_report(csv,(size_t)length,name,sizeof(name)):ESP_ERR_INVALID_SIZE;
    if(lab_save_error==ESP_OK)snprintf(lab_message,sizeof(lab_message),"SAVED %s",name);
}
static esp_err_t lab_draw(void)
{
    char line[64];
    ESP_RETURN_ON_ERROR(draw_centered(66,"RADIO LAB",UI_TEXT,2),TAG,"lab title");
    ESP_RETURN_ON_ERROR(draw_small_button(12,100,296,42,lab_mode==CERBERUS_TEST_WIFI_BEACON?"C5 / WIFI BEACON TEST":"S3 / BLE ADVERTISING TEST"),TAG,"lab mode");
    snprintf(line,sizeof(line),lab_mode==CERBERUS_TEST_WIFI_BEACON?"CHANNEL %u / TAP TO CHANGE":"BLE / ADVERTISING CHANNELS",lab_channel);
    ESP_RETURN_ON_ERROR(draw_small_button(12,152,296,36,line),TAG,"lab channel");
    snprintf(line,sizeof(line),"DURATION %u SECONDS / TAP TO CHANGE",lab_seconds);
    ESP_RETURN_ON_ERROR(draw_small_button(12,198,296,36,line),TAG,"lab duration");
    ESP_RETURN_ON_ERROR(draw_centered(246,lab_name,UI_TEXT,1),TAG,"lab name");
    const char *state=lab_pending?"WAITING FOR RADIO":lab_status.state==CERBERUS_TEST_RUNNING?"TRANSMITTING":lab_status.state==CERBERUS_TEST_STOPPING?"STOPPING":lab_status.state==CERBERUS_TEST_COMPLETE?"COMPLETE":lab_status.state==CERBERUS_TEST_CANCELLED?"CANCELLED":lab_status.state==CERBERUS_TEST_ERROR?"STOPPED WITH ERROR":"READY";
    snprintf(line,sizeof(line),"%s / %lu MS",state,(unsigned long)lab_status.elapsed_ms);
    ESP_RETURN_ON_ERROR(draw_centered(268,line,UI_GOOD,1),TAG,"lab state");
    if(lab_status.counts_valid)snprintf(line,sizeof(line),"DRIVER ACCEPTED %lu BEACONS",(unsigned long)lab_status.accepted_frames);
    else snprintf(line,sizeof(line),"ONE ADVERTISER / 100 MS INTERVAL");
    ESP_RETURN_ON_ERROR(draw_centered(290,line,UI_MUTED,1),TAG,"lab count");
    esp_err_t error=lab_save_error!=ESP_OK?lab_save_error:lab_error!=ESP_OK?lab_error:lab_status.error_code;
    if(error!=ESP_OK)snprintf(line,sizeof(line),"%s %s",lab_save_error!=ESP_OK?"SAVE ERROR":"ERROR",esp_err_to_name(error));
    else snprintf(line,sizeof(line),"%s",lab_message[0]?lab_message:"START TRANSMITS / LEAVING STOPS");
    ESP_RETURN_ON_ERROR(draw_centered(316,line,UI_MUTED,1),TAG,"lab note");
    ESP_RETURN_ON_ERROR(draw_centered(338,"PROFILE NAMES: SD /CERBERUS/TESTS",UI_MUTED,1),TAG,"lab profile");
    ESP_RETURN_ON_ERROR(draw_small_button(12,364,144,42,lab_busy()?"STOP":"START"),TAG,"lab start");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){164,364,144,42},"SAVE REPORT",1,!lab_busy()&&lab_config.job_id,UI_WIDGET_THEME);
}
static void lab_tap(uint16_t x,uint16_t y)
{
    if(y>=364&&y<406){if(x>=12&&x<156){if(lab_busy())lab_stop();else lab_start();}else if(x>=164&&x<308)lab_save();return;}
    if(lab_busy()||x<12||x>=308)return;
    bool edited=false;
    if(y>=100&&y<142){lab_mode=lab_mode==CERBERUS_TEST_WIFI_BEACON?CERBERUS_TEST_BLE_ADVERT:CERBERUS_TEST_WIFI_BEACON;edited=true;}
    else if(y>=152&&y<188&&lab_mode==CERBERUS_TEST_WIFI_BEACON){const uint8_t channels[]={1,6,11,36,40,44,48};for(unsigned i=0;i<sizeof(channels);++i)if(lab_channel==channels[i]){lab_channel=channels[(i+1)%sizeof(channels)];break;}edited=true;}
    else if(y>=198&&y<234){lab_seconds=lab_seconds==10?20:lab_seconds==20?30:10;edited=true;}
    if(edited){memset(&lab_status,0,sizeof(lab_status));memset(&lab_config,0,sizeof(lab_config));lab_message[0]=0;lab_error=lab_load_name();lab_save_error=ESP_OK;}
}
