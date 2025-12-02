#ifndef TLV_META_TABLE_H
#define TLV_META_TABLE_H
 
#include "tlv_types.h"
 
#ifdef __cplusplus
extern "C" {
#endif
 
/* ============================ Tag定义 ============================ */
// 系统配置区 0x1000-0x1FFF
#define TAG_SYSTEM_CONFIG           0x1001
#define TAG_SYSTEM_CALIBRATION      0x1002
#define TAG_SYSTEM_SERIAL_NUMBER    0x1003
#define TAG_SYSTEM_MAC_ADDRESS      0x1004
#define TAG_SYSTEM_BOOT_COUNT       0x1005
 
// 传感器配置区 0x2000-0x2FFF
#define TAG_SENSOR_CALIB_TEMP       0x2001
#define TAG_SENSOR_CALIB_PRESSURE   0x2002
#define TAG_SENSOR_CALIB_HUMIDITY   0x2003
#define TAG_SENSOR_OFFSET_X         0x2004
#define TAG_SENSOR_OFFSET_Y         0x2005
#define TAG_SENSOR_OFFSET_Z         0x2006
 
// 网络配置区 0x3000-0x3FFF
#define TAG_NET_IP_ADDRESS          0x3001
#define TAG_NET_SUBNET_MASK         0x3002
#define TAG_NET_GATEWAY             0x3003
#define TAG_NET_DNS_SERVER          0x3004
#define TAG_NET_WIFI_SSID           0x3005
#define TAG_NET_WIFI_PASSWORD       0x3006
 
// 用户数据区 0x4000-0x4FFF
#define TAG_USER_PROFILE            0x4001
#define TAG_USER_SETTINGS           0x4002
#define TAG_USER_PREFERENCES        0x4003
#define TAG_USER_HISTORY            0x4004
 
/* ============================ 外部声明 ============================ */
extern const tlv_meta_const_t TLV_META_MAP[];  // 只声明，不定义
 
/* ============================ 辅助函数 ============================ */
const char* tlv_get_tag_name(uint16_t tag);
uint16_t tlv_get_tag_max_length(uint16_t tag);
 
#ifdef __cplusplus
}
#endif
 
#endif /* TLV_META_TABLE_H */
