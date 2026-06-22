#include "wifi_app_task.h"

#include "app_config.h"
#include "system/includes.h"
#include "init.h"
#include "wifi/wifi_connect.h"
#include "event/net_event.h"
#include <stdio.h>
#include <string.h>

#define CONNECT_TIMEOUT_SEC 60

#define WIFI_INIT_MODE STA_MODE
#define WIFI_FORCE_DEFAULT_MODE 1
#define WIFI_INIT_SSID APP_WIFI_SSID
#define WIFI_INIT_PWD APP_WIFI_PASSWORD
#define WIFI_INIT_CONNECT_BEST_SSID 0
#define WIFI_INIT_STORED_SSID 0

static struct {
    u8 request_connect_flag;
} wifi_app_state;

#define __this (&wifi_app_state)

static int wifi_config_is_valid(void)
{
    return APP_WIFI_SSID[0] != '\0' && APP_WIFI_PASSWORD[0] != '\0';
}

void wifi_return_sta_mode(void)
{
    if (!wifi_config_is_valid()) {
        puts("wifi config missing, skip sta reconnect\n");
        return;
    }

    if (!wifi_is_on()) {
        return;
    }

    wifi_clear_scan_result();
    wifi_set_sta_connect_best_ssid(0);
    wifi_enter_sta_mode(WIFI_INIT_SSID, WIFI_INIT_PWD);
}

static int wifi_event_callback(void *network_ctx, enum WIFI_EVENT event)
{
    struct net_event net = {0};
    struct wifi_store_info wifi_default_mode_parm;

    (void)network_ctx;

    switch (event) {
    case WIFI_EVENT_MODULE_INIT:
        memset(&wifi_default_mode_parm, 0, sizeof(wifi_default_mode_parm));
        wifi_default_mode_parm.mode = WIFI_INIT_MODE;
        strncpy((char *)wifi_default_mode_parm.ssid[wifi_default_mode_parm.mode - STA_MODE],
                WIFI_INIT_SSID,
                sizeof(wifi_default_mode_parm.ssid[wifi_default_mode_parm.mode - STA_MODE]) - 1);
        strncpy((char *)wifi_default_mode_parm.pwd[wifi_default_mode_parm.mode - STA_MODE],
                WIFI_INIT_PWD,
                sizeof(wifi_default_mode_parm.pwd[wifi_default_mode_parm.mode - STA_MODE]) - 1);
        wifi_default_mode_parm.connect_best_network = WIFI_INIT_CONNECT_BEST_SSID;
        wifi_set_default_mode(&wifi_default_mode_parm,
                              WIFI_FORCE_DEFAULT_MODE,
                              WIFI_INIT_STORED_SSID);
        break;

    case WIFI_EVENT_STA_CONNECT_SUCC:
        printf("|wifi_event_callback->WIFI_STA_CONNECT_SUCC,CH=%d\r\n", wifi_get_channel());
        break;

    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC:
        printf("|wifi_event_callback->WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC\r\n");
        wifi_set_sta_connect_best_ssid(0);
        __this->request_connect_flag = 0;
        net.arg = "net";
        net.event = NET_EVENT_CONNECTED;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;

    case WIFI_EVENT_STA_DISCONNECT:
        printf("|wifi_event_callback->WIFI_STA_DISCONNECT\r\n");
        net.arg = "net";
        net.event = NET_EVENT_DISCONNECTED;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        if (!__this->request_connect_flag) {
            net.event = NET_EVENT_DISCONNECTED_AND_REQ_CONNECT;
            net_event_notify(NET_EVENT_FROM_USER, &net);
        }
        break;

    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
        printf("|wifi_event_callback->WIFI_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID\r\n");
        net.arg = "net";
        net.event = NET_CONNECT_TIMEOUT_NOT_FOUND_SSID;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;

    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
        printf("|wifi_event_callback->WIFI_STA_CONNECT_ASSOCIAT_FAIL\r\n");
        net.arg = "net";
        net.event = NET_CONNECT_ASSOCIAT_FAIL;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;

    default:
        break;
    }

    return 0;
}

static void wifi_and_network_on(void)
{
    if (!wifi_config_is_valid()) {
        puts("wifi config missing, skip wifi start\n");
        return;
    }

    if (wifi_is_on()) {
        return;
    }

    if (APP_WIFI_START_DELAY_TICKS > 0) {
        os_time_dly(APP_WIFI_START_DELAY_TICKS);
    }

    wifi_on();
}

static void wifi_app_task(void *priv)
{
    (void)priv;

    wifi_set_store_ssid_cnt(NETWORK_SSID_INFO_CNT);
    wifi_set_sta_connect_timeout(CONNECT_TIMEOUT_SEC);
    wifi_set_event_callback(wifi_event_callback);
    wifi_and_network_on();
}

static int wireless_net_init(void)
{
    puts("wireless_net_init\n");
    return thread_fork("wifi_app_task", 10, 1792, 0, 0, wifi_app_task, NULL);
}
late_initcall(wireless_net_init);
