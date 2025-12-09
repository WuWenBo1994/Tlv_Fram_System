/**
 * @file system_config_migration.c
 */
#include "system_config_versions.h"
#include "tlv_migration.h"
#include <string.h>



/**
 * @brief V1->V2的迁移函数（原地）
 *
 * @param data      指向配置数据的指针,用于原地迁移
 * @param old_len   旧配置数据的长度
 * @param new_len   输出参数,指向存储新配置数据长度的变量
 * @param max_size  数据缓冲区的最大大小
 * @param old_ver   旧配置版本号,应为1
 * @param new_ver   新配置版本号,应为2
 *
 * @return TLV_OK 成功迁移
 *         TLV_ERROR_INVALID_PARAM 参数无效,旧数据长度不足
 *         TLV_ERROR_NO_BUFFER_MEMORY 缓冲区空间不足
 *         TLV_ERROR_VERSION 版本号不匹配
 */
int migrate_system_config_v1_to_v2(void *data,
                                   uint16_t old_len,
                                   uint16_t *new_len,
                                   uint16_t max_size,
                                   uint8_t old_ver,
                                   uint8_t new_ver)
{
    // 参数检查
    if (old_len < sizeof(system_config_v1_t))
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (max_size < sizeof(system_config_v2_t))
    {
		*new_len = sizeof(system_config_v2_t);
        return TLV_ERROR_NO_BUFFER_MEMORY;
    }

    if (old_ver != 1 && new_ver != 2)
    {
        return TLV_ERROR_VERSION;
    }

    // ========== 原地迁移：从后往前 ==========

    system_config_v1_t *v1 = (system_config_v1_t *)data;

    // 1. 保存旧数据到临时变量（栈上）
    uint32_t sig = v1->signature;
    uint16_t ver = v1->version;
    uint8_t lang = v1->language;
    uint8_t tz = v1->timezone;

    // 2. 转换为新结构（同一缓冲区）
    system_config_v2_t *v2 = (system_config_v2_t *)data;

    // 3. 填充新结构（从后往前,避免覆盖）
    v2->reserved = 0;
    strcpy(v2->product, "DefaultProduct");
    v2->flags = 0x00000001;
    v2->timezone = tz;
    v2->language = lang;
    v2->version = ver;
    v2->signature = sig;

    *new_len = sizeof(system_config_v2_t);

    return TLV_OK;
}


/**
 * @brief V2->V3的迁移函数（原地）
 *
 * @param data      指向配置数据的指针,用于原地迁移
 * @param old_len   旧配置数据的长度
 * @param new_len   输出参数,指向存储新配置数据长度的变量
 * @param max_size  数据缓冲区的最大大小
 * @param old_ver   旧配置版本号,应为1
 * @param new_ver   新配置版本号,应为2
 *
 * @return TLV_OK 成功迁移
 *         TLV_ERROR_INVALID_PARAM 参数无效,旧数据长度不足
 *         TLV_ERROR_NO_BUFFER_MEMORY 缓冲区空间不足
 *         TLV_ERROR_VERSION 版本号不匹配
 */
int migrate_system_config_v2_to_v3(void *data,
                                   uint16_t old_len,
                                   uint16_t *new_len,
                                   uint16_t max_size,
                                   uint8_t old_ver,
                                   uint8_t new_ver)
{
    // 参数检查
    if (old_len < sizeof(system_config_v2_t))
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (max_size < sizeof(system_config_v3_t))
    {
		*new_len = sizeof(system_config_v3_t);
        return TLV_ERROR_NO_BUFFER_MEMORY;
    }

    if (old_ver != 2 && new_ver != 3)
    {
        return TLV_ERROR_VERSION;
    }

    // ========== 原地迁移：从后往前 ==========

    system_config_v2_t *v2 = (system_config_v2_t *)data;

    // 1. 保存旧数据到临时变量（栈上）
    uint32_t sig = v2->signature;
    uint16_t ver = v2->version;
    uint8_t lang = v2->language;
    uint8_t tz = v2->timezone;
    uint32_t flags = v2->flags;
    char product_temp[16];
    strncpy(product_temp, v2->product, sizeof(product_temp));
    product_temp[sizeof(product_temp) - 1] = '\0';

    // 2. 转换为新结构（同一缓冲区）
    system_config_v3_t *v3 = (system_config_v3_t *)data;

    // 3. 清零整个结构
    memset(v3, 0, sizeof(system_config_v3_t));
// 4. 填充新结构
// 新增字段：硬件版本
#define CONFIG_DEFAULT_HW_VERSION 0
    v3->hw_version = CONFIG_DEFAULT_HW_VERSION;

// 新增字段：序列号（从其他地方获取,或使用默认值）
// 这里可以从硬件读取,或者使用默认值
#define CONFIG_DEFAULT_SERIAL 0
    v3->serial_number = CONFIG_DEFAULT_SERIAL;

    // 扩展字段：产品名称（16B → 32B）
    strncpy(v3->product, product_temp, sizeof(v3->product) - 1);
    v3->product[sizeof(v3->product) - 1] = '\0';

    v3->flags = flags;
    v3->timezone = tz;
    v3->language = lang;
    v3->version = ver;
    v3->signature = sig;

    *new_len = sizeof(system_config_v3_t);
    return TLV_OK;
}

/**
 * @brief 通用迁移函数（支持任意版本升级）
 */
int migrate_system_config(void *data,
                          uint16_t old_len,
                          uint16_t *new_len,
                          uint16_t max_size,
                          uint8_t old_ver,
                          uint8_t new_ver)
{
    int ret = TLV_OK;

    if (!data || !new_len)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (old_ver >= new_ver)
    {
        return TLV_ERROR_VERSION;
    }

    // 逐级升级
    for (uint8_t v = old_ver; v < new_ver; v++)
    {
        switch (v)
        {
        case 1: // V1 → V2
            ret = migrate_system_config_v1_to_v2(data, old_len, new_len,
                                                 max_size, v, v + 1);
            if (ret != TLV_OK)
                return ret;
            old_len = *new_len; // 下一轮的输入长度
            break;

        case 2: // V2 → V3
            ret = migrate_system_config_v2_to_v3(data, old_len, new_len,
                                                 max_size, v, v + 1);
            if (ret != TLV_OK)
                return ret;
            old_len = *new_len;
            break;

        default:
            return TLV_ERROR_VERSION;
        }
    }

    return TLV_OK;
}
