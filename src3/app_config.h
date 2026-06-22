#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "build_config.h"

#if defined(__has_include)
#if __has_include("app_config_local.h")
#include "app_config_local.h"
#endif
#endif

/*
 * Gen3 Phase 1 compile-time config.
 * Scope:
 * - boot
 * - WiFi
 * - NERTC session
 * - audio capture/playback
 * - OTA
 * - key control
 *
 * Out of scope in this phase:
 * - UI / video / Bluetooth business logic
 */

// === 网络 ===
#define CONFIG_WIFI_ENABLE
#define CONFIG_NET_ENABLE

// WiFi STA 配置
#define APP_WIFI_SSID                "xxx-ssid-xxx"
#define APP_WIFI_PASSWORD            "xxx-passwdd-xx"

#define APP_WIFI_START_DELAY_TICKS   200
#define APP_WIFI_READY_DELAY_TICKS   200

// === NERTC ===
#define CONFIG_NERTC_ENABLE
#define CONFIG_CONNECTION_TYPE_NERTC
#define CONFIG_NERTC_MCP_ENABLE
#define CONFIG_NERTC_MUSIC_PLAYER

// NERTC 云端配置
#define APP_KEY                      "xxxxxxxxxx=appkey-xxxxxxxx"  // 云信appkey
#define APP_CNAME                    "nertc-jl-demo"
#define APP_UID                      6669
#define APP_DEFAULT_DEVICE_ID        "00:11:22:33:44:55"           // 换成真实的mac地址

// === 音频 ===
#define CONFIG_AUDIO_ENABLE
#define CONFIG_AUDIO_DEC_PLAY_SOURCE        "dac"
#define CONFIG_PCM_DEC_ENABLE
#define CONFIG_WAV_DEC_ENABLE
#define CONFIG_MP3_DEC_ENABLE
#define CONFIG_M4A_DEC_ENABLE
#define CONFIG_OPUS_ENC_ENABLE
#define CONFIG_OPUS_DEC_ENABLE
#define CONFIG_AEC_ENC_ENABLE
#define CONFIG_DNS_ENC_ENABLE

#define NERTC_OTA_APP_NAME           "jieli"
#define NERTC_OTA_APP_VERSION        "1.0.0"
#define NERTC_OTA_BOARD_NAME         "AC7911BB"
#ifndef NERTC_OTA_HOST
#define NERTC_OTA_HOST               "nrtc.netease.im"
#endif
#define NERTC_OTA_TIMEOUT_MS         10000

// === 板级 ===
// CONFIG_BOARD_7911BA 和 CONFIG_BOARD_DEVELOP 在 board_config.h 中定义

// === 工程特性 ===
#define CONFIG_RELEASE_ENABLE
#define CONFIG_FREE_RTOS_ENABLE

// === 存储路径（SDK system/update 模块编译必需） ===
#define CONFIG_STORAGE_PATH            "storage/sd0"
#define SDX_DEV                        "sd0"
#define CONFIG_ROOT_PATH               CONFIG_STORAGE_PATH"/C/"
#define CONFIG_UPGRADE_PATH            CONFIG_ROOT_PATH"upgrade"

// === 调试配置（log_config 编译必需） ===
#if !defined CONFIG_DEBUG_ENABLE || defined CONFIG_LIB_DEBUG_DISABLE
#define LIB_DEBUG    0
#else
#define LIB_DEBUG    1
#endif
#define CONFIG_DEBUG_LIB(x)         (x & LIB_DEBUG)

// === 低功耗配置（board_7911B.c 编译必需） ===
#define TCFG_LOWPOWER_LOWPOWER_SEL           0
#define TCFG_LOWPOWER_BTOSC_DISABLE          0
#define TCFG_LOWPOWER_VDDIOM_LEVEL           VDDIOM_VOL_32V
#define TCFG_LOWPOWER_VDDIOW_LEVEL           VDDIOW_VOL_21V
#define VDC14_VOL_SEL_LEVEL                  VDC14_VOL_SEL_140V
#define SYSVDD_VOL_SEL_LEVEL                 SYSVDD_VOL_SEL_126V

#endif // APP_CONFIG_H
