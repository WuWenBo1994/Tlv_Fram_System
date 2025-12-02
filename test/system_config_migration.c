/**
 * @file system_config_migration.c
 * @brief 系统配置迁移函数
 */
 
#include "tlv_migration.h"
#include "system_config_versions.h"
#include <string.h>
 
// ========== V1 → V2 迁移 ==========
static int migrate_system_config_v1_to_v2(const void *old_data, uint16_t old_len,
                                          void *new_data, uint16_t *new_len,
                                          uint8_t old_ver, uint8_t new_ver)
{
    if (old_len < sizeof(system_config_v1_t)) {
        return TLV_ERROR_INVALID_PARAM;
    }
 
    const system_config_v1_t *v1 = (const system_config_v1_t*)old_data;
    system_config_v2_t *v2 = (system_config_v2_t*)new_data;
 
    // 清零新结构
    memset(v2, 0, sizeof(system_config_v2_t));
 
    // 拷贝公共字段
    v2->signature = v1->signature;
    v2->version = v1->version;
    v2->language = v1->language;
    v2->timezone = v1->timezone;
 
    // 初始化新字段（设置默认值）
    v2->flags = 0x00000001;  // 默认启用某功能
    strcpy(v2->product, "DefaultProduct");
    v2->reserved = 0;
 
    *new_len = sizeof(system_config_v2_t);
    return TLV_OK;
}
 
// ========== V2 → V3 迁移 ==========
static int migrate_system_config_v2_to_v3(const void *old_data, uint16_t old_len,
                                          void *new_data, uint16_t *new_len,
                                          uint8_t old_ver, uint8_t new_ver)
{
    if (old_len < sizeof(system_config_v2_t)) {
        return TLV_ERROR_INVALID_PARAM;
    }
 
    const system_config_v2_t *v2 = (const system_config_v2_t*)old_data;
    system_config_v3_t *v3 = (system_config_v3_t*)new_data;
 
    memset(v3, 0, sizeof(system_config_v3_t));
 
    // 拷贝公共字段
    v3->signature = v2->signature;
    v3->version = v2->version;
    v3->language = v2->language;
    v3->timezone = v2->timezone;
    v3->flags = v2->flags;
 
    // 扩展字段（16B→32B）
    strncpy(v3->product, v2->product, 16);
    v3->product[31] = '\0';
 
    // 新增字段默认值
    v3->serial_number = 0x12345678;  // 从其他地方读取或设置默认值
    v3->hw_version = 1;
 
    *new_len = sizeof(system_config_v3_t);
    return TLV_OK;
}
 
// ========== 通用迁移函数（支持跨版本） ==========
int migrate_system_config(const void *old_data, uint16_t old_len,
                         void *new_data, uint16_t *new_len,
                         uint8_t old_ver, uint8_t new_ver)
{
    int ret = TLV_OK;
    uint8_t temp_buffer[TLV_MAX_DATA_SIZE];
    const void *src = old_data;
    uint16_t src_len = old_len;
 
    // 逐级升级
    for (uint8_t v = old_ver; v < new_ver; v++) {
        switch (v) {
            case 1:  // V1 → V2
                ret = migrate_system_config_v1_to_v2(src, src_len, 
                                                     temp_buffer, new_len,
                                                     v, v + 1);
                break;
 
            case 2:  // V2 → V3
                ret = migrate_system_config_v2_to_v3(src, src_len,
                                                     temp_buffer, new_len,
                                                     v, v + 1);
                break;
 
            default:
                return TLV_ERROR_VERSION;
        }
 
        if (ret != TLV_OK) {
            return ret;
        }
 
        // 为下一轮迁移准备
        src = temp_buffer;
        src_len = *new_len;
    }
 
    // 拷贝最终结果
    if (src != new_data) {
        memcpy(new_data, src, *new_len);
    }
 
    return TLV_OK;
}
