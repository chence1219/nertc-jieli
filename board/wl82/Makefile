# NERTC-JL-Demo Board Makefile (board/wl82)
#
# 模仿 Lingxi toytale/src/platform/board/wl82/Makefile 结构
# 适配: fw-AC79_AIoT_SDK (git submodule) + src/ (应用平级)
#
# make             编译并下载
# make VERBOSE=1   显示编译详细过程
# make clean       清除编译临时文件

# ============================================================================
# 工具路径设置
# ============================================================================
ifeq ($(OS), Windows_NT)
TOOL_DIR := C:/JL/pi32/bin
CC    := clang.exe
CXX   := clang.exe
LD    := pi32v2-lto-wrapper.exe
AR    := llvm-ar.exe
MKDIR := mkdir_win -p
RM    := rm -rf
SYS_LIB_DIR :=
SYS_INC_DIR :=
EXT_CFLAGS  :=
export PATH:=$(TOOL_DIR);$(PATH)
FIXBAT          := ../../fw-AC79_AIoT_SDK/tools/utils/fixbat.exe
POST_SCRIPT_EXT := bat
POST_TOOL_DIR_WIN := ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools
else
TOOL_DIR := /opt/jieli/pi32v2/bin
CC    := clang
CXX   := clang
LD    := lto-wrapper
AR    := lto-ar
MKDIR := mkdir -p
RM    := rm -rf
export OBJDUMP := $(TOOL_DIR)/objdump
export OBJCOPY := $(TOOL_DIR)/objcopy
export OBJSIZEDUMP := $(TOOL_DIR)/objsizedump
SYS_LIB_DIR := $(TOOL_DIR)/../lib/r3
SYS_INC_DIR := $(TOOL_DIR)/../include
EXT_CFLAGS  := -D__SHELL__
export PATH:=$(TOOL_DIR):$(PATH)
FIXBAT          := touch
POST_SCRIPT_EXT := sh
endif

CC  := $(TOOL_DIR)/$(CC)
CXX := $(TOOL_DIR)/$(CXX)
LD  := $(TOOL_DIR)/$(LD)
AR  := $(TOOL_DIR)/$(AR)

# ============================================================================
# 输出文件设置
# ============================================================================
OUT_ELF   := ../../output/sdk.elf
OBJ_FILE  := $(OUT_ELF).objs.txt
BUILD_DIR := objs
ROOT_PREFIX := ../..
GEN_DIR := ../../output/gen
SDK_USED_LIST := $(GEN_DIR)/sdk_used_list.used
MOVABLE_SECTION_MAP := $(GEN_DIR)/section.txt
LINKER_SCRIPT := $(GEN_DIR)/sdk.ld
POST_SCRIPT := $(GEN_DIR)/download.$(POST_SCRIPT_EXT)
ISD_CONFIG := $(GEN_DIR)/isd_config.ini
ISD_CONFIG_LOADER := $(GEN_DIR)/isd_config_loader.ini

ifeq ($(OS), Windows_NT)
RUN_POST_SCRIPT := ..\..\output\gen\download.bat
RUN_POST_BUILD := cmd /c "set PATH=$(POST_TOOL_DIR_WIN);%PATH% && call $(RUN_POST_SCRIPT) sdk"
else
RUN_POST_SCRIPT := bash $(POST_SCRIPT)
RUN_POST_BUILD := $(RUN_POST_SCRIPT) sdk
endif

# ============================================================================
# 编译参数设置
# ============================================================================
CFLAGS := \
	-target pi32v2 \
	-integrated-as \
	-mcpu=r3 \
	-mfprev1 \
	-Wuninitialized \
	-Wno-invalid-noreturn \
	-fno-common \
	-Oz \
	-g \
	-flto \
	-fallow-pointer-null \
	-fprefer-gnu-section \
	-femulated-tls \
	-Wno-shift-negative-value \
	-Wframe-larger-than=2560 \
	-mllvm -pi32v2-large-program=true \
	-fms-extensions \
	-w \
	-fno-unwind-tables \
	-ffunction-sections \
	-fdata-sections \
	-fmessage-length=0 \

