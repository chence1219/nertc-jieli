# AI 实时互动 Example（杰理 AC7911）

网易云信 AI 实时互动演示 Demo，适用于杰理 AC7911 系列开发板。

## 介绍

本仓库展示 AC7911 开发板接入 NERTC AI 实时互动能力的最小可运行方案，当前版本已经包含：

- 固定 WiFi STA 直连
- OTA 配置查询
- MQTT 外部注入
- NERTC Lite Mode 进房与 AI 音频通道控制
- 本地音频采集、Opus 上行、AI 下行播放
- 基于 `on_ai_data` 的歌曲列表与在线音乐播放

当前第三代主线代码统一收口在 `src3/`，`fw-AC79_AIoT_SDK/` 作为只读子模块使用。

### 功能说明

- AI 对话：支持设备侧接入 AI 实时语音对话链路。
- MCP：支持通过 MCP tool 调用设备能力，例如音量调节。
- 网易云音乐播放：支持基于 AI 返回的歌单进行在线音乐播放；该能力需要联系商务人员单独开通权限。

## 源码说明

### 源码结构

```text
├── board
│   └── wl82
│       ├── Makefile                  // 板级构建入口
│       └── board_7911B.c             // 板级配置与按键映射
├── fw-AC79_AIoT_SDK                  // 杰理 SDK 子模块（只读）
├── src3
│   ├── app_main.c                    // 应用入口
│   ├── app_nertc_call.c              // 主状态机与按键/网络事件处理
│   ├── wifi_app_task.c               // WiFi STA 启动与事件转发
│   ├── nertc_ota.c                   // OTA HTTP 配置查询
│   ├── nertc_mqtt_ext.c              // MQTT 外部注入层
│   ├── nertc_protocol.c              // NERTC 协议封装
│   ├── audio_input.c                 // 本地音频录音/播放主实现
│   ├── audio_io.c                    // 音频流启停与上下行适配
│   ├── music_player.c                // 在线音乐播放与歌单集成
│   ├── app_config.h                  // 主配置
│   ├── app_config_local.h.example    // 本地配置样例
│   └── nertc_sdk
│       ├── include                   // NERTC SDK 头文件
│       └── lib/ac7911/libnertc_sdk.a // NERTC SDK 静态库
├── .vscode
│   ├── tasks.json                    // VSCode 构建任务
│   └── winmk.bat                     // Windows 构建脚本
├── Makefile                          // 顶层构建入口
└── README.md
```

## 环境要求

- 已完成杰理 AC7911 开发环境搭建，包括工具链与 SDK 子模块。
- 已具备可用的云信 `AppKey`，并完成目标设备的云端配置。
- 可正常烧录固件并查看串口日志。

## 配置步骤（推荐流程）

1. 准备仓库与子模块。
   - 拉取当前仓库。
   - 初始化 `fw-AC79_AIoT_SDK/` 子模块。
   - `git submodule update --init --recursive`
2. 创建本地配置文件。
   - 复制 `src3/app_config_local.h.example` 为 `src3/app_config_local.h`。
   - 该文件已被 `.gitignore` 忽略，不会进入版本库。
3. 填写 WiFi 信息。
   - 编辑 `src3/app_config_local.h`：
   - `APP_WIFI_SSID`
   - `APP_WIFI_PASSWORD`
4. 填写云端接入参数。
   - 编辑 `src3/app_config.h`：
   - `APP_KEY`
   - `APP_DEFAULT_DEVICE_ID`

## 编译与运行

### VSCode

执行任务：

- `ac791n_nertc_demo`

该任务会调用 `.vscode/winmk.bat` 执行完整构建，并继续执行板级 post-build 下载脚本。

### 命令行

```powershell
.vscode\winmk.bat ac791n_nertc_demo
```

清理构建产物：

```powershell
.vscode\winmk.bat clean_ac791n_nertc_demo
```

说明：

- 构建通过后如果出现 `Device Offline`，通常是下载阶段未连板，不代表编译失败。

## 关键配置清单

- WiFi 本地覆盖：`src3/app_config_local.h`
- 主配置：`src3/app_config.h`
- WiFi 启动与事件：`src3/wifi_app_task.c`
- 应用状态机：`src3/app_nertc_call.c`
- OTA 查询：`src3/nertc_ota.c`
- MQTT 外部注入：`src3/nertc_mqtt_ext.c`
- NERTC 协议层：`src3/nertc_protocol.c`
- 本地音频采集/播放：`src3/audio_input.c`
- 音频流上下行适配：`src3/audio_io.c`
- 在线音乐播放：`src3/music_player.c`
- NERTC SDK 静态库：`src3/nertc_sdk/lib/ac7911/libnertc_sdk.a`

## 按键说明

- `KEY_ENC`
  - 在 `IDLE` 状态下开启 AI 对话
  - 在 `LISTENING` 状态下关闭 AI 音频通道
  - 在 `SPEAKING` 状态下触发打断
- `KEY_POWER`
  - 在播放音乐时停止音乐，并恢复 AI 音频通道

当前板级按键映射定义位于：

- `board/wl82/board_7911B.c`

## 注意事项

- `APP_KEY`、设备标识、真实 WiFi 凭证都属于敏感配置，公开仓库中不建议提交真实值。
- 仓库默认不会跟踪 `src3/app_config_local.h`，请仅在本地保存该文件。
- 如果 `APP_WIFI_SSID` 或 `APP_WIFI_PASSWORD` 为空，程序会跳过 WiFi 启动与重连。
- `fw-AC79_AIoT_SDK/` 是 git 子模块，不应在当前项目需求下直接修改。
- 当前仓库的设计目标是逐步收敛和精简，不建议再把第二代大状态机或额外业务模块整块搬回主线。
