#include "system/includes.h"
#include "app_nertc_call.h"
#include "app_config.h"

const struct irq_info irq_info_table[] = {
#if CPU_CORE_NUM == 1
    { IRQ_SOFT5_IDX,      7,   0    },
    { IRQ_SOFT4_IDX,      7,   1    },
    { -2,                 -2,  -2   },
#endif
    { -1,                 -1,  -1   },
};

#define SYS_TIMER_STK_SIZE  512
#define SYS_TIMER_Q_SIZE    128
static u8 sys_timer_tcb_stk_q[sizeof(StaticTask_t) + SYS_TIMER_STK_SIZE * 4 + sizeof(struct task_queue) + SYS_TIMER_Q_SIZE] ALIGNE(4);

#define SYSTIMER_STK_SIZE   256
static u8 systimer_tcb_stk_q[sizeof(StaticTask_t) + SYSTIMER_STK_SIZE * 4] ALIGNE(4);

#define SYS_EVENT_STK_SIZE  512
static u8 sys_event_tcb_stk_q[sizeof(StaticTask_t) + SYS_EVENT_STK_SIZE * 4] ALIGNE(4);

#define APP_CORE_STK_SIZE   8192
#define APP_CORE_Q_SIZE     1024
static u8 app_core_tcb_stk_q[sizeof(StaticTask_t) + APP_CORE_STK_SIZE * 4 + sizeof(struct task_queue) + APP_CORE_Q_SIZE] ALIGNE(4);

#define WIFI_TASKLET_STK_SIZE 1400
static u8 wifi_tasklet_tcb_stk_q[sizeof(struct thread_parm) + WIFI_TASKLET_STK_SIZE * 4] ALIGNE(4);

#define WIFI_CMDQ_STK_SIZE  300
static u8 wifi_cmdq_tcb_stk_q[sizeof(struct thread_parm) + WIFI_CMDQ_STK_SIZE * 4] ALIGNE(4);

#define WIFI_MLME_STK_SIZE  700
static u8 wifi_mlme_tcb_stk_q[sizeof(struct thread_parm) + WIFI_MLME_STK_SIZE * 4] ALIGNE(4);

#define WIFI_RX_STK_SIZE    256
static u8 wifi_rx_tcb_stk_q[sizeof(struct thread_parm) + WIFI_RX_STK_SIZE * 4] ALIGNE(4);

const struct task_info task_info_table[] = {
    {"thread_fork_kill",    25,      256,   0     },
    {"app_core",            15,     APP_CORE_STK_SIZE,   APP_CORE_Q_SIZE,   app_core_tcb_stk_q },
    {"sys_event",           29,     SYS_EVENT_STK_SIZE,  0,                 sys_event_tcb_stk_q },
    {"systimer",            14,     SYSTIMER_STK_SIZE,   0,                 systimer_tcb_stk_q },
    {"sys_timer",            9,     SYS_TIMER_STK_SIZE,  SYS_TIMER_Q_SIZE,  sys_timer_tcb_stk_q },
    {"audio_server",        16,      512,   64    },
    {"audio_mix",           28,      512,   0     },
    {"audio_encoder",       12,      384,   64    },
    {"speex_encoder",       13,      512,   0     },
    {"opus_encoder",        13,     1536,   0     },
    {"vir_dev_task",        14,      256,   0     },
    {"vad_encoder",         14,      768,   0     },
    {"aec_encoder",         13,     1024,   0     },
    {"dns_encoder",         13,      512,   0     },
    {"echo_deal",           11,     1024,   32    },
#ifdef CONFIG_WIFI_ENABLE
    {"tcpip_thread",        16,      800,   0     },
    {"tasklet",             10,     WIFI_TASKLET_STK_SIZE, 0, wifi_tasklet_tcb_stk_q },
    {"RtmpMlmeTask",        17,     WIFI_MLME_STK_SIZE,    0, wifi_mlme_tcb_stk_q },
    {"RtmpCmdQTask",        17,     WIFI_CMDQ_STK_SIZE,    0, wifi_cmdq_tcb_stk_q },
    {"wl_rx_irq_thread",     7,     WIFI_RX_STK_SIZE,      0, wifi_rx_tcb_stk_q },
#endif
#ifdef CONFIG_BT_ENABLE
    {"btencry",             14,      512,   128   },
    {"btctrler",            19,      512,   384   },
    {"btstack",             18,      768,   384   },
#endif
    {0, 0},
};

void app_default_event_handler(struct sys_event *event)
{
    switch (event->type) {
    case SYS_KEY_EVENT:
    case SYS_NET_EVENT:
    default:
        break;
    }
}

void app_main()
{
    struct intent it;

#ifdef CONFIG_CXX_SUPPORT
    extern void cpp_run_init(void);
    cpp_run_init();
#endif

    printf("------------- nertc call app main-------------\n");

    init_intent(&it);
    it.name = "app_nertc_call";
    it.action = ACTION_NERTC_CALL_MAIN;
    start_app(&it);
}
