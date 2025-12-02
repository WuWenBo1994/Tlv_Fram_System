/**
 * @file tlv_core.c
 * @brief TLV FRAM存储系统核心读写逻辑（裸机简化版）
 */

#include "tlv_fram.h"
#include "tlv_index.h"
#include "tlv_port.h"
#include "tlv_utils.h"
#include "tlv_meta_table.h"
#include <string.h>

/* ============================ 全局静态变量 ============================ */

static tlv_context_t g_tlv_ctx = {0};

/* 静态分配的内存（替代malloc） */
static tlv_system_header_t g_static_header;
static tlv_index_table_t g_static_index;

/* ============================ 私有函数声明 ============================ */

static int system_header_init(void);
static int system_header_load(void);
static int system_header_save(void);
static int system_header_verify(void);
static uint32_t allocate_space(uint32_t size);
static int write_data_block(uint16_t tag, const void *data, uint16_t len, uint32_t addr);
static int read_data_block(uint32_t addr, void *buf, uint16_t *len);
static const tlv_meta_const_t *get_meta(uint16_t tag);

/* ============================ 系统管理API实现 ============================ */

tlv_init_result_t tlv_init(void)
{
    int ret;
    tlv_init_result_t result = TLV_INIT_ERROR;

    // 初始化硬件
    ret = tlv_port_fram_init();
    if (ret != TLV_OK)
    {
        return TLV_INIT_ERROR;
    }

    // 使用静态分配的内存
    g_tlv_ctx.header = &g_static_header;
    g_tlv_ctx.index_table = &g_static_index;

    // 设置元数据表
    g_tlv_ctx.meta_table = TLV_META_MAP;
    g_tlv_ctx.meta_table_size = sizeof(TLV_META_MAP) / sizeof(tlv_meta_const_t);

    // 尝试加载系统Header
    ret = system_header_load();
    if (ret == TLV_OK)
    {
        // Header有效，初始化索引系统
        ret = tlv_index_init(&g_tlv_ctx);
        if (ret != TLV_OK)
        {
            goto error_cleanup;
        }

        // 加载索引表
        ret = tlv_index_load(&g_tlv_ctx);
        if (ret == TLV_OK)
        {
            g_tlv_ctx.state = TLV_STATE_INITIALIZED;
            result = TLV_INIT_OK;
        }
        else
        {
            // 索引表损坏，尝试从备份恢复
            ret = tlv_restore_from_backup();
            if (ret == TLV_OK)
            {
                g_tlv_ctx.state = TLV_STATE_INITIALIZED;
                result = TLV_INIT_RECOVERED;
            }
            else
            {
                goto error_cleanup;
            }
        }
    }
    else
    {
        // 首次启动或数据损坏
        result = TLV_INIT_FIRST_BOOT;
        g_tlv_ctx.state = TLV_STATE_UNINITIALIZED;
    }

    return result;

error_cleanup:
    g_tlv_ctx.state = TLV_STATE_ERROR;
    return TLV_INIT_ERROR;
}

int tlv_deinit(void)
{
    // 保存索引表
    if (g_tlv_ctx.index_table)
    {
        tlv_index_save(&g_tlv_ctx);
    }

    // 保存系统Header
    if (g_tlv_ctx.header)
    {
        system_header_save();
    }

    // 反初始化索引系统
    tlv_index_deinit(&g_tlv_ctx);

    g_tlv_ctx.state = TLV_STATE_UNINITIALIZED;

    return TLV_OK;
}

int tlv_format(uint32_t magic)
{
    // 初始化系统Header
    int ret = system_header_init();
    if (ret != TLV_OK)
    {
        return ret;
    }

    if (magic != 0)
    {
        g_tlv_ctx.header->magic = magic;
    }

    // 初始化索引系统
    ret = tlv_index_init(&g_tlv_ctx);
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 保存Header和索引表
    ret = system_header_save();
    if (ret != TLV_OK)
    {
        return ret;
    }

    ret = tlv_index_save(&g_tlv_ctx);
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 备份管理区
    tlv_backup_all();

    g_tlv_ctx.state = TLV_STATE_FORMATTED;

    return ret;
}

