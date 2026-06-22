#ifndef SRC3_MCP_SERVER_H
#define SRC3_MCP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

void mcp_server_init(void);
void mcp_server_set_send_func(void (*send_func)(const char *payload, int len));
int mcp_server_parse_message(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif
