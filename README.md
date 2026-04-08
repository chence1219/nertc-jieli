# AI 实时互动 Example（杰理 AC791）

网易云信 · AI 实时互动演示 Demo，适用于杰理 AC791 系列开发板。

## 介绍

本 Demo 演示杰理智能硬件接入 AI 实时互动（语音通话）能力，并提供客户二次集成时的关键改动点与文件参考。

## 源码说明

### 源码结构

```text
├── common
│   ├── nertc_sdk                 // NERTC SDK（头文件 + 静态库）
│   │   ├── include
│   │   └── lib/ac7911/libnertc_sdk.a
│   └── LLM/audio/audio_input.c   // 音频采集/播放适配实现（需替换 SDK 原文件）
├── wifi_story_machine
│   ├── app_main.c                // 应用入口
│   ├── wifi_app_task.c           // WiFi 配置（SSID/PWD）
│   ├── ai_sdk
│   │   ├── nertc_protocol.h      // NERTC 默认参数（AppKey/DeviceID 等）
│   │   └── nertc_protocol.c      // NERTC 协议封装实现
│   └── board/wl82
│       ├── AC791N_WIFI_STORY_MACHINE.cbp  // CodeBlocks 工程
│       └── Makefile                        // 命令行构建入口
└── README.md
```

## 环境要求

- 杰理 AC791 开发环境（工具链 + SDK），请先完成杰理官方环境搭建。
- 已获取 AI 实时互动所需的云信能力开通信息（至少包含 `AppKey`）。
- 可正常烧录并查看串口日志。

## 客户接入步骤（推荐流程）

1. Clone 杰理 AC791 SDK：
   - `https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK`
2. 替换 Demo：
   - 用本仓库的 `wifi_story_machine` 替换 SDK 内同名工程目录（通常是 `FW-AC79_AIoT_SDK/apps/wifi_story_machine`）。
3. 添加 NERTC SDK：
   - 将本仓库 `common/nertc_sdk` 拷贝到 SDK 的 `apps/common` 下（目标路径通常为 `FW-AC79_AIoT_SDK/apps/common/nertc_sdk`）。
4. 替换音频文件：
   - 用本仓库 `common/LLM/audio/audio_input.c` 覆盖 SDK 内同路径文件（通常为 `FW-AC79_AIoT_SDK/apps/common/LLM/audio/audio_input.c`）。
5. 修改 WiFi：
   - 编辑 `wifi_story_machine/wifi_app_task.c`（约第 307 行）：
   - `#define INIT_SSID "你的WiFi名"`
   - `#define INIT_PWD  "你的WiFi密码"`
6. 填写 AppKey：
   - 编辑 `wifi_story_machine/ai_sdk/nertc_protocol.h`（第 23 行）：
   - `#define NERTC_DEFAULT_APP_KEY "你的AppKey"`
7. 智能体平台配置：
   - 在智能体平台创建并配置智能体，绑定设备 `device-id`（对应 `nertc_protocol.h` 中 `NERTC_DEFAULT_DEVICE_ID`）。

## 编译与运行

编译 Demo 工程，参考杰理[官方文档](https://doc.zh-jieli.com/AC79/zh-cn/master/getting_started/project_download/download.html)。

## 关键配置清单

- WiFi：`wifi_story_machine/wifi_app_task.c`
- AppKey / DeviceID：`wifi_story_machine/ai_sdk/nertc_protocol.h`
- NERTC 协议实现：`wifi_story_machine/ai_sdk/nertc_protocol.c`
- 音频采集与播放：`common/LLM/audio/audio_input.c`
- NERTC SDK 静态库：`common/nertc_sdk/lib/ac7911/libnertc_sdk.a`

## 注意事项

- `NERTC_DEFAULT_APP_KEY` 为敏感配置，量产版本建议使用更安全的下发机制，不建议硬编码在固件中。
- `NERTC_DEFAULT_DEVICE_ID` 建议使用设备唯一标识（如 MAC）以便平台侧精确管理。
- 若你的 SDK 目录结构与本文不同，请以“同名模块替换 + 路径对齐”为准。

运行 Demo：`K8` 按钮：开启/关闭/打断 AI 智能体。
