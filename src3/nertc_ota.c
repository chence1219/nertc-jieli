#include "app_config.h"
#include "system/includes.h"
#include "nertc_log.h"
#include "nertc_ota.h"
#include "http/http_cli.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "OTA"
#define OTA_RESP_BUF_SIZE 4096

typedef struct {
    char *buf;
    int length;
    int capacity;
    bool overflow;
} ota_response_t;

static int ota_http_callback(void *ctx, void *buf, unsigned int size, void *priv, httpin_status status)
{
    ota_response_t *resp = (ota_response_t *)priv;
    int remain;
    int copy_len;

    (void)ctx;

    if ((status != HTTPIN_PROGRESS && status != HTTPIN_FINISHED) || !resp || !buf || size == 0) {
        return 0;
    }

    remain = resp->capacity - resp->length;
    copy_len = (int)size < remain ? (int)size : remain;
    if (copy_len > 0) {
        memcpy(resp->buf + resp->length, buf, copy_len);
        resp->length += copy_len;
        resp->buf[resp->length] = '\0';
    } else {
        resp->overflow = true;
    }

    return 0;
}

static bool json_extract_string(const char *json, const char *key, char *out, int out_len)
{
    char search[128];
    const char *pos;
    const char *end;
    int len;

    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strstr(json, search);
    if (!pos) {
        return false;
    }

    pos = strchr(pos + strlen(search), ':');
    if (!pos) {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    if (*pos != '"') {
        return false;
    }
    pos++;

    end = strchr(pos, '"');
    if (!end) {
        return false;
    }
    len = end - pos;
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, pos, len);
    out[len] = '\0';
    return true;
}

static bool json_extract_int(const char *json, const char *key, int *out)
{
    char search[128];
    const char *pos;

    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strstr(json, search);
    if (!pos) {
        return false;
    }

    pos = strchr(pos + strlen(search), ':');
    if (!pos) {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    *out = atoi(pos);
    return true;
}

int nertc_ota_check(nertc_ota_result_t *result,
                    const char *app_key,
                    const char *device_id,
                    const char *device_mac,
                    const char *version)
{
    char *resp_buf;
    ota_response_t resp;
    char url[512];
    char post_body[2048];
    char http_headers[768];
    httpcli_ctx http_ctx;
    httpin_error ret;

    (void)device_mac;

    if (!result || !app_key || !device_id || !version) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    resp_buf = (char *)malloc(OTA_RESP_BUF_SIZE);
    if (!resp_buf) {
        return -1;
    }

    memset(resp_buf, 0, OTA_RESP_BUF_SIZE);
    resp.buf = resp_buf;
    resp.length = 0;
    resp.capacity = OTA_RESP_BUF_SIZE - 1;
    resp.overflow = false;

    snprintf(url, sizeof(url), "https://%s/v1/ota?appkey=%s", NERTC_OTA_HOST, app_key);
    snprintf(post_body, sizeof(post_body),
             "{"
             "\"version\":2,"
             "\"language\":\"zh-CN\","
             "\"flash_size\":16777216,"
             "\"minimum_free_heap_size\":\"8428816\","
             "\"chip_model_name\":\"jieli-ac7911\","
             "\"chip_info\":{\"model\":9,\"cores\":2,\"revision\":2,\"features\":18},"
             "\"application\":{\"name\":\"" NERTC_OTA_APP_NAME "\",\"version\":\"%s\",\"device_id\":\"%s\",\"compile_time\":\"%sT%sZ\",\"elf_sha256\":\"\"},"
             "\"partition_table\":[],"
             "\"ota\":{\"label\":\"ota_0\"},"
             "\"display\":{\"monochrome\":false,\"width\":0,\"height\":0},"
             "\"board\":{\"type\":\"jieli\",\"name\":\"" NERTC_OTA_BOARD_NAME "\"}"
             "}",
             version, device_id, __DATE__, __TIME__);
    snprintf(http_headers, sizeof(http_headers),
             "POST /v1/ota?appkey=%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Device-Id: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             app_key, NERTC_OTA_HOST, device_id, (int)strlen(post_body));

    printf("ota url:%s request:%s\n", url, post_body);
    memset(&http_ctx, 0, sizeof(http_ctx));
    http_ctx.url = url;
    http_ctx.post_data = post_body;
    http_ctx.data_len = strlen(post_body);
    http_ctx.timeout_millsec = NERTC_OTA_TIMEOUT_MS;
    http_ctx.cb = ota_http_callback;
    http_ctx.priv = &resp;
    http_ctx.mode = MODE_HTTPS;
    http_ctx.user_http_header = http_headers;
    http_ctx.connection = "close";

    ret = httpcli_post(&http_ctx);
    httpcli_close(&http_ctx);
    if (ret != HERROR_OK || resp.length == 0 || resp.overflow) {
        free(resp_buf);
        return -1;
    }

    printf("ota response:%s\n", resp_buf);
    json_extract_string(resp_buf, "endpoint", result->mqtt_endpoint, sizeof(result->mqtt_endpoint));
    json_extract_string(resp_buf, "client_id", result->mqtt_client_id, sizeof(result->mqtt_client_id));
    json_extract_string(resp_buf, "username", result->mqtt_username, sizeof(result->mqtt_username));
    json_extract_string(resp_buf, "password", result->mqtt_password, sizeof(result->mqtt_password));
    json_extract_string(resp_buf, "publish_topic", result->mqtt_publish_topic, sizeof(result->mqtt_publish_topic));
    json_extract_string(resp_buf, "version", result->firmware_version, sizeof(result->firmware_version));
    json_extract_string(resp_buf, "url", result->firmware_url, sizeof(result->firmware_url));
    json_extract_int(resp_buf, "interrupt_mode", &result->interrupt_mode);

    if (result->mqtt_endpoint[0] != '\0' &&
        result->mqtt_client_id[0] != '\0' &&
        result->mqtt_username[0] != '\0' &&
        result->mqtt_password[0] != '\0' &&
        result->mqtt_publish_topic[0] != '\0') {
        result->valid = true;
    }

    free(resp_buf);
    return 0;
}
