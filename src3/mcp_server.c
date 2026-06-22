#include "mcp_server.h"

#include "music_player.h"
#include "nertc_log.h"
#include "cJSON.h"

#include <string.h>

#define TAG "MCP"

static void (*g_send_func)(const char *payload, int len) = NULL;

static cJSON *mcp_build_text_result(const char *text, int is_error)
{
    cJSON *item;
    cJSON *content;
    cJSON *result;

    item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text ? text : "");

    content = cJSON_CreateArray();
    cJSON_AddItemToArray(content, item);

    result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "content", content);
    cJSON_AddBoolToObject(result, "isError", is_error);
    return result;
}

static void mcp_reply_jsonrpc(cJSON *id, cJSON *result, const char *error_text)
{
    cJSON *resp;
    char *str;

    if (!g_send_func) {
        if (result) {
            cJSON_Delete(result);
        }
        return;
    }

    resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    if (id) {
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
    } else {
        cJSON_AddNullToObject(resp, "id");
    }

    if (error_text) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "message", error_text);
        cJSON_AddItemToObject(resp, "error", error);
        if (result) {
            cJSON_Delete(result);
        }
    } else {
        cJSON_AddItemToObject(resp, "result", result ? result : cJSON_CreateObject());
    }

    str = cJSON_PrintUnformatted(resp);
    if (str) {
        g_send_func(str, (int)strlen(str));
        cJSON_free(str);
    }
    cJSON_Delete(resp);
}

static cJSON *mcp_handle_initialize(void)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *caps = cJSON_CreateObject();
    cJSON *server = cJSON_CreateObject();

    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
    cJSON_AddItemToObject(caps, "tools", cJSON_CreateObject());
    cJSON_AddItemToObject(result, "capabilities", caps);

    cJSON_AddStringToObject(server, "name", "nertc-jl-demo");
    cJSON_AddStringToObject(server, "version", "1.0.0");
    cJSON_AddItemToObject(result, "serverInfo", server);
    return result;
}

static cJSON *mcp_handle_tools_list(void)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    cJSON *tool;

    tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "name", "self.audio_speaker.set_volume");
    cJSON_AddNumberToObject(tool, "version", 1);
    cJSON_AddItemToArray(tools, tool);

    cJSON_AddItemToObject(result, "tools", tools);
    return result;
}

static cJSON *mcp_call_set_volume(cJSON *arguments)
{
    cJSON *vol = cJSON_GetObjectItem(arguments, "volume");
    int value;

    if (!cJSON_IsNumber(vol)) {
        return mcp_build_text_result("missing volume", 1);
    }

    value = music_player_set_volume(vol->valueint);
    if (value < 0) {
        return mcp_build_text_result("set volume failed", 1);
    }

    return mcp_build_text_result("accepted", 0);
}

static cJSON *mcp_handle_tools_call(cJSON *params)
{
    cJSON *name;
    cJSON *arguments;
    const char *tool_name;
    cJSON *empty_args = NULL;

    if (!cJSON_IsObject(params)) {
        return mcp_build_text_result("missing params", 1);
    }

    name = cJSON_GetObjectItem(params, "name");
    arguments = cJSON_GetObjectItem(params, "arguments");

    if (!cJSON_IsString(name) || !name->valuestring) {
        return mcp_build_text_result("missing tool name", 1);
    }

    if (!cJSON_IsObject(arguments)) {
        empty_args = cJSON_CreateObject();
        arguments = empty_args;
    }

    tool_name = name->valuestring;
    if (strcmp(tool_name, "self.audio_speaker.set_volume") == 0) {
        cJSON *result = mcp_call_set_volume(arguments);
        if (empty_args) {
            cJSON_Delete(empty_args);
        }
        return result;
    }

    if (empty_args) {
        cJSON_Delete(empty_args);
    }
    return mcp_build_text_result("unknown tool", 1);
}

void mcp_server_init(void)
{
    g_send_func = NULL;
}

void mcp_server_set_send_func(void (*send_func)(const char *payload, int len))
{
    g_send_func = send_func;
}

int mcp_server_parse_message(const char *json_str)
{
    cJSON *root;
    cJSON *version;
    cJSON *method;
    cJSON *id;
    cJSON *params;
    cJSON *result = NULL;

    if (!json_str || !*json_str) {
        return -1;
    }

    root = cJSON_Parse(json_str);
    if (!root) {
        NERTC_LOGE("mcp parse failed");
        return -1;
    }

    version = cJSON_GetObjectItem(root, "jsonrpc");
    method = cJSON_GetObjectItem(root, "method");
    id = cJSON_GetObjectItem(root, "id");
    params = cJSON_GetObjectItem(root, "params");

    if (!cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0 ||
        !cJSON_IsString(method) || !method->valuestring) {
        cJSON_Delete(root);
        return -1;
    }

    if (strncmp(method->valuestring, "notifications/", 14) == 0) {
        cJSON_Delete(root);
        return 0;
    }

    if (strcmp(method->valuestring, "initialize") == 0) {
        result = mcp_handle_initialize();
        mcp_reply_jsonrpc(id, result, NULL);
    } else if (strcmp(method->valuestring, "tools/list") == 0) {
        result = mcp_handle_tools_list();
        mcp_reply_jsonrpc(id, result, NULL);
    } else if (strcmp(method->valuestring, "tools/call") == 0) {
        result = mcp_handle_tools_call(params);
        mcp_reply_jsonrpc(id, result, NULL);
    } else {
        mcp_reply_jsonrpc(id, NULL, "unknown method");
    }

    cJSON_Delete(root);
    return 0;
}