CXXFLAGS := \
	-std=gnu++14 \
	-fno-exceptions \
	-fno-rtti \

# ============================================================================
# 宏定义
# ============================================================================
DEFINES := \
	-DSUPPORT_MS_EXTENSIONS \
	-DCONFIG_RELEASE_ENABLE \
	-DCONFIG_CPU_WL82 \
	-DCONFIG_FREE_RTOS_ENABLE \
	-DCONFIG_THREAD_ENABLE \
	-DCONFIG_AUDIO_ONCHIP \
	-D__GCC_PI32V2__ \
	-DCONFIG_NEW_ECC_ENABLE \
	-DCONFIG_EQ_SUPPORT_ASYNC \
	-DEQ_CORE_V1 \
	-DCONFIG_AUDIO_ENABLE \
	-DCONFIG_JLFAT_ENABLE \
	-DCONFIG_RTOS_AND_MM_LIB_CODE_SECTION_IN_SDRAM \
	-DCONFIG_NET_ENABLE=1 \
	-DEVENT_HANDLER_NUM_CONFIG=2 \
	-DEVENT_TOUCH_ENABLE_CONFIG=0 \
	-DEVENT_POOL_SIZE_CONFIG=256 \
	-DCONFIG_EVENT_KEY_MAP_ENABLE=0 \
	-DTIMER_POOL_NUM_CONFIG=15 \
	-DAPP_ASYNC_POOL_NUM_CONFIG=0 \
	-DVFS_ENABLE=1 \
	-DUSE_SDFILE_NEW=1 \
	-DSDFILE_STORAGE=1 \
	-DVFS_FILE_POOL_NUM_CONFIG=8 \
	-DVM_MAX_PAGE_ALIGN_SIZE_CONFIG=64*1024 \
	-DVM_MAX_SECTOR_ALIGN_SIZE_CONFIG=64*1024 \
	-DVM_ITEM_MAX_NUM=256 \
	-DCONFIG_ITEM_FORMAT_VM \
	-DJIELI_RTOS \
	-DJIELI_791N_RTOS \
	-D_XOPEN_SOURCE=700 \
	-D_GNU_SOURCE \
	-D__ELF__ \

DEFINES += $(EXT_CFLAGS)

# ============================================================================
# 头文件搜索路径
# ============================================================================
INCLUDES := \
	-I../../fw-AC79_AIoT_SDK/include_lib/c++ \
	-I../../fw-AC79_AIoT_SDK/include_lib/c++/include \
	-I../../fw-AC79_AIoT_SDK/include_lib/newlib/include \
	-I../../fw-AC79_AIoT_SDK/include_lib/c++/simple_pthread \
	-I../../fw-AC79_AIoT_SDK/include_lib \
	-I../../fw-AC79_AIoT_SDK/include_lib/driver \
	-I../../fw-AC79_AIoT_SDK/include_lib/driver/device \
	-I../../fw-AC79_AIoT_SDK/include_lib/driver/cpu/wl82 \
	-I../../fw-AC79_AIoT_SDK/include_lib/system \
	-I../../fw-AC79_AIoT_SDK/include_lib/system/generic \
	-I../../fw-AC79_AIoT_SDK/include_lib/btctrler \
	-I../../fw-AC79_AIoT_SDK/include_lib/btctrler/port/wl82 \
	-I../../fw-AC79_AIoT_SDK/include_lib/btstack \
	-I../../fw-AC79_AIoT_SDK/include_lib/btstack/third_party/common \
	-I../../fw-AC79_AIoT_SDK/include_lib/btstack/third_party/rcsp \
	-I../../fw-AC79_AIoT_SDK/include_lib/utils \
	-I../../fw-AC79_AIoT_SDK/include_lib/utils/syscfg \
	-I../../fw-AC79_AIoT_SDK/include_lib/utils/event \
	-I../../fw-AC79_AIoT_SDK/include_lib/media \
	-I../../fw-AC79_AIoT_SDK/include_lib/media/cpu/wl82 \
	-I../../fw-AC79_AIoT_SDK/apps \
	-I../../fw-AC79_AIoT_SDK/apps/common \
	-I../../fw-AC79_AIoT_SDK/apps/common/include \
	-I../../fw-AC79_AIoT_SDK/apps/common/config/include \
	-I../../fw-AC79_AIoT_SDK/apps/common/system \
	-I../../fw-AC79_AIoT_SDK/include_lib/net \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/mbedtls_3_4_0 \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/server \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/wifi_module \
	-I../../fw-AC79_AIoT_SDK/include_lib/utils/img_stitch \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/hostapdandwpa_supplicant \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/lwip_2_2_0/lwip/src/include \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/lwip_2_2_0/lwip/src/include/lwip \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/lwip_2_2_0/lwip/port \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/lwip_2_2_0/lwip/app \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/lwip_2_2_0/lwip/app/ctp \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/lwip_2_2_0/lwip/app/cdp \
	-I../../fw-AC79_AIoT_SDK/apps/common/example/third_party/littlefs \
	-I../../fw-AC79_AIoT_SDK/apps/common/net \
	-I../../fw-AC79_AIoT_SDK/apps/common/LLM/audio \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/cJSON_common \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/curl/include \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/lwip_2_2_0/lwip/src/include/compat/posix \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/lwip_2_2_0/lwip/src/include/compat/stdc \
	-I../../fw-AC79_AIoT_SDK/include_lib/system/os \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/libwebsockets/include \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/libwebsockets/include/libwebsockets \
	-I../../fw-AC79_AIoT_SDK/include_lib/net/web_socket \
	-I../../src3 \
	-I../../src3/nertc_sdk/include \
	-I../../board/wl82 \

