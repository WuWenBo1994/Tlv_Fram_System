/**
 * @file tlv_migration.c
 * @brief TLV数据版本迁移实现
 */
 
#include "tlv_migration.h"
#include "tlv_fram.h"
#include "tlv_index.h"
#include "tlv_port.h"
#include "tlv_utils.h"
#include "tlv_meta_table.h"
#include <string.h>
 
/* ============================ 外部变量 ============================ */
 
extern tlv_context_t g_tlv_ctx;
 
/* ============================ 私有变量 ============================ */
 
static uint32_t g_migrated_count = 0;
static uint32_t g_failed_count = 0;
 
/* ============================ 迁移函数实现 ============================ */
 
/**
 * @brief 迁移单个Tag的数据到最新版本
 * @param tag 
 * @param data 
 * @param len 
 * @param current_ver 当前版本
 * @return int 
 */
int tlv_migrate_tag(uint16_t tag, void *data, uint16_t *len, uint8_t current_ver)
{
    if (!data || !len) {
        return TLV_ERROR_INVALID_PARAM;
    }
 
    // 查找元数据
    const tlv_meta_const_t *meta = NULL;
    for (uint32_t i = 0; i < g_tlv_ctx.meta_table_size; i++) {
        if (g_tlv_ctx.meta_table[i].tag == tag) {
            meta = &g_tlv_ctx.meta_table[i];
            break;
        }
        if (g_tlv_ctx.meta_table[i].tag == 0xFFFF) {
            break;
        }
    }
 
    if (!meta) {
        return TLV_ERROR_NOT_FOUND;
    }
 
    // 检查版本
    if (current_ver == meta->version) {
        // 版本相同，无需迁移
        return TLV_OK;
    }
 
    if (current_ver > meta->version) {
        // 当前版本高于固件版本（降级？），报错
        return TLV_ERROR_VERSION;
    }
 
    // 需要迁移：current_ver < meta->version
    if (!meta->migrate) {
        // 没有提供迁移函数，报错
        return TLV_ERROR_VERSION;
    }
 
    // 分配临时缓冲区 ，这个太大了吧
    uint8_t temp_buffer[TLV_MAX_DATA_SIZE];
    uint16_t new_len = sizeof(temp_buffer);
 
    // 调用迁移函数
    int ret = meta->migrate(data, *len, temp_buffer, &new_len, 
                            current_ver, meta->version);
    if (ret != TLV_OK) {
        return ret;
    }
 
    // 检查新数据长度
    if (new_len > meta->max_length) {
        return TLV_ERROR_INVALID_PARAM;
    }
 
    // 拷贝新数据回原缓冲区
    memcpy(data, temp_buffer, new_len);
    *len = new_len;
 
    return TLV_OK;
}
 
int tlv_migrate_all(void)
{
    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED) {
        return TLV_ERROR;
    }
 
    g_migrated_count = 0;
    g_failed_count = 0;
 
    // 遍历所有索引
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++) {
        tlv_index_entry_t *entry = &g_tlv_ctx.index_table->entries[i];
        
        if (entry->tag == 0 || !(entry->flags & TLV_FLAG_VALID)) {
            continue;
        }
 
        // 查找元数据
        const tlv_meta_const_t *meta = NULL;
        for (uint32_t j = 0; j < g_tlv_ctx.meta_table_size; j++) {
            if (g_tlv_ctx.meta_table[j].tag == entry->tag) {
                meta = &g_tlv_ctx.meta_table[j];
                break;
            }
        }
 
        if (!meta) {
            continue;  // Tag不在元数据表中，跳过
        }
 
        // 检查版本
        if (entry->version == meta->version) {
            continue;  // 版本相同，无需迁移
        }
 
        if (entry->version > meta->version) {
            // 降级情况，报警但跳过
            g_failed_count++;
            continue;
        }
 
        // 需要迁移
        uint8_t old_data[TLV_MAX_DATA_SIZE];
        uint16_t old_len = sizeof(old_data);
 
        // 读取旧数据
        int ret = tlv_read(entry->tag, old_data, &old_len);
        if (ret != TLV_OK) {
            g_failed_count++;
            continue;
        }
 
        // 迁移数据
        uint8_t new_data[TLV_MAX_DATA_SIZE];
        uint16_t new_len = sizeof(new_data);
        
        memcpy(new_data, old_data, old_len);
        new_len = old_len;
 
        ret = tlv_migrate_tag(entry->tag, new_data, &new_len, entry->version);
        if (ret != TLV_OK) {
            g_failed_count++;
            continue;
        }
 
        // 写回新数据
        ret = tlv_write(entry->tag, new_data, new_len);
        if (ret < 0) {
            g_failed_count++;
            continue;
        }
 
        // 更新索引中的版本号
        entry->version = meta->version;
 
        g_migrated_count++;
    }
 
    // 保存更新的索引表
    tlv_index_save(&g_tlv_ctx);
 
    return g_migrated_count;
}
 
int tlv_get_migration_stats(uint32_t *migrated, uint32_t *failed)
{
    if (migrated) *migrated = g_migrated_count;
    if (failed) *failed = g_failed_count;
    return TLV_OK;
}
