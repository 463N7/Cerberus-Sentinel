/* Private, main-task-only UI. Radio jobs and sockets remain on the onboard C5. */
static cerberus_net_scan_request_t net_request;
static cerberus_net_scan_status_t net_status;
static cerberus_net_scan_result_t net_results[CERBERUS_NET_SCAN_MAX_RESULTS];
static esp_err_t net_error;
static esp_err_t net_save_error;
static uint8_t net_loaded, net_page, net_mode, net_editing;
static bool net_pending, net_stop_requested;
static uint32_t net_last_poll;
static char net_target[16], net_ports[12] = "1-1024", net_message[49];
static uint8_t net_scan_mask[4];
static cerberus_keyboard_t net_keyboard;

static bool net_mode_supported(uint8_t mode)
{
    if (mode <= CERBERUS_NET_SCAN_TELNET_LAN) return true;
    if (mode <= CERBERUS_NET_SCAN_RDP_LAN) return c5_status.capabilities & CERBERUS_C5_CAP_SERVICE_SCANS;
    return mode < CERBERUS_NET_SCAN_MODE_COUNT && (c5_status.capabilities & CERBERUS_C5_CAP_HOST_DISCOVERY);
}

static bool net_busy(void)
{
    return net_pending || net_status.state == CERBERUS_NET_SCAN_RUNNING ||
        net_status.state == CERBERUS_NET_SCAN_STOPPING;
}
static const char *net_mode_name(void)
{
    static const char *names[] = {"16 COMMON PORTS", "CUSTOM TCP PORT RANGE", "SSH LAN DISCOVERY", "TELNET LAN DISCOVERY",
        "SMTP LAN / TCP 25", "DNS LAN / TCP 53", "HTTP LAN / TCP 80", "HTTPS LAN / TCP 443", "RDP LAN / TCP 3389",
        "PING LAN / ICMP ECHO", "ARP LAN / MAC RESOLUTION"};
    return net_mode < CERBERUS_NET_SCAN_MODE_COUNT ? names[net_mode] : "UNSUPPORTED MODE";
}
static void net_enter(void)
{
    if (!net_busy() && !net_mode_supported(net_mode)) net_mode = CERBERUS_NET_SCAN_COMMON;
    cerberus_wifi_connection_status_payload_t link = {0};
    if (c5_spi_link_get_wifi_connection_status(&link) != ESP_OK) return;
    if (!net_busy() && net_request.job_id &&
        (net_request.connection_generation != link.generation || net_request.connection_index != link.index)) {
        /* A report from a previous AP must not appear under the new target. */
        memset(&net_request,0,sizeof(net_request));
        memset(&net_status,0,sizeof(net_status));
        memset(net_results,0,sizeof(net_results));
        net_loaded=net_page=0;net_target[0]=net_message[0]=0;
        net_error=net_save_error=ESP_OK;net_stop_requested=false;
    }
    if (!net_target[0])
        snprintf(net_target, sizeof(net_target), "%u.%u.%u.%u", link.gateway[0], link.gateway[1], link.gateway[2], link.gateway[3]);
}
static void net_start(void)
{
    cerberus_wifi_connection_status_payload_t link = {0};
    cerberus_net_scan_status_t previous;
    net_message[0] = '\0';
    net_save_error = ESP_OK;
    if (!net_mode_supported(net_mode)) {
        snprintf(net_message,sizeof(net_message),"C5 DOES NOT SUPPORT THIS SCAN MODE");return;
    }
    net_error = c5_spi_link_get_wifi_connection_status(&link);
    if (net_error != ESP_OK) return;
    if (link.state != CERBERUS_WIFI_CONNECTION_CONNECTED) { net_error = ESP_ERR_INVALID_STATE; return; }
    net_error = c5_spi_link_network_status(&previous);
    if (net_error != ESP_OK) return;
    if (previous.state == CERBERUS_NET_SCAN_RUNNING || previous.state == CERBERUS_NET_SCAN_STOPPING) {
        snprintf(net_message, sizeof(net_message), "PREVIOUS JOB IS STILL STOPPING"); return;
    }
    cerberus_net_scan_request_t request = {.job_id = (uint16_t)(previous.job_id + 1U),
        .connection_generation = link.generation, .connection_index = link.index,
        .timeout_ms = 500, .mode = net_mode};
    if (!request.job_id) request.job_id = 1;
    if (net_mode >= CERBERUS_NET_SCAN_SSH_LAN) {
        request.first_port = request.last_port = cerberus_net_scan_service_port(net_mode);
        for(unsigned i=0;i<4;++i)request.target[i]=link.ipv4[i]&link.netmask[i];
    } else {
        if (!cerberus_net_parse_ipv4(net_target, request.target)) {
            snprintf(net_message, sizeof(net_message), "ENTER A VALID TARGET IPV4 ADDRESS");return;
        }
        if (net_mode == CERBERUS_NET_SCAN_RANGE) {
            uint16_t first,last;
            if (!cerberus_net_parse_ports(net_ports,&first,&last)) {
                snprintf(net_message,sizeof(net_message),"ENTER A VALID TCP PORT RANGE");return;
            }
            request.first_port=first;request.last_port=last;
        }
    }
    cerberus_net_scan_plan_t plan;
    if (!cerberus_net_scan_plan(&plan, &request, link.ipv4, link.netmask)) {
        snprintf(net_message, sizeof(net_message), net_mode >= CERBERUS_NET_SCAN_SSH_LAN ?
            "LAN DISCOVERY REQUIRES /24 TO /31" : "CHOOSE ANOTHER HOST ON THIS NETWORK"); return;
    }
    net_request = request;
    memcpy(net_scan_mask,link.netmask,sizeof(net_scan_mask));
    memset(&net_status, 0, sizeof(net_status));
    memset(net_results, 0, sizeof(net_results));
    net_loaded = net_page = 0;
    net_pending = true; net_stop_requested = false;
    net_error = c5_spi_link_network_start(&net_request, &net_status);
    if (net_error == ESP_OK) net_pending = false;
    /* On a lost START acknowledgement, keep polling the job ID. Never report
     * it stopped merely because the transport failed. The C5 lease is 5 s. */
}
static void net_cancel(void)
{
    if (!net_busy() || !net_request.job_id) return;
    net_stop_requested = true;
    net_error = c5_spi_link_network_stop(net_request.job_id, &net_status);
}
static void net_poll(uint32_t now, bool visible)
{
    if (!net_request.job_id || (uint32_t)(now - net_last_poll) < 500U) return;
    net_last_poll = now;
    if (!visible) {
        if (net_busy()) net_cancel();
        /* Do not renew the scan lease while hidden if STOP is not delivered. */
        return;
    }
    cerberus_net_scan_status_t status;
    net_error = c5_spi_link_network_status(&status);
    if (net_error != ESP_OK) return;
    if (status.job_id != net_request.job_id) {
        /* Our command was rejected or the radio rebooted. Its authoritative
         * status belongs to a different job, so discard no-longer-valid rows. */
        net_pending = false;
        net_status = (cerberus_net_scan_status_t){.state = CERBERUS_NET_SCAN_ERROR};
        net_error = ESP_ERR_INVALID_STATE; net_loaded = 0;
        return;
    }
    net_status = status; net_pending = false;
    if (net_stop_requested && status.state == CERBERUS_NET_SCAN_RUNNING) net_cancel();
    for (unsigned i = 0; i < 4 && net_loaded < net_status.result_count; ++i) {
        net_error = c5_spi_link_network_result(net_request.job_id, net_loaded, &net_results[net_loaded]);
        if (net_error != ESP_OK) break;
        ++net_loaded;
    }
}
static void net_save(void)
{
    if (net_busy() || net_error != ESP_OK || !net_request.job_id || net_loaded != net_status.result_count) {
        snprintf(net_message, sizeof(net_message), !net_request.job_id ? "RUN A SCAN BEFORE SAVING" :
            net_busy() ? "STOP OR COMPLETE THE SCAN FIRST" : "WAIT FOR ALL C5 RESULTS BEFORE SAVING");
        return;
    }
    net_save_error = ESP_OK;
    net_message[0] = '\0';
    char *csv = malloc(4096);
    if (!csv) { net_save_error = ESP_ERR_NO_MEM; return; }
    const char *columns = net_request.mode == CERBERUS_NET_SCAN_PING_LAN ? "ipv4,echo_reply,rtt_ms\n" :
        net_request.mode == CERBERUS_NET_SCAN_ARP_LAN ? "ipv4,mac,resolution_ms,was_cached\n" :
        "ipv4,tcp_port,connect_ms,ssh_banner,banner_prefix_hex\n";
    size_t used = (size_t)snprintf(csv, 4096,
        "# Cerberus network scan; job=%u; mode=%u; scope=%s; target=%u.%u.%u.%u; netmask=%u.%u.%u.%u; ports=%u-%u; state=%u; error=%ld; tested=%u/%u; matched=%u; retained=%u; closed=%u; filtered_or_timeout=%u; elapsed_ms=%lu\n%s",
        net_request.job_id, net_request.mode, net_request.mode>=CERBERUS_NET_SCAN_SSH_LAN?"connected_subnet":"one_host",
        net_request.target[0], net_request.target[1], net_request.target[2], net_request.target[3],
        net_scan_mask[0],net_scan_mask[1],net_scan_mask[2],net_scan_mask[3],net_request.first_port, net_request.last_port,
        net_status.state, (long)net_status.error_code, net_status.tested, net_status.total,
        net_status.open, net_loaded, net_status.closed, net_status.filtered, (unsigned long)net_status.elapsed_ms, columns);
    for (unsigned i = 0; i < net_loaded; ++i) {
        const cerberus_net_scan_result_t *r = &net_results[i];
        char hex[21];
        for (unsigned j = 0; j < sizeof(r->banner); ++j) snprintf(hex + j * 2, 3, "%02X", (uint8_t)r->banner[j]);
        int n;
        if (net_request.mode == CERBERUS_NET_SCAN_PING_LAN)
            n = snprintf(csv + used, 4096 - used, "%u.%u.%u.%u,%u,%u\n", r->ipv4[0], r->ipv4[1],
                r->ipv4[2], r->ipv4[3], !!(r->flags & CERBERUS_NET_SCAN_ICMP_REPLY), r->connect_ms);
        else if (net_request.mode == CERBERUS_NET_SCAN_ARP_LAN)
            n = snprintf(csv + used, 4096 - used, "%u.%u.%u.%u,%02X:%02X:%02X:%02X:%02X:%02X,%u,%u\n",
                r->ipv4[0], r->ipv4[1], r->ipv4[2], r->ipv4[3], (uint8_t)r->banner[0], (uint8_t)r->banner[1],
                (uint8_t)r->banner[2], (uint8_t)r->banner[3], (uint8_t)r->banner[4], (uint8_t)r->banner[5],
                r->connect_ms, !!(r->flags & CERBERUS_NET_SCAN_ARP_CACHED));
        else n = snprintf(csv + used, 4096 - used, "%u.%u.%u.%u,%u,%u,%u,%s\n", r->ipv4[0], r->ipv4[1],
                r->ipv4[2], r->ipv4[3], r->port, r->connect_ms, !!(r->flags & CERBERUS_NET_SCAN_SSH_BANNER), hex);
        if (n < 0 || (size_t)n >= 4096 - used) { net_save_error = ESP_ERR_INVALID_SIZE; break; }
        used += (size_t)n;
    }
    char name[32];
    if (net_save_error == ESP_OK) net_save_error = cerberus_storage_write_wifi_report(csv, used, name, sizeof(name));
    free(csv);
    if (net_save_error == ESP_OK) snprintf(net_message, sizeof(net_message), "SAVED %s", name);
}
static esp_err_t net_draw(void)
{
    if (net_editing) return cerberus_keyboard_draw(&net_keyboard, net_editing == 1 ? "TARGET IPV4" : "PORT OR RANGE: 1-65535");
    char line[64];
    uint8_t result_mode = net_request.job_id ? net_request.mode : net_mode;
    ESP_RETURN_ON_ERROR(draw_centered(66, "NETWORK SCAN", UI_TEXT, 2), TAG, "network title");
    ESP_RETURN_ON_ERROR(draw_centered(92, net_mode_name(), UI_MUTED, 1), TAG, "network mode");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8,110,188,36},
        net_mode >= CERBERUS_NET_SCAN_SSH_LAN ? "CONNECTED SUBNET" : net_target[0] ? net_target : "TARGET IPV4",
        1,net_mode < CERBERUS_NET_SCAN_SSH_LAN,UI_WIDGET_THEME),TAG,"network target");
    ESP_RETURN_ON_ERROR(draw_small_button(204, 110, 108, 36, "MODE"), TAG, "network mode button");
    snprintf(line, sizeof(line), net_mode >= CERBERUS_NET_SCAN_SSH_LAN ? "CONNECTED LAN / MAX 253 PEERS" :
        net_mode == CERBERUS_NET_SCAN_RANGE ? "PORTS %s" : "COMMON PORTS / INCLUDES SSH + TELNET", net_ports);
    ESP_RETURN_ON_ERROR(draw_small_button(8, 154, 304, 36, line), TAG, "network range");
    const char *state = net_pending ? "WAITING FOR C5" :
        net_status.state == CERBERUS_NET_SCAN_RUNNING ? "SCANNING" :
        net_status.state == CERBERUS_NET_SCAN_STOPPING ? "STOPPING" :
        net_status.state == CERBERUS_NET_SCAN_COMPLETE ? "COMPLETE" :
        net_status.state == CERBERUS_NET_SCAN_CANCELLED ? "CANCELLED" :
        net_status.state == CERBERUS_NET_SCAN_ERROR ? "STOPPED WITH ERROR" : "READY";
    snprintf(line, sizeof(line), "%s  %u/%u", state, net_status.tested, net_status.total);
    ESP_RETURN_ON_ERROR(draw_centered(204, line, UI_GOOD, 1), TAG, "network progress");
    if (result_mode >= CERBERUS_NET_SCAN_PING_LAN)
        snprintf(line, sizeof(line), "FOUND %u  NO REPLY %u", net_status.open, net_status.filtered);
    else snprintf(line, sizeof(line), "OPEN %u  CLOSED %u  NO REPLY %u", net_status.open, net_status.closed, net_status.filtered);
    ESP_RETURN_ON_ERROR(draw_centered(224, line, UI_TEXT, 1), TAG, "network counts");
    if (net_save_error != ESP_OK)
        snprintf(line, sizeof(line), "SAVE ERROR %s", esp_err_to_name(net_save_error));
    else if (net_error != ESP_OK || net_status.error_code != ESP_OK)
        snprintf(line, sizeof(line), "ERROR %s", esp_err_to_name(net_error != ESP_OK ? net_error : net_status.error_code));
    else if (net_message[0]) snprintf(line, sizeof(line), "%s", net_message);
    else if (net_status.open > net_status.result_count) snprintf(line, sizeof(line), "FIRST 64 MATCHING RESULTS RETAINED");
    else if (result_mode == CERBERUS_NET_SCAN_PING_LAN) snprintf(line, sizeof(line), "ICMP ECHO REPLIES / NO REPLY MAY BE FILTERED");
    else if (result_mode == CERBERUS_NET_SCAN_ARP_LAN) snprintf(line, sizeof(line), "CACHE = KNOWN BEFORE THIS ARP REQUEST");
    else snprintf(line, sizeof(line), "TCP OPEN IS NOT A SERVICE IDENTITY");
    ESP_RETURN_ON_ERROR(draw_centered(246, line, UI_MUTED, 1), TAG, "network note");
    for (unsigned row = 0; row < 4; ++row) {
        unsigned index = net_page * 4U + row;
        if (index >= net_loaded) break;
        const cerberus_net_scan_result_t *r = &net_results[index];
        if (r->flags & CERBERUS_NET_SCAN_ARP_RESOLVED)
            snprintf(line, sizeof(line), "%u.%u.%u.%u %02X:%02X:%02X:%02X:%02X:%02X%s",
                r->ipv4[0], r->ipv4[1], r->ipv4[2], r->ipv4[3], (uint8_t)r->banner[0], (uint8_t)r->banner[1],
                (uint8_t)r->banner[2], (uint8_t)r->banner[3], (uint8_t)r->banner[4], (uint8_t)r->banner[5],
                r->flags & CERBERUS_NET_SCAN_ARP_CACHED ? " CACHE" : "");
        else if (r->flags & CERBERUS_NET_SCAN_ICMP_REPLY)
            snprintf(line, sizeof(line), "%u.%u.%u.%u  ECHO %u MS", r->ipv4[0], r->ipv4[1], r->ipv4[2], r->ipv4[3], r->connect_ms);
        else snprintf(line, sizeof(line), "%u.%u.%u.%u:%u  %s", r->ipv4[0], r->ipv4[1], r->ipv4[2], r->ipv4[3],
                r->port, r->flags & CERBERUS_NET_SCAN_SSH_BANNER ? "SSH BANNER" : "TCP OPEN");
        ESP_RETURN_ON_ERROR(draw_centered((uint16_t)(278 + row * 22), line, UI_TEXT, 1), TAG, "network result");
    }
    ESP_RETURN_ON_ERROR(draw_small_button(8, 378, 90, 36, net_busy() ? "STOP" : "START"), TAG, "network start");
    ESP_RETURN_ON_ERROR(draw_small_button(104, 378, 80, 36, "NEXT"), TAG, "network next");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){192, 378, 120, 36}, "SAVE TO SD", 1,
        !net_busy() && net_request.job_id && net_error == ESP_OK && net_loaded == net_status.result_count, UI_WIDGET_THEME);
}
static void net_tap(uint16_t x, uint16_t y)
{
    if (net_editing) {
        cerberus_keyboard_action_t action;
        if (cerberus_keyboard_handle_tap(&net_keyboard, x, y, &action) == ESP_OK && action == CERBERUS_KEYBOARD_ACTION_SUBMIT)
            net_editing = 0;
        return;
    }
    if (y >= 378 && y < 414) {
        if (x >= 8 && x < 98) { if (net_busy()) net_cancel(); else net_start(); }
        else if (x >= 104 && x < 184) net_page = (net_page + 1U) * 4U >= net_loaded ? 0 : net_page + 1U;
        else if (x >= 192 && x < 312) net_save();
    } else if (!net_busy()) {
        if (y >= 110 && y < 146 && x >= 8 && x < 196 && net_mode < CERBERUS_NET_SCAN_SSH_LAN) {
            char initial[16]; memcpy(initial, net_target, sizeof(initial));
            net_editing = 1;
            (void)cerberus_keyboard_begin(&net_keyboard, net_target, sizeof(net_target), initial);
        } else if (y >= 110 && y < 146 && x >= 204 && x < 312) {
            do { net_mode = (net_mode + 1U) % CERBERUS_NET_SCAN_MODE_COUNT; } while (!net_mode_supported(net_mode));
        }
        else if (y >= 154 && y < 190 && x >= 8 && x < 312 && net_mode == CERBERUS_NET_SCAN_RANGE) {
            char initial[12]; memcpy(initial, net_ports, sizeof(initial));
            net_editing = 2;
            (void)cerberus_keyboard_begin(&net_keyboard, net_ports, sizeof(net_ports), initial);
        }
    }
}
