/**
 * @file nertc_log.h
 * @brief 统一日志宏
 *
 * 每个模块在包含本头文件前定义 TAG，例如:
 *   #define TAG "OTA"
 *   #include "nertc_log.h"
 */

#ifndef NERTC_LOG_H
#define NERTC_LOG_H

#ifndef TAG
#define TAG "NERTC"
#endif

#define NERTC_LOGI(fmt, ...) printf("[%s] " fmt "\n", TAG, ##__VA_ARGS__)
#define NERTC_LOGE(fmt, ...) printf("[%s][E] " fmt "\n", TAG, ##__VA_ARGS__)
#define NERTC_LOGW(fmt, ...) printf("[%s][W] " fmt "\n", TAG, ##__VA_ARGS__)

#endif /* NERTC_LOG_H */