# ============================================================================
# 需要编译的 .c 文件（SDK 公共文件 + 应用文件）
# ============================================================================
c_SRC_FILES := \
	../../fw-AC79_AIoT_SDK/apps/common/audio_music/audio_config.c \
	../../fw-AC79_AIoT_SDK/apps/common/config/log_config/app_config.c \
	../../fw-AC79_AIoT_SDK/apps/common/config/log_config/lib_driver_config.c \
	../../fw-AC79_AIoT_SDK/apps/common/config/log_config/lib_system_config.c \
	../../fw-AC79_AIoT_SDK/apps/common/config/new_cfg_tool.c \
	../../fw-AC79_AIoT_SDK/apps/common/config/user_cfg.c \
	../../fw-AC79_AIoT_SDK/apps/common/debug/debug.c \
	../../fw-AC79_AIoT_SDK/apps/common/debug/debug_user.c \
	../../fw-AC79_AIoT_SDK/apps/common/example/third_party/littlefs/lfs.c \
	../../fw-AC79_AIoT_SDK/apps/common/example/third_party/littlefs/lfs_util.c \
	../../fw-AC79_AIoT_SDK/apps/common/example/third_party/littlefs/port/wl82/port.c \
	../../fw-AC79_AIoT_SDK/apps/common/fat_nor/extflash.c \
	../../fw-AC79_AIoT_SDK/apps/common/iic/iic.c \
	../../fw-AC79_AIoT_SDK/apps/common/iic/software_iic.c \
	../../fw-AC79_AIoT_SDK/apps/common/jl_math/flfft_core_pi32v2.c \
	../../fw-AC79_AIoT_SDK/apps/common/jl_math/jl_fft.c \
	../../fw-AC79_AIoT_SDK/apps/common/jl_math/kiss_fft.c \
	../../fw-AC79_AIoT_SDK/apps/common/jl_math/jl_math.c \
	../../fw-AC79_AIoT_SDK/apps/common/jl_math/nn_function_vec.c \
	../../fw-AC79_AIoT_SDK/apps/common/key/adkey.c \
	../../fw-AC79_AIoT_SDK/apps/common/key/adkey_rtcvdd.c \
	../../fw-AC79_AIoT_SDK/apps/common/key/ctmu_touch_key.c \
	../../fw-AC79_AIoT_SDK/apps/common/key/iokey.c \
	../../fw-AC79_AIoT_SDK/apps/common/key/irkey.c \
	../../fw-AC79_AIoT_SDK/apps/common/key/key_driver.c \
	../../fw-AC79_AIoT_SDK/apps/common/key/touch_key.c \
	../../fw-AC79_AIoT_SDK/apps/common/net/assign_macaddr.c \
	../../fw-AC79_AIoT_SDK/apps/common/net/platform_cfg.c \
	../../fw-AC79_AIoT_SDK/apps/common/net/wifi_conf.c \
	../../fw-AC79_AIoT_SDK/apps/common/system/device_mount.c \
	../../fw-AC79_AIoT_SDK/apps/common/system/flash_write_protect.c \
	../../fw-AC79_AIoT_SDK/apps/common/system/init.c \
	../../fw-AC79_AIoT_SDK/apps/common/system/power_manage.c \
	../../fw-AC79_AIoT_SDK/apps/common/system/system_reset_reason.c \
	../../fw-AC79_AIoT_SDK/apps/common/system/system_vdd_clock.c \
	../../fw-AC79_AIoT_SDK/apps/common/system/version.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/debug.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/iic.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/key/adc_api.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/key/ctmu.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/key/irflt.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/key/plcnt.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/key/rdec.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/port_waked_up.c \
	../../fw-AC79_AIoT_SDK/cpu/wl82/setup.c \
	../../src3/audio_input.c \
	../../src3/app_main.c \
	../../src3/app_nertc_call.c \
	../../src3/mcp_server.c \
	../../src3/music_player.c \
	../../src3/nertc_protocol.c \
	../../src3/nertc_mqtt_ext.c \
	../../src3/nertc_ota.c \
	../../src3/audio_io.c \
	../../src3/link_compat.c \
	../../src3/wifi_app_task.c \
	../../board/wl82/board_7911B.c \

