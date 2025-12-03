/**
 * @file tlv_migration.c
 * @brief TLV数据版本迁移实现
 */

#include "tlv_migration.h"
#include "tlv_fram.h"
#include "tlv_index.h"
#include "tlv_meta_table.h"
#include <string.h>

/* ============================ 外部变量 ============================ */

extern tlv_context_t g_tlv_ctx;

/* ============================ 私有变量 ============================ */

static uint32_t g_migrated_count = 0;
static uint32_t g_failed_count = 0;

/* ============================ 辅助函数 ============================ */

static const tlv_meta_const_t *get_meta(uint16_t tag)
{
    for (uint32_t i = 0; i < g_tlv_ctx.meta_table_size; i++)
    {
        if (g_tlv_ctx.meta_table[i].tag == tag)
        {
            return &g_tlv_ctx.meta_table[i];
        }
        if (g_tlv_ctx.meta_table[i].tag == 0xFFFF)
        {
            break;
        }
    }
    return NULL;
}

/* ============================ 核心迁移函数 ============================ */

/**
 * @brief 迁移单个Tag数据
 * @param tag        Tag标识
 * @param data       输入/输出缓冲区（同一个）
 *                   - 输入时包含旧版本数据
 *                   - 输出时包含新版本数据
 * @param old_len    旧数据长度
 * @param *new_len   新数据长度（输出）
 * @param max_size   输入输出缓冲区最大容量
 * @param old_ver    旧版本号
 * @param new_ver    新版本号
 * @return 0: 成功, 其他: 错误码
 *
 * @note 迁移函数必须能够在同一缓冲区中完成转换
 *       如果需要临时空间，使用栈上的小变量（<256B）
 */
int tlv_migrate_tag(uint16_t tag,
                    void *data,
                    uint16_t old_len,
                    uint16_t *new_len,
                    uint16_t max_size,
                    uint8_t current_ver)
{
    if (!data || !new_len || max_size == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 查找元数据
    const tlv_meta_const_t *meta = get_meta(tag);
    if (!meta)
    {
        return TLV_ERROR_NOT_FOUND;
    }

    // ========== 版本检查 ==========
    if (current_ver == meta->version)
    {
        // 版本相同，无需迁移
        *new_len = old_len;
        return TLV_OK;
    }

    if (current_ver > meta->version)
    {
// 版本回退，不支持
#ifdef TLV_DEBUG
        printf("ERROR: Version downgrade not supported\n");
        printf("  Tag: 0x%04X, Current: %u, Target: %u\n",
               tag, current_ver, meta->version);
#endif
        return TLV_ERROR_VERSION;
    }

    // ========== 检查是否有迁移函数 ==========
    if (!meta->migrate)
    {
        // 没有迁移函数，无法升级
#ifdef TLV_DEBUG
        printf("ERROR: No migration function for tag 0x%04X\n", tag);
        printf("  Current version: %u, Expected: %u\n",
               current_ver, meta->version);
#endif
        return TLV_ERROR_VERSION;
    }

    // ========== 执行迁移（原地迁移）==========
    int ret = meta->migrate(data, old_len, new_len, max_size,
                            current_ver, meta->version);

    if (ret != TLV_OK)
    {
#ifdef TLV_DEBUG
        printf("ERROR: Migration failed for tag 0x%04X\n", tag);
        printf("  Error code: %d\n", ret);
#endif
        return ret;
    }

    // 验证结果
    if (*new_len > meta->max_length)
    {
#ifdef TLV_DEBUG
        printf("ERROR: Migration result too large\n");
        printf("  Tag: 0x%04X, Result: %u, Max: %u\n",
               tag, *new_len, meta->max_length);
#endif
        return TLV_ERROR_INVALID_PARAM;
    }

    if (*new_len > max_size)
    {
#ifdef TLV_DEBUG
        printf("ERROR: Migration result exceeds buffer\n");
        printf("  Tag: 0x%04X, Result: %u, Buffer: %u\n",
               tag, *new_len, max_size);
#endif
        return TLV_ERROR_INVALID_PARAM;
    }
#ifdef TLV_DEBUG
    printf("Migration successful: Tag 0x%04X, v%u -> v%u, %u -> %u bytes\n",
           tag, current_ver, meta->version, old_len, *new_len);
#endif

    return TLV_OK;
}

/* ============================ 批量迁移（可选）============================ */

int tlv_migrate_all(void)
{
    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    g_migrated_count = 0;
    g_failed_count = 0;

    // 遍历所有索引
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        tlv_index_entry_t *entry = &g_tlv_ctx.index_table->entries[i];

        if (entry->tag == 0 || !(entry->flags & TLV_FLAG_VALID))
        {
            continue;
        }

        // 查找元数据
        const tlv_meta_const_t *meta = get_meta(entry->tag);
        if (!meta)
        {
            continue; // Tag不在元数据表中，跳过
        }

        // 检查版本
        if (entry->version == meta->version)
        {
            continue; // 版本相同，无需迁移
        }

        if (entry->version > meta->version)
        {
            // 版本降级，警告但跳过
#ifdef TLV_DEBUG
            printf("WARNING: Tag 0x%04X version downgrade detected (v%u -> v%u)\n",
                   entry->tag, entry->version, meta->version);
#endif
            g_failed_count++;
            continue;
        }

        if (!meta->migrate)
        {
            // 无迁移函数，警告但跳过
#ifdef TLV_DEBUG
            printf("WARNING: Tag 0x%04X needs migration but no function provided\n",
                   entry->tag);
#endif
            g_failed_count++;
            continue;
        }

        // ========== 需要迁移 ==========

        // 分配缓冲区（使用栈或静态，根据大小）
        uint8_t stack_buffer[256]; // 小缓冲区在栈上
        uint8_t *buffer = NULL;
        uint16_t buffer_size = 0;

        if (meta->max_length <= sizeof(stack_buffer))
        {
            // 小数据：使用栈缓冲区
            buffer = stack_buffer;
            buffer_size = sizeof(stack_buffer);
        }
        else if (meta->max_length <= TLV_BUFFER_SIZE)
        {
            // 中等数据：使用全局静态缓冲区
            buffer = g_tlv_ctx.static_buffer;
            buffer_size = TLV_BUFFER_SIZE;
        }
        else
        {
            // 数据太大，跳过
#ifdef TLV_DEBUG
            printf("ERROR: Tag 0x%04X too large for migration (%u bytes)\n",
                   entry->tag, meta->max_length);
#endif
            g_failed_count++;
            continue;
        }

        // 读取旧数据
        uint16_t read_len = buffer_size;
        int ret = tlv_read(entry->tag, buffer, &read_len);
        if (ret != TLV_OK)
        {
            g_failed_count++;
            continue;
        }

        // 迁移数据（原地）
        uint16_t new_len = 0;
        ret = tlv_migrate_tag(entry->tag, buffer, read_len, &new_len,
                              buffer_size, entry->version);
        if (ret != TLV_OK)
        {
            g_failed_count++;
            continue;
        }

        // 写回新数据
        ret = tlv_write(entry->tag, buffer, new_len);
        if (ret < 0)
        {
            g_failed_count++;
            continue;
        }

        // 更新索引版本（tlv_write已经更新）
        g_migrated_count++;
    }

    return g_migrated_count;
}

/* ============================ 统计函数 ============================ */

int tlv_get_migration_stats(uint32_t *migrated, uint32_t *failed)
{
    if (migrated)
    {
        *migrated = g_migrated_count;
    }
    if (failed)
    {
        *failed = g_failed_count;
    }
    return TLV_OK;
}