tlv_state_t tlv_get_state(void)
{
    return g_tlv_ctx.state;
}

/* ============================ 数据操作API实现 ============================ */

int tlv_write(uint16_t tag, const void *data, uint16_t len)
{
    if (!data || len == 0 || tag == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    int ret = TLV_ERROR;

    // 查找元数据
    const tlv_meta_const_t *meta = get_meta(tag);
    if (!meta)
    {
        return TLV_ERROR_NOT_FOUND;
    }

    // 检查长度
    if (len > meta->max_length)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 查找现有索引
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, tag);
    uint32_t target_addr;

    if (index && (index->flags & TLV_FLAG_VALID))
    {
        // Tag已存在，检查是否可以原地更新
        tlv_data_block_header_t old_header;
        tlv_port_fram_read(index->data_addr, &old_header, sizeof(old_header));

        uint32_t old_total_size = sizeof(tlv_data_block_header_t) + old_header.length + 2;
        uint32_t new_total_size = sizeof(tlv_data_block_header_t) + len + 2;

        if (new_total_size <= old_total_size)
        {
            // 原地更新
            target_addr = index->data_addr;
        }
        else
        {
            // 需要重新分配
            target_addr = allocate_space(new_total_size);
            if (target_addr == 0)
            {
                return TLV_ERROR_NO_SPACE;
            }
            // 标记旧块为脏
            index->flags |= TLV_FLAG_DIRTY;
        }
    }
    else
    {
        // 新Tag，分配空间
        uint32_t total_size = sizeof(tlv_data_block_header_t) + len + 2;
        target_addr = allocate_space(total_size);
        if (target_addr == 0)
        {
            return TLV_ERROR_NO_SPACE;
        }
    }

    // 写入数据块
    ret = write_data_block(tag, data, len, target_addr);
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 更新或添加索引
    if (index)
    {
        tlv_index_update(&g_tlv_ctx, tag, target_addr);
    }
    else
    {
        index = tlv_index_add(&g_tlv_ctx, tag, target_addr);
        if (!index)
        {
            return TLV_ERROR_NO_MEMORY;
        }
    }

    // 更新统计
    g_tlv_ctx.header->total_writes++;
    g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();

    // 定期保存索引表
    if (g_tlv_ctx.header->total_writes % 100 == 0)
    {
        tlv_index_save(&g_tlv_ctx);
        system_header_save();
    }

    return len; // 返回写入长度
}

int tlv_read(uint16_t tag, void *buf, uint16_t *len)
{
    if (!buf || !len || tag == 0 || *len == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    // 查找索引
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, tag);
    if (!index || !(index->flags & TLV_FLAG_VALID))
    {
        return TLV_ERROR_NOT_FOUND;
    }

    // 读取数据块
    uint16_t read_len = *len;
    int ret = read_data_block(index->data_addr, buf, &read_len);
    if (ret != TLV_OK)
    {
        return ret;
    }

#if TLV_ENABLE_MIGRATION    
    // 查找元数据获取期望版本
    const tlv_meta_const_t *meta = get_meta(tag);
    if (meta && index->version < meta->version) {
        // 需要升级
        
        // 迁移数据
        ret = tlv_migrate_tag(tag, buf, &read_len, index->version);
        if (ret == TLV_OK) {
            // 迁移成功，写回FRAM
            tlv_write(tag, buf, read_len);
            
            // 更新索引版本
            index->version = meta->version;
            tlv_index_save(&g_tlv_ctx);
        } else {
            // 迁移失败，返回错误但数据仍可用（旧版本）
            // 根据策略决定是否继续
        }
    }
#endif

    *len = read_len;
    return TLV_OK;
}