# ============================================================================
# 需要编译的 .S 文件
# ============================================================================
S_SRC_FILES := \
	../../fw-AC79_AIoT_SDK/include_lib/driver/version.z.S \
	../../fw-AC79_AIoT_SDK/include_lib/media/version.z.S \
	../../fw-AC79_AIoT_SDK/include_lib/net/version.z.S \
	../../fw-AC79_AIoT_SDK/include_lib/server/version.z.S \
	../../fw-AC79_AIoT_SDK/include_lib/system/version.z.S \
	../../fw-AC79_AIoT_SDK/include_lib/utils/version.z.S \
	../../fw-AC79_AIoT_SDK/lib/net/version.z.S \
	../../fw-AC79_AIoT_SDK/lib/server/version.z.S \
	../../fw-AC79_AIoT_SDK/lib/utils/version.z.S \

s_SRC_FILES :=

cpp_SRC_FILES := \
	../../fw-AC79_AIoT_SDK/apps/common/c++/cxx_runtime.cpp \

NERTC_SDK_LIB := ../../src3/nertc_sdk/lib/ac7911/libnertc_sdk.a

cc_SRC_FILES :=
cxx_SRC_FILES :=

# ============================================================================
# 链接参数
# ============================================================================
LFLAGS := \
	--plugin-opt=-pi32v2-always-use-itblock=false \
	--plugin-opt=-enable-ipra=true \
	--plugin-opt=-pi32v2-merge-max-offset=4096 \
	--plugin-opt=-pi32v2-enable-simd=true \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-global-merge-on-const \
	--plugin-opt=-inline-threshold=5 \
	--plugin-opt=-inline-max-allocated-size=32 \
	--plugin-opt=-inline-normal-into-special-section=true \
	--plugin-opt=-dont-used-symbol-list=malloc,free,sprintf,printf,puts,putchar \
	--plugin-opt=save-temps \
	--plugin-opt=-pi32v2-enable-rep-memop \
	--plugin-opt=-emulated-tls \
	--sort-common \
	--plugin-opt=-used-symbol-file=$(SDK_USED_LIST) \
	--plugin-opt=-enable-movable-region=true \
	--plugin-opt=-movable-region-section-prefix=.movable.slot. \
	--plugin-opt=-movable-region-stub-section-prefix=.movable.stub. \
	--plugin-opt=-movable-region-prefix=.movable.region. \
	--plugin-opt=-movable-region-stub-prefix=__movable_stub_ \
	--plugin-opt=-movable-region-stub-swi-number=-2 \
	--plugin-opt=-movable-region-map-file-list=../../fw-AC79_AIoT_SDK/apps/common/movable/funcname.txt \
	--plugin-opt=-movable-region-section-map-file-list=$(MOVABLE_SECTION_MAP) \
	--plugin-opt=-movable-region-exclude-func-file-list=../../fw-AC79_AIoT_SDK/apps/common/movable/exclude.txt \
	--plugin-opt=-pi32v2-large-program=true \
	--start-group \
	../../fw-AC79_AIoT_SDK/include_lib/newlib/pi32v2-lib/libm.a \
	../../fw-AC79_AIoT_SDK/include_lib/newlib/pi32v2-lib/libc.a \
	../../fw-AC79_AIoT_SDK/include_lib/newlib/pi32v2-lib/libcompiler_rt.a \
	../../fw-AC79_AIoT_SDK/include_lib/c++/libstdc++/libc++abi.a \
	../../fw-AC79_AIoT_SDK/include_lib/c++/libstdc++/libc++.a \
	--start-group \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/event.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/system.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/fs.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/cfg_tool.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/cpu.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/common_lib.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/wl_rf_common.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/VirtualBass.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_esco_repair.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/audio_server.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/echo_server.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_opus_enc.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_opus_stenc.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libspeex.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libvad.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_mp3_dec.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_mp3_enc.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_wav_dec.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_opus_dec.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_adpcm_enc.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_pitchshifter.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_rmono2stereo.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_reverb_cal.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_howling.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_pitch_speed.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libaec.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libSMS_TDE_pi32v2_OnChip.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libFFT_pi32v2_OnChip.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_dvad.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libdns.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libjlsp.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libjlsp_kws_far_keyword.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libkwscommon.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/media_app.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libcompressor.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_crossover_coff.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libdrc.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/net_server.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lwip_2_2_0.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/wl_wifi_sta.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lte_module.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/ai_server.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/zliblite.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/stream_media_server.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libmbedtls_3_4_0.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/wpasupplicant.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/hsm.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/http_cli.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/https_cli.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/http_server.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/json.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libmpeg.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_m4a_dec.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/network_download.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_mqtt.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/websocket.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/cJSON.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/lib_ccm_aes.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/crypto_toolbox_Osize.a \
	--end-group \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libcurl.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libwebsockets.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/cJSON.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/libmbedtls_3_4_0.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/websocket.a \
	../../fw-AC79_AIoT_SDK/cpu/wl82/liba/cJSON.a \
	$(NERTC_SDK_LIB) \
	-T$(LINKER_SCRIPT) \
	-M=../../output/sdk.map \
	--end-group \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-mattr=+fprev1 \

