/**
 * @file tlv_meta_table.c
 * @brief TLV元数据表实现
 */
 
#include "tlv_meta_table.h"
#include <string.h>
 
/* ============================ 元数据表定义 ============================ */
 
const tlv_meta_const_t TLV_META_MAP[] = 
{
    // Tag                      MaxLen  Prior Ver  Bkup  Name
    {TAG_SYSTEM_CONFIG,         64,     10,   1,   1,   "SystemConfig",     NULL},
    {TAG_SYSTEM_CALIBRATION,    128,    10,   1,   1,   "SystemCalibration",     NULL},
    {TAG_SYSTEM_SERIAL_NUMBER,  32,     10,   1,   1,   "SerialNumber",     NULL},
    {TAG_SYSTEM_MAC_ADDRESS,    8,      10,   1,   1,   "MACAddress",     NULL},
    {TAG_SYSTEM_BOOT_COUNT,     4,      5,    1,   0,   "BootCount",     NULL},
 
    {TAG_SENSOR_CALIB_TEMP,     16,     8,    1,   1,   "SensorCalibTemp",     NULL},
    {TAG_SENSOR_CALIB_PRESSURE, 16,     8,    1,   1,   "SensorCalibPressure",     NULL},
    {TAG_SENSOR_CALIB_HUMIDITY, 16,     8,    1,   1,   "SensorCalibHumidity",     NULL},
    {TAG_SENSOR_OFFSET_X,       12,     6,    1,   0,   "SensorOffsetX",     NULL},
    {TAG_SENSOR_OFFSET_Y,       12,     6,    1,   0,   "SensorOffsetY",     NULL},
    {TAG_SENSOR_OFFSET_Z,       12,     6,    1,   0,   "SensorOffsetZ",     NULL},
 
    {TAG_NET_IP_ADDRESS,        16,     7,    1,   1,   "IPAddress",     NULL},
    {TAG_NET_SUBNET_MASK,       16,     7,    1,   1,   "SubnetMask",     NULL},
    {TAG_NET_GATEWAY,           16,     7,    1,   1,   "Gateway",     NULL},
    {TAG_NET_DNS_SERVER,        16,     7,    1,   1,   "DNSServer",     NULL},
    {TAG_NET_WIFI_SSID,         64,     7,    1,   1,   "WiFiSSID",     NULL},
    {TAG_NET_WIFI_PASSWORD,     64,     7,    1,   1,   "WiFiPassword",     NULL},
 
    {TAG_USER_PROFILE,          256,    5,    1,   1,   "UserProfile",     NULL},
    {TAG_USER_SETTINGS,         128,    5,    1,   1,   "UserSettings",     NULL},
    {TAG_USER_PREFERENCES,      64,     5,    1,   0,   "UserPreferences",     NULL},
    {TAG_USER_HISTORY,          512,    3,    1,   0,   "UserHistory",     NULL},
 
    // 终止符
    {0xFFFF,                    0,      0,    0,   0,   NULL,     NULL}
};
const int TLV_META_MAP_SIZE = (sizeof(TLV_META_MAP) / sizeof(TLV_META_MAP[0])) - 1;
/* ============================ 辅助函数实现 ============================ */

/**
 * @brief 根据TLV标签值获取对应的标签名称
 * 
 * @param tag 要查询的TLV标签值
 * @return 返回标签对应的名称字符串，如果未找到或名称为空则返回"Unknown"
 */
const char *tlv_get_tag_name(uint16_t tag)
{
    // 遍历TLV元数据映射表查找匹配的标签
    for (int i = 0; TLV_META_MAP[i].tag != 0xFFFF; i++)
    {
        // 找到匹配的标签时，返回对应的名称
        if (TLV_META_MAP[i].tag == tag)
        {
            return TLV_META_MAP[i].name ? TLV_META_MAP[i].name : "Unknown";
        }
    }
    // 未找到匹配的标签，返回默认名称
    return "Unknown";
}

/**
 * @brief 根据标签值获取对应TLV项的最大长度
 * 
 * @param tag 要查询的标签值
 * @return 返回对应标签的最大长度，如果未找到则返回0
 */
uint16_t tlv_get_tag_max_length(uint16_t tag)
{
    // 遍历TLV元数据映射表查找匹配的标签
    for (int i = 0; TLV_META_MAP[i].tag != 0xFFFF; i++)
    {
        // 找到匹配的标签，返回其最大长度
        if (TLV_META_MAP[i].tag == tag)
        {
            return TLV_META_MAP[i].max_length;
        }
    }
    // 未找到匹配的标签，返回0
    return 0;
}

/**
 * @brief 根据名称查找对应的TLV标签值
 * 
 * @param name 要查找的TLV项名称
 * @return 返回找到的标签值，如果未找到或输入为空则返回0xFFFF
 */
uint16_t tlv_find_tag_by_name(const char *name)
{
    // 检查输入参数有效性
    if (!name)
        return 0xFFFF;

    // 遍历TLV元数据映射表查找匹配的名称
    for (int i = 0; TLV_META_MAP[i].tag != 0xFFFF; i++)
    {
        // 比较当前项名称与目标名称是否匹配
        if (TLV_META_MAP[i].name && strcmp(TLV_META_MAP[i].name, name) == 0)
        {
            return TLV_META_MAP[i].tag;
        }
    }
    // 未找到匹配项，返回无效标签值
    return 0xFFFF;
}