int tlv_delete(uint16_t tag)
{
    if (tag == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    // 删除索引
    int ret = tlv_index_remove(&g_tlv_ctx, tag);

    if (ret == TLV_OK)
    {
        // 更新统计
        g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();

        // 定期保存
        if (g_tlv_ctx.header->total_writes % 100 == 0)
        {
            tlv_index_save(&g_tlv_ctx);
            system_header_save();
        }
    }

    return ret;
}

bool tlv_exists(uint16_t tag)
{
    if (tag == 0 || g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return false;
    }

    tlv_index_entry_t *entry = tlv_index_find(&g_tlv_ctx, tag);
    return (entry != NULL && (entry->flags & TLV_FLAG_VALID));
}

int tlv_get_length(uint16_t tag, uint16_t *len)
{
    if (!len || tag == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, tag);
    if (!index)
    {
        return TLV_ERROR_NOT_FOUND;
    }

    // 读取数据块Header获取长度
    tlv_data_block_header_t header;
    int ret = tlv_port_fram_read(index->data_addr, &header, sizeof(header));
    if (ret != TLV_OK)
    {
        return ret;
    }

    *len = header.length;
    return TLV_OK;
}

/* ============================ 批量操作API实现 ============================ */

int tlv_read_batch(const uint16_t *tags, uint16_t count,
                   void **buffers, uint16_t *lengths)
{
    if (!tags || !buffers || !lengths || count == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    int success_count = 0;

    for (uint16_t i = 0; i < count; i++)
    {
        if (tlv_read(tags[i], buffers[i], &lengths[i]) == TLV_OK)
        {
            success_count++;
        }
    }

    return success_count;
}

int tlv_write_batch(const uint16_t *tags, uint16_t count,
                    const void **datas, const uint16_t *lengths)
{
    if (!tags || !datas || !lengths || count == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    int success_count = 0;

    for (uint16_t i = 0; i < count; i++)
    {
        if (tlv_write(tags[i], datas[i], lengths[i]) >= 0)
        {
            success_count++;
        }
    }

    // 批量写入后保存索引
    tlv_index_save(&g_tlv_ctx);
    system_header_save();

    return success_count;
}

/* ============================ 查询与统计API实现 ============================ */

int tlv_get_statistics(tlv_statistics_t *stats)
{
    if (!stats)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    memset(stats, 0, sizeof(tlv_statistics_t));

    stats->total_tags = TLV_MAX_TAG_COUNT;
    stats->valid_tags = g_tlv_ctx.header->tag_count;
    stats->free_space = g_tlv_ctx.header->free_space;
    stats->used_space = g_tlv_ctx.header->used_space;

    // 计算脏数据Tag数
    uint32_t dirty_count = 0;
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        if (g_tlv_ctx.index_table->entries[i].tag != 0 &&
            (g_tlv_ctx.index_table->entries[i].flags & TLV_FLAG_DIRTY))
        {
            dirty_count++;
        }
    }
    stats->dirty_tags = dirty_count;

    // 计算碎片化程度
    if (g_tlv_ctx.header->data_region_size > 0)
    {
        uint32_t wasted = (g_tlv_ctx.header->next_free_addr - TLV_DATA_ADDR) -
                          g_tlv_ctx.header->used_space;
        stats->fragmentation = (wasted * 100) / g_tlv_ctx.header->data_region_size;
    }

    return TLV_OK;
}

int tlv_foreach(tlv_foreach_callback_t callback, void *user_data)
{
    if (!callback)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    int count = 0;

    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        if (g_tlv_ctx.index_table->entries[i].tag != 0 &&
            (g_tlv_ctx.index_table->entries[i].flags & TLV_FLAG_VALID))
        {
            callback(g_tlv_ctx.index_table->entries[i].tag, user_data);
            count++;
        }
    }

    return count;
}

/* ============================ 维护管理API实现 ============================ */

int tlv_defragment(void)
{
    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    int ret = TLV_OK;
    uint32_t write_pos = TLV_DATA_ADDR;

    // 遍历所有有效Tag
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        tlv_index_entry_t *entry = &g_tlv_ctx.index_table->entries[i];

        if (entry->tag == 0 || !(entry->flags & TLV_FLAG_VALID))
        {
            continue;
        }

        // 读取数据块Header
        tlv_data_block_header_t header;
        ret = tlv_port_fram_read(entry->data_addr, &header, sizeof(header));
        if (ret != TLV_OK)
        {
            continue;
        }

        uint32_t block_size = sizeof(header) + header.length + 2;

        // 如果不在紧凑位置，移动它
        if (entry->data_addr != write_pos)
        {
            // 使用静态缓冲区分批读写
            uint32_t remaining = block_size;
            uint32_t src_offset = 0;

            while (remaining > 0)
            {
                uint32_t chunk_size = (remaining > TLV_BUFFER_SIZE) ? TLV_BUFFER_SIZE : remaining;

                // 读取
                ret = tlv_port_fram_read(entry->data_addr + src_offset,
                                         g_tlv_ctx.static_buffer, chunk_size);
                if (ret != TLV_OK)
                {
                    return ret;
                }

                // 写入新位置
                ret = tlv_port_fram_write(write_pos + src_offset,
                                          g_tlv_ctx.static_buffer, chunk_size);
                if (ret != TLV_OK)
                {
                    return ret;
                }

                src_offset += chunk_size;
                remaining -= chunk_size;
            }

            // 更新索引
            entry->data_addr = write_pos;
            entry->flags &= ~TLV_FLAG_DIRTY;
        }

        write_pos += block_size;
    }

    // 更新系统Header
    uint32_t reclaimed = (g_tlv_ctx.header->next_free_addr - TLV_DATA_ADDR) -
                         (write_pos - TLV_DATA_ADDR);
    g_tlv_ctx.header->next_free_addr = write_pos;
    g_tlv_ctx.header->free_space += reclaimed;

    // 保存更新
    tlv_index_save(&g_tlv_ctx);
    system_header_save();

    return ret;
}

