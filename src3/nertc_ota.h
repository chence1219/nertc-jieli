#ifndef NERTC_OTA_H
#define NERTC_OTA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    char mqtt_endpoint[256];
    char mqtt_client_id[256];
    char mqtt_username[256];
    char mqtt_password[256];
    char mqtt_publish_topic[256];
    char firmware_version[64];
    char firmware_url[512];
    int interrupt_mode;
} nertc_ota_result_t;

int nertc_ota_check(nertc_ota_result_t *result,
                    const char *app_key,
                    const char *device_id,
                    const char *device_mac,
                    const char *version);

#endif
