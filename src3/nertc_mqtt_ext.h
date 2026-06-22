#ifndef __NERTC_MQTT_EXT_H__
#define __NERTC_MQTT_EXT_H__

#include "nertc_sdk_ext_net.h"

nertc_sdk_ext_net_handle_t *nertc_mqtt_ext_get_handle(void);
void nertc_mqtt_ext_init(void);
void nertc_mqtt_ext_deinit(void);

#endif