int tlv_verify_all(uint32_t *corrupted_count)
{
    if (!corrupted_count)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    *corrupted_count = 0;

    // 遍历索引表验证每个数据块
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        tlv_index_entry_t *entry = &g_tlv_ctx.index_table->entries[i];

        if (entry->tag == 0 || !(entry->flags & TLV_FLAG_VALID))
        {
            continue;
        }

        // 读取Header
        tlv_data_block_header_t header;
        int ret = tlv_port_fram_read(entry->data_addr, &header, sizeof(header));
        if (ret != TLV_OK)
        {
            (*corrupted_count)++;
            continue;
        }

        // 检查Tag匹配
        if (header.tag != entry->tag)
        {
            (*corrupted_count)++;
            continue;
        }

        // 读取数据并验证CRC（分批读取）
        uint16_t crc = tlv_crc16_init();
        crc = tlv_crc16_update(crc, &header, sizeof(header));

        uint32_t remaining = header.length;
        uint32_t offset = 0;

        while (remaining > 0)
        {
            uint32_t chunk_size = (remaining > TLV_BUFFER_SIZE) ? TLV_BUFFER_SIZE : remaining;

            ret = tlv_port_fram_read(entry->data_addr + sizeof(header) + offset,
                                     g_tlv_ctx.static_buffer, chunk_size);
            if (ret != TLV_OK)
            {
                (*corrupted_count)++;
                break;
            }

            crc = tlv_crc16_update(crc, g_tlv_ctx.static_buffer, chunk_size);
            offset += chunk_size;
            remaining -= chunk_size;
        }

        if (ret != TLV_OK)
        {
            continue;
        }

        crc = tlv_crc16_final(crc);

        // 读取存储的CRC
        uint16_t stored_crc;
        ret = tlv_port_fram_read(entry->data_addr + sizeof(header) + header.length,
                                 &stored_crc, sizeof(stored_crc));
        if (ret != TLV_OK || crc != stored_crc)
        {
            (*corrupted_count)++;
        }
    }

    return (*corrupted_count > 0) ? TLV_ERROR_CORRUPTED : TLV_OK;
}

int tlv_backup_all(void)
{
    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED &&
        g_tlv_ctx.state != TLV_STATE_FORMATTED)
    {
        return TLV_ERROR;
    }

    int ret = TLV_OK;
    uint32_t backup_size = TLV_BACKUP_ADDR - TLV_HEADER_ADDR;

    // 分批备份
    uint32_t offset = 0;
    while (offset < backup_size)
    {
        uint32_t chunk_size = (backup_size - offset > TLV_BUFFER_SIZE) ? TLV_BUFFER_SIZE : (backup_size - offset);

        // 读取管理区
        ret = tlv_port_fram_read(TLV_HEADER_ADDR + offset,
                                 g_tlv_ctx.static_buffer, chunk_size);
        if (ret != TLV_OK)
        {
            return ret;
        }

        // 写入备份区
        ret = tlv_port_fram_write(TLV_BACKUP_ADDR + offset,
                                  g_tlv_ctx.static_buffer, chunk_size);
        if (ret != TLV_OK)
        {
            return ret;
        }

        offset += chunk_size;
    }

    // 更新备份时间
    g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();
    system_header_save();

    return ret;
}