# ============================================================================
# 对象文件处理
# ============================================================================
c_OBJS    := $(c_SRC_FILES:%.c=%.c.o)
S_OBJS    := $(S_SRC_FILES:%.S=%.S.o)
s_OBJS    := $(s_SRC_FILES:%.s=%.s.o)
cpp_OBJS  := $(cpp_SRC_FILES:%.cpp=%.cpp.o)
cxx_OBJS  := $(cxx_SRC_FILES:%.cxx=%.cxx.o)
cc_OBJS   := $(cc_SRC_FILES:%.cc=%.cc.o)

OBJS      := $(c_OBJS) $(S_OBJS) $(s_OBJS) $(cpp_OBJS) $(cxx_OBJS) $(cc_OBJS)
DEP_FILES := $(OBJS:%.o=%.d)

OBJS      := $(addprefix $(BUILD_DIR)/, $(OBJS:$(ROOT_PREFIX)/%=%))
DEP_FILES := $(addprefix $(BUILD_DIR)/, $(DEP_FILES:$(ROOT_PREFIX)/%=%))

# ============================================================================
# 构建控制
# ============================================================================
VERBOSE ?= 0
ifeq ($(VERBOSE), 1)
QUITE :=
else
QUITE := @
endif

LINK_AT ?= 1

