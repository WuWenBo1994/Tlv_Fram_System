/**
 * @file tlv_meta_table.c
 * @author wuwenbo
 * @brief TLV元数据表实现
 * @version 1.0
 * @date 2025-12-05
 * 
 * @copyright Copyright (c) 2025
 * @note 需要维护一个TLV元数据表,元数据表是一个常量表,存储元数据信息,元数据信息包括Tag、MaxLen、Prior、Ver、Bkup、Name等信息。
 *       该文件实现了元数据表的定义和获取函数。可在其他文件中定义新的元数据表以覆盖默认实现。
 */
 
#include "tlv_meta_table.h"
#include "tlv_tag.h"
#include "system_config_versions.h"

/* ============================ 元数据表实现 ============================ */
static const tlv_meta_const_t TLV_META_MAP[] = 
{
    // Tag                      MaxLen  Prior Ver  Bkup  Name
    {TAG_SYSTEM_CONFIG,         64,     10,   1,   1,   "SystemConfig",     migrate_system_config},
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

/**
 * @brief 获取元数据表
 */
__weak const tlv_meta_const_t* tlv_get_meta_table(void)
{
    return TLV_META_MAP;
}

/**
 * @brief 获取元数据表size
 */
__weak int tlv_get_meta_table_size(void)
{
    return (sizeof(TLV_META_MAP) / sizeof(TLV_META_MAP[0])) - 1;
}