int tlv_restore_from_backup(void)
{
    int ret = TLV_OK;
    uint32_t backup_size = TLV_BACKUP_ADDR - TLV_HEADER_ADDR;

    // 分批恢复
    uint32_t offset = 0;
    while (offset < backup_size)
    {
        uint32_t chunk_size = (backup_size - offset > TLV_BUFFER_SIZE) ? TLV_BUFFER_SIZE : (backup_size - offset);

        // 读取备份区
        ret = tlv_port_fram_read(TLV_BACKUP_ADDR + offset,
                                 g_tlv_ctx.static_buffer, chunk_size);
        if (ret != TLV_OK)
        {
            return ret;
        }

        // 写入管理区
        ret = tlv_port_fram_write(TLV_HEADER_ADDR + offset,
                                  g_tlv_ctx.static_buffer, chunk_size);
        if (ret != TLV_OK)
        {
            return ret;
        }

        offset += chunk_size;
    }

    // 重新加载
    ret = system_header_load();
    if (ret != TLV_OK)
    {
        return ret;
    }

    ret = tlv_index_load(&g_tlv_ctx);
    return ret;
}

/* ============================ 空间管理API实现 ============================ */

int tlv_get_free_space(uint32_t *free_space)
{
    if (!free_space)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    *free_space = g_tlv_ctx.header->free_space;
    return TLV_OK;
}

int tlv_get_used_space(uint32_t *used_space)
{
    if (!used_space)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    *used_space = g_tlv_ctx.header->used_space;
    return TLV_OK;
}

int tlv_calculate_fragmentation(uint32_t *fragmentation_percent)
{
    if (!fragmentation_percent)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    uint32_t allocated = g_tlv_ctx.header->next_free_addr - TLV_DATA_ADDR;
    uint32_t used = g_tlv_ctx.header->used_space;
    uint32_t wasted = allocated - used;

    if (g_tlv_ctx.header->data_region_size > 0)
    {
        *fragmentation_percent = (wasted * 100) / g_tlv_ctx.header->data_region_size;
    }
    else
    {
        *fragmentation_percent = 0;
    }

    return TLV_OK;
}

/* ============================ 私有函数实现 ============================ */

static int system_header_init(void)
{
    if (!g_tlv_ctx.header)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    memset(g_tlv_ctx.header, 0, sizeof(tlv_system_header_t));

    g_tlv_ctx.header->magic = TLV_SYSTEM_MAGIC;
    g_tlv_ctx.header->version = 0x0100;
    g_tlv_ctx.header->tag_count = 0;
    g_tlv_ctx.header->data_region_start = TLV_DATA_ADDR;
    g_tlv_ctx.header->data_region_size = TLV_BACKUP_ADDR - TLV_DATA_ADDR;
    g_tlv_ctx.header->next_free_addr = TLV_DATA_ADDR;
    g_tlv_ctx.header->total_writes = 0;
    g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();
    g_tlv_ctx.header->free_space = g_tlv_ctx.header->data_region_size;
    g_tlv_ctx.header->used_space = 0;

    // 计算Header CRC16
    g_tlv_ctx.header->header_crc16 = tlv_crc16(g_tlv_ctx.header,
                                               sizeof(tlv_system_header_t) - sizeof(uint16_t));

    return TLV_OK;
}

static int system_header_load(void)
{
    if (!g_tlv_ctx.header)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 从FRAM读取Header
    int ret = tlv_port_fram_read(TLV_HEADER_ADDR, g_tlv_ctx.header,
                                 sizeof(tlv_system_header_t));
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 校验Header
    return system_header_verify();
}