.PHONY: all clean pre_build
.SUFFIXES:

all: $(OUT_ELF)
	$(info +POST-BUILD)
	$(QUITE) $(FIXBAT) $(POST_SCRIPT)
	$(QUITE) copy /Y $(subst /,\,$(OUT_ELF)) $(subst /,\,$(dir $(POST_SCRIPT))sdk.elf) > nul 2>&1 || true
	$(QUITE) copy /Y ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools\uboot.boot ..\..\output\gen\uboot.boot > nul 2>&1 || true
	$(QUITE) copy /Y ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools\ota.bin ..\..\output\gen\ota.bin > nul 2>&1 || true
	$(QUITE) copy /Y ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools\script.ver ..\..\output\gen\script.ver > nul 2>&1 || true
	$(QUITE) copy /Y ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools\jl_isd.fw ..\..\output\gen\jl_isd.fw > nul 2>&1 || true
	$(QUITE) copy /Y ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools\cfg_tool.bin ..\..\output\gen\cfg_tool.bin > nul 2>&1 || true
	$(QUITE) copy /Y ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools\wl82loader.bin ..\..\output\gen\wl82loader.bin > nul 2>&1 || true
	$(QUITE) xcopy /E /I /Y ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools\audlogo ..\..\output\gen\audlogo > nul 2>&1 || true
	$(QUITE) xcopy /E /I /Y ..\..\fw-AC79_AIoT_SDK\cpu\wl82\tools\cfg ..\..\output\gen\cfg > nul 2>&1 || true
	$(QUITE) $(RUN_POST_BUILD)

pre_build:
	$(info +PRE-BUILD)
	$(QUITE) $(MKDIR) ../../output
	$(QUITE) $(MKDIR) $(GEN_DIR)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../fw-AC79_AIoT_SDK/cpu/wl82/sdk_used_list.c -o $(SDK_USED_LIST)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../fw-AC79_AIoT_SDK/apps/common/movable/section.c -o $(MOVABLE_SECTION_MAP)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../fw-AC79_AIoT_SDK/cpu/wl82/sdk_ld.c -o $(LINKER_SCRIPT)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../fw-AC79_AIoT_SDK/cpu/wl82/tools/download.c -o $(POST_SCRIPT)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../fw-AC79_AIoT_SDK/cpu/wl82/tools/isd_config_rule.c -o $(ISD_CONFIG)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../fw-AC79_AIoT_SDK/cpu/wl82/tools/isd_config_rule_loader.c -o $(ISD_CONFIG_LOADER)
	$(QUITE) $(FIXBAT) $(POST_SCRIPT)

clean:
	$(QUITE) $(RM) $(OUT_ELF)
	$(QUITE) $(RM) $(BUILD_DIR)
	$(QUITE) $(RM) $(GEN_DIR)

ifeq ($(LINK_AT), 1)
$(OUT_ELF): pre_build $(OBJS) $(NERTC_SDK_LIB)
	$(info +LINK $@)
	$(shell $(MKDIR) $(@D))
	$(file >$(OBJ_FILE), $(OBJS))
	$(QUITE) $(LD) -o $(OUT_ELF) @$(OBJ_FILE) $(LFLAGS)
else
$(OUT_ELF): pre_build $(OBJS) $(NERTC_SDK_LIB)
	$(info +LINK $@)
	$(shell $(MKDIR) $(@D))
	$(QUITE) $(LD) -o $(OUT_ELF) $(OBJS) $(LFLAGS)
endif

$(BUILD_DIR)/%.c.o : $(ROOT_PREFIX)/%.c
	$(info +CC $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.S.o : $(ROOT_PREFIX)/%.S
	$(info +AS $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.s.o : $(ROOT_PREFIX)/%.s
	$(info +AS $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.cpp.o : $(ROOT_PREFIX)/%.cpp
	$(info +CXX $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.cxx.o : $(ROOT_PREFIX)/%.cxx
	$(info +CXX $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/%.cc.o : $(ROOT_PREFIX)/%.cc
	$(info +CXX $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MF $(@:.o=.d) -c $< -o $@

-include $(DEP_FILES)
