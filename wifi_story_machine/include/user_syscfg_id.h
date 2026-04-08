// ... existing code ...
#ifndef USER_SYSCFG_ID_H
#define USER_SYSCFG_ID_H

// 用户自定义配置项ID，使用 syscfg 用户区间 [1 ~ 59]
#define    CFG_USER_PRODUCT_KEY         2
#define    CFG_USER_DEVICE_MAC          3
#define    CFG_USER_AUTH_KEY            4
#define    CFG_USER_DEVICE_ID           5
#define    CFG_USER_DEVICE_SECRET       6
#define    CFG_USER_DEVICE_INFO         7
#define    CFG_USER_CLOSE_POWERON_PROMPT 8       //关闭开机提示音
#define    CFG_USER_CLOSE_NETWORK_PROMPT 9       //关闭网络提示音

#ifndef CFG_USER_DEFINE_END
#define    CFG_USER_DEFINE_END          59
#endif

#endif
// ... existing code ...