static int system_header_save(void)
{
    if (!g_tlv_ctx.header)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 重新计算CRC
    g_tlv_ctx.header->header_crc16 = tlv_crc16(g_tlv_ctx.header,
                                               sizeof(tlv_system_header_t) - sizeof(uint16_t));

    // 写入FRAM
    return tlv_port_fram_write(TLV_HEADER_ADDR, g_tlv_ctx.header,
                               sizeof(tlv_system_header_t));
}

static int system_header_verify(void)
{
    if (!g_tlv_ctx.header)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 检查魔数
    if (g_tlv_ctx.header->magic != TLV_SYSTEM_MAGIC)
    {
        return TLV_ERROR_CORRUPTED;
    }

    // 校验CRC16
    uint16_t calc_crc = tlv_crc16(g_tlv_ctx.header,
                                  sizeof(tlv_system_header_t) - sizeof(uint16_t));

    if (calc_crc != g_tlv_ctx.header->header_crc16)
    {
        return TLV_ERROR_CRC_FAILED;
    }

    return TLV_OK;
}

static uint32_t allocate_space(uint32_t size)
{
    if (!g_tlv_ctx.header)
    {
        return 0;
    }

    uint32_t addr = g_tlv_ctx.header->next_free_addr;
    uint32_t end_addr = TLV_DATA_ADDR + g_tlv_ctx.header->data_region_size;

    if (addr + size > end_addr)
    {
        return 0;
    }

    g_tlv_ctx.header->next_free_addr += size;
    g_tlv_ctx.header->used_space += size;
    g_tlv_ctx.header->free_space -= size;

    return addr;
}

static int write_data_block(uint16_t tag, const void *data, uint16_t len, uint32_t addr)
{
    const tlv_meta_const_t *meta = get_meta(tag);

    // 构建数据块Header
    tlv_data_block_header_t header = {0};
    header.tag = tag;
    header.length = len;
    header.version = meta ? meta->version : 1;
    header.flags = 0;
    header.timestamp = tlv_port_get_timestamp_s();

    // 读取旧的write_count（如果存在）
    tlv_data_block_header_t old_header;
    if (tlv_port_fram_read(addr, &old_header, sizeof(old_header)) == TLV_OK &&
        old_header.tag == tag)
    {
        header.write_count = old_header.write_count + 1;
    }
    else
    {
        header.write_count = 1;
    }

    // 计算CRC16（Header + Data）
    uint16_t crc = tlv_crc16_init();
    crc = tlv_crc16_update(crc, &header, sizeof(header));
    crc = tlv_crc16_update(crc, data, len);
    crc = tlv_crc16_final(crc);

    // 写入FRAM：Header -> Data -> CRC16
    int ret = tlv_port_fram_write(addr, &header, sizeof(header));
    if (ret != TLV_OK)
    {
        return ret;
    }

    ret = tlv_port_fram_write(addr + sizeof(header), data, len);
    if (ret != TLV_OK)
    {
        return ret;
    }

    ret = tlv_port_fram_write(addr + sizeof(header) + len, &crc, sizeof(crc));
    return ret;
}

static int read_data_block(uint32_t addr, void *buf, uint16_t *len)
{
    // 读取Header
    tlv_data_block_header_t header;
    int ret = tlv_port_fram_read(addr, &header, sizeof(header));
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 检查长度
    if (header.length > *len)
    {
        return TLV_ERROR_NO_MEMORY;
    }

    // 读取数据
    ret = tlv_port_fram_read(addr + sizeof(header), buf, header.length);
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 读取CRC16
    uint16_t stored_crc;
    ret = tlv_port_fram_read(addr + sizeof(header) + header.length,
                             &stored_crc, sizeof(stored_crc));
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 校验CRC16
    uint16_t calc_crc = tlv_crc16_init();
    calc_crc = tlv_crc16_update(calc_crc, &header, sizeof(header));
    calc_crc = tlv_crc16_update(calc_crc, buf, header.length);
    calc_crc = tlv_crc16_final(calc_crc);

    if (calc_crc != stored_crc)
    {
        return TLV_ERROR_CRC_FAILED;
    }

    *len = header.length;
    return TLV_OK;
}

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
