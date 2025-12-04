/**
 * @file tlv_core.c
 * @brief TLV FRAM存储系统核心读写逻辑（裸机简化版）
 */

#include "tlv_fram.h"
#include "tlv_index.h"
#include "tlv_port.h"
#include "tlv_utils.h"
#include "tlv_meta_table.h"
#include "tlv_migration.h"
#include <string.h>
#include "Main.h"

/* ===================== TLV系统头结构体大小检查 ======================== */

STATIC_CHECK_SIZE(tlv_system_header_t, 128);
STATIC_CHECK_SIZE(tlv_data_block_header_t, 14);
STATIC_CHECK_SIZE(tlv_index_entry_t, 8);
STATIC_CHECK_SIZE(tlv_index_table_t, 2050);
/* ============================ 全局静态变量 ============================ */

tlv_context_t g_tlv_ctx = {0};

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
static int tlv_backup_all_internal(void);


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
    g_tlv_ctx.meta_table_size = TLV_META_MAP_SIZE;

    // 清零header和index_table
    memset(&g_static_header, 0, sizeof(g_static_header));
    memset(&g_static_index, 0, sizeof(g_static_index));

    // 尝试加载系统Header
    ret = system_header_load();
    if (ret == TLV_OK)
    {
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
    // 检查并设置格式化状态
    if (g_tlv_ctx.state == TLV_STATE_ERROR)
    {
        // 允许格式化错误状态
    }
    else if (g_tlv_ctx.state == TLV_STATE_INITIALIZED)
    {
        // 警告：会丢失所有数据
#if TLV_DEBUG
        tlv_printf("WARNING: Formatting initialized system - all data will be lost!\n");
#endif
    }

    // 设置格式化中状态
    tlv_state_t old_state = g_tlv_ctx.state;
    g_tlv_ctx.state = TLV_STATE_FORMATTING;

    // 确保指针已设置
    if (!g_tlv_ctx.header || !g_tlv_ctx.index_table)
    {
        // 如果还未初始化，先设置指针
        g_tlv_ctx.header = &g_static_header;
        g_tlv_ctx.index_table = &g_static_index;
    }

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
    ret = tlv_backup_all_internal();
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 设置状态为已格式化
    g_tlv_ctx.state = TLV_STATE_FORMATTED;

    return TLV_OK;
}

tlv_state_t tlv_get_state(void)
{
    return g_tlv_ctx.state;
}

/* ============================ 数据操作API实现 ============================ */
/**
 * @brief 写入TLV数据
 * @param tag Tag值
 * @param data 数据指针
 * @param len 数据长度
 * @return 实际写入长度，负数表示错误
 */
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
    uint32_t old_block_size = 0;
    uint32_t new_block_size = sizeof(tlv_data_block_header_t) + len + sizeof(uint16_t);
    bool is_update = false;      // 是否是更新操作
    bool need_add_index = false; // 是否需要新增索引
    // 判断索引表是否有空闲的槽位
    bool has_free_slot = g_tlv_ctx.header->tag_count < TLV_MAX_TAG_COUNT;

    if (index && (index->flags & TLV_FLAG_VALID))
    {
        // 读取旧Header
        tlv_data_block_header_t old_header;
        ret = tlv_port_fram_read(index->data_addr, &old_header, sizeof(old_header));
        if (ret != TLV_OK)
        {
            return ret;
        }

        old_block_size = sizeof(tlv_data_block_header_t) + old_header.length + sizeof(uint16_t);

        if (new_block_size <= old_block_size)
        {
            // 数据需要更新
            is_update = true;

            // 原地更新,新数据更小或相等
            target_addr = index->data_addr;

            // 更新 used_space：减少旧的,增加新的
            g_tlv_ctx.header->used_space -= old_block_size;
            g_tlv_ctx.header->used_space += new_block_size;
        }
        else // 数据大小变大,需要重新分配新的槽位
        {
            // 索引表满,不分配空间
            if (!has_free_slot)
            {
                return TLV_ERROR_NO_MEMORY;
            }

            // 分配新空间
            target_addr = allocate_space(new_block_size);
            if (target_addr == 0)
            {
                return TLV_ERROR_NO_SPACE;
            }

            // 需要新增索引
            need_add_index = true;
        }
    }
    else // Tag不存在，新写入操作
    {
        // 索引表满,不分配空间
        if (!has_free_slot)
        {
            return TLV_ERROR_NO_MEMORY;
        }

        // 新Tag,分配空间
        target_addr = allocate_space(new_block_size);
        if (target_addr == 0)
        {
            return TLV_ERROR_NO_SPACE;
        }

        // 需要新增索引
        need_add_index = true;
    }

    // 写入数据块
    ret = write_data_block(tag, data, len, target_addr);
    if (ret != TLV_OK)
    {
        // ========== 写入失败，回滚 ==========
#if TLV_DEBUG
        tlv_printf("write_data_block failed: %d\n", ret);
#endif
        // 写入失败，回滚 used_space,分配资源的next_free就不回收了，分配的内存作为碎片,等待碎片整理
        g_tlv_ctx.header->fragment_count++;
        g_tlv_ctx.header->fragment_size += new_block_size;

        if (is_update)
        {
            // 恢复旧值
            if (new_block_size <= old_block_size)
            {
                g_tlv_ctx.header->used_space -= new_block_size;
                g_tlv_ctx.header->used_space += old_block_size;
            }
            else
            {
                g_tlv_ctx.header->used_space -= new_block_size;
            }
        }
        else
        {
            g_tlv_ctx.header->used_space -= new_block_size;
        }
        return ret;
    }

    // 新增索引
    if (need_add_index)
    {
        // ---------- 需要新增索引 ----------
        if (index && (index->flags & TLV_FLAG_VALID)) // 原有的索引块废弃
        {
            // 标记旧块为脏,旧块失效,旧块索引不使用
            index->flags = TLV_FLAG_DIRTY;
            index = NULL;
			
			// 旧块不再有效，减少 used_space
			g_tlv_ctx.header->used_space -= old_block_size;

            // 统计碎片数量及碎片大小
            g_tlv_ctx.header->fragment_count++;
            g_tlv_ctx.header->fragment_size += old_block_size;
        }

        index = tlv_index_add(&g_tlv_ctx, tag, target_addr);
        if (!index)
        {
            // 这不应该发生（我们已经检查过）
            // 但如果发生，需要回滚
#if TLV_DEBUG
            tlv_printf("CRITICAL: Index add failed unexpectedly!\n");
#endif
            return TLV_ERROR_NO_MEMORY;
        }
    }
    else // 更新索引
    {
        tlv_index_update(&g_tlv_ctx, tag, target_addr);
    }

    // 立即保存索引表到FRAM (保证一致性)
    ret = tlv_index_save(&g_tlv_ctx);
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 更新统计
    g_tlv_ctx.header->total_writes++;
    g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();

    ret = system_header_save();
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 检查是否自动整理碎片
#if TLV_AUTO_CLEAN_FRAGEMENT
    if (g_tlv_ctx.header->fragment_count > TLV_AUTO_CLEAN_FRAGEMENT_NUMBER ||
        g_tlv_ctx.header->fragment_size > TLV_AUTO_CLEAN_FRAGEMENT_SIZE)
    {
        ret = tlv_defragment();
        if (ret != TLV_OK)
        {
#if TLV_DEBUG
            tlv_printf("CRITICAL: auto defragment failed unexpectedly!\n");
#endif
            return ret;
        }
    }
#endif

    return len; // 返回写入长度
}

/**
 * @brief 读取TLV数据
 * @param tag Tag值
 * @param buf 输出缓冲区
 * @param len 缓冲区大小（输入），实际读取大小（输出）
 * @return 0: 成功, 其他: 错误码
 */
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

    uint16_t output_size = *len; // 保存输出缓冲区大小

    // 查找索引
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, tag);
    if (!index || !(index->flags & TLV_FLAG_VALID))
    {
        return TLV_ERROR_NOT_FOUND;
    }

    // 读取数据块
    uint16_t read_len = output_size;
    int ret = read_data_block(index->data_addr, buf, &read_len);
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 读取时惰性迁移
#if (TLV_ENABLE_MIGRATION && TLV_LAZY_MIGRATE_ON_READ)
    // 查找元数据获取期望版本
    const tlv_meta_const_t *meta = get_meta(tag);
    if (meta && index->version < meta->version)
    {
        // 首先检查缓冲区是否足够大，否则无法迁移
        if (meta->max_length > output_size)
        {
#if TLV_DEBUG
            tlv_printf("ERROR: Buffer too small for migration\n");
            tlv_printf("  Need: %u, Have: %u\n", meta->max_length, output_size);
#endif
            *len = read_len; //返回旧版本数据
			return TLV_OK;
        }

// 需要升级
#if TLV_DEBUG
        tlv_printf("Migrating tag 0x%04X on read: v%u -> v%u\n",
                   tag, index->version, meta->version);
#endif
        // 迁移数据（原地，在 buf 中）
        uint16_t old_len = read_len; // 旧数据长度
        uint16_t new_len = 0;
        ret = tlv_migrate_tag(tag, buf, old_len, &new_len, output_size,
                              index->version);
        if (ret == TLV_OK)
        {
            // 迁移成功，写回FRAM
            int write_ret = tlv_write(tag, buf, new_len);
            if (write_ret < 0)
            {
                // 写回失败，警告但仍返回迁移后的数据
#if TLV_DEBUG
                tlv_printf("WARNING: Migration successful but write back failed (err: %d)\n",
                           write_ret);
#endif
            }

            // 更新返回的数据长度
            read_len = new_len;
            // 索引版本已在 tlv_write() 中更新
        }
        else
        {
            // 迁移失败，返回错误（数据可能不可用）
#if TLV_DEBUG
            tlv_printf("ERROR: Migration failed (err: %d)\n", ret);
#endif
            return ret;
        }
    }
#endif

    *len = read_len;
    return TLV_OK;
}

/**
 * @brief 删除TLV数据
 * @param tag Tag值
 * @return 0: 成功, 其他: 错误码
 */
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
	
	// 先获取索引信息，计算块大小
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, tag);
    if (!index || !(index->flags & TLV_FLAG_VALID)) {
        return TLV_ERROR_NOT_FOUND;
    }
 
    // 读取数据块大小
    tlv_data_block_header_t header;
    int ret = tlv_port_fram_read(index->data_addr, &header, sizeof(header));
    if (ret == TLV_OK) {
        uint32_t block_size = sizeof(header) + header.length + sizeof(uint16_t);
        
        // 更新统计
        g_tlv_ctx.header->used_space -= block_size;
        g_tlv_ctx.header->fragment_count++;
        g_tlv_ctx.header->fragment_size += block_size;
    }
	

    // 删除索引
    ret = tlv_index_remove(&g_tlv_ctx, tag);
    if (ret == TLV_OK)
    {
        // 更新统计
        g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();

        // 删除操作必须保存索引和Header (避免幽灵数据)
        tlv_index_save(&g_tlv_ctx);
        system_header_save();
    }

    return ret;
}

/**
 * @brief 强制保存所有挂起的更改
 * @return 0: 成功, 其他: 错误码
 */
int tlv_flush(void)
{
    int ret = tlv_index_save(&g_tlv_ctx);
    if (ret != TLV_OK)
    {
        return ret;
    }

    return system_header_save();
}

/**
 * @brief 检查Tag是否存在
 * @param tag Tag值
 * @return true: 存在, false: 不存在
 */
bool tlv_exists(uint16_t tag)
{
    if (tag == 0 || g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return false;
    }

    tlv_index_entry_t *entry = tlv_index_find(&g_tlv_ctx, tag);
    return (entry != NULL && (entry->flags & TLV_FLAG_VALID));
}

/**
 * @brief 获取Tag数据长度
 * @param tag Tag值
 * @param len 输出长度
 * @return 0: 成功, 其他: 错误码
 */
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
    if (!index || !(index->flags & TLV_FLAG_VALID))
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
/**
 * @brief 批量读取
 * @param tags Tag数组
 * @param count Tag数量
 * @param buffers 缓冲区数组
 * @param lengths 长度数组
 * @return 成功读取的数量
 */
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
/**
 * @brief 批量写入
 * @param tags Tag数组
 * @param count Tag数量
 * @param datas 数据数组
 * @param lengths 长度数组
 * @return 成功写入的数量
 */
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

    return success_count;
}

/* ============================ 查询与统计API实现 ============================ */
/**
 * @brief 获取统计信息
 * @param stats 统计信息输出
 * @return 0: 成功, 其他: 错误码
 */
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
/**
 * @brief 遍历所有Tag
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 遍历的Tag数量
 */
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

/* ============================ 私有函数实现 ============================ */
/**
 * @brief 原地排序索引表（按地址）
 */
static void sort_index_table_inplace(void)
{
    // 1. 压缩：移动所有有效项到前面
    int write_idx = 0;
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        tlv_index_entry_t *entry = &g_tlv_ctx.index_table->entries[i];

        if (entry->tag != 0 && (entry->flags & TLV_FLAG_VALID))
        {
            if (i != write_idx)
            {
                g_tlv_ctx.index_table->entries[write_idx] = *entry;
            }
            write_idx++;
        }
    }

    int total_valid = write_idx;

    if (total_valid == 0)
    {
        return;
    }

    // 2. 对压缩后的有效项排序（选择排序）
    for (int i = 0; i < total_valid - 1; i++)
    {
        int min_idx = i;

        // 找到剩余最小地址
        for (int j = i + 1; j < total_valid; j++)
        {
            if (g_tlv_ctx.index_table->entries[j].data_addr <
                g_tlv_ctx.index_table->entries[min_idx].data_addr)
            {
                min_idx = j;
            }
        }

        // 交换
        if (min_idx != i)
        {
            tlv_index_entry_t temp = g_tlv_ctx.index_table->entries[i];
            g_tlv_ctx.index_table->entries[i] = g_tlv_ctx.index_table->entries[min_idx];
            g_tlv_ctx.index_table->entries[min_idx] = temp;
        }
    }

    // 3. 清空剩余槽位
    for (int i = total_valid; i < TLV_MAX_TAG_COUNT; i++)
    {
        memset(&g_tlv_ctx.index_table->entries[i], 0, sizeof(tlv_index_entry_t));
    }
}

/* ============================ 维护管理API实现 ============================ */
/**
 * @brief 碎片整理
 * @return 0: 成功, 其他: 错误码
 * @note 清除无效的tag，按地址排序整理内存及索引
 */

int tlv_defragment(void)
{
    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        return TLV_ERROR;
    }

    int ret = TLV_OK;
    uint32_t write_pos = TLV_DATA_ADDR;
    uint32_t total_used = 0;
    uint32_t processed = 0;
    uint32_t total_tags = 0;

    // 统计有效tag数量
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        tlv_index_entry_t *entry = &g_tlv_ctx.index_table->entries[i];
        if (entry->tag != 0 && (entry->flags & TLV_FLAG_VALID))
        {
            total_tags++;
        }
    }

#if TLV_DEBUG
    tlv_printf("Valid tags: %lu\n", (unsigned long)total_tags);
#endif

    if (total_tags == 0)
    {
        // 没有有效数据，清空
        ret = system_header_init();
        if (ret != TLV_OK)
        {
            return ret;
        }
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
        ret = tlv_backup_all_internal();
        return ret;
    }

    // 原地排序索引表
    sort_index_table_inplace();

    total_tags = 0;
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        if (g_tlv_ctx.index_table->entries[i].tag != 0 &&
            (g_tlv_ctx.index_table->entries[i].flags & TLV_FLAG_VALID))
        {
            total_tags++;
        }
        else
        {
            break; // 已压缩，后面都是空的
        }
    }

    // 遍历所有有效Tag,移动数据块
    for (int i = 0; i < total_tags; i++)
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

        uint32_t block_size = sizeof(header) + header.length + sizeof(uint16_t);

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
        else if (entry->flags & TLV_FLAG_DIRTY)
        {
            entry->flags &= ~TLV_FLAG_DIRTY;
        }

        write_pos += block_size;
        total_used += block_size;
    }

    // 更新系统Header
    uint32_t region_size = TLV_BACKUP_ADDR - TLV_DATA_ADDR;
    uint32_t new_allocated = total_used;
    uint32_t new_free_space = region_size - new_allocated;

    g_tlv_ctx.header->data_region_start = TLV_DATA_ADDR;
    g_tlv_ctx.header->data_region_size = region_size;
    g_tlv_ctx.header->tag_count = total_tags;
    g_tlv_ctx.header->next_free_addr = write_pos;
    g_tlv_ctx.header->free_space = new_free_space;
    g_tlv_ctx.header->used_space = new_allocated;
    g_tlv_ctx.header->fragment_count = 0;
    g_tlv_ctx.header->fragment_size = 0;

    // 保存更新
    tlv_index_save(&g_tlv_ctx);
    system_header_save();

    return ret;
}

/**
 * @brief 校验所有数据
 * @param corrupted_count 输出损坏数量
 * @return 0: 成功, 其他: 错误码
 */
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

/* ============================ 公开函数：对外备份接口（带状态检查）============================ */
/**
 * @brief 备份所有数据到备份区
 * @return 0: 成功, 其他: 错误码
 */
int tlv_backup_all(void)
{
    // 对外接口：严格的状态检查
    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED &&
        g_tlv_ctx.state != TLV_STATE_FORMATTED)
    {
        return TLV_ERROR;
    }

    // 调用内部函数
    int ret = tlv_backup_all_internal();

    if (ret == TLV_OK)
    {
        // 更新备份时间
        g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();
        system_header_save();
    }

    return ret;
}
/**
 * @brief 从备份区恢复数据
 * @return 0: 成功, 其他: 错误码
 */
int tlv_restore_from_backup(void)
{
    if (!g_tlv_ctx.header || !g_tlv_ctx.index_table)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    int ret;
    tlv_system_header_t backup_header;
    // 读取备份Header
    ret = tlv_port_fram_read(TLV_BACKUP_ADDR, &backup_header,
                             sizeof(backup_header));
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 验证备份Header
    if (backup_header.magic != TLV_SYSTEM_MAGIC)
    {
#if TLV_DEBUG
        tlv_printf("ERROR: Backup header magic invalid (0x%08lX)\n",
                   (unsigned long)backup_header.magic);
#endif
        return TLV_ERROR_CORRUPTED;
    }

    // 验证CRC
    uint16_t calc_crc = tlv_crc16(&backup_header,
                                  sizeof(backup_header) - sizeof(uint16_t));
    if (calc_crc != backup_header.header_crc16)
    {
#if TLV_DEBUG
        tlv_printf("ERROR: Backup header CRC mismatch\n");
#endif
        return TLV_ERROR_CORRUPTED;
    }

    // 验证数据合理性
    if (backup_header.data_region_size !=
        (TLV_BACKUP_ADDR - TLV_DATA_ADDR))
    {
#if TLV_DEBUG
        tlv_printf("ERROR: Backup header data size mismatch\n");
#endif
        return TLV_ERROR_CORRUPTED;
    }

    // 计算备份区大小
    uint32_t backup_size = TLV_DATA_REGION_SIZE;

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
/**
 * @brief 获取可用空间
 * @param free_space 输出可用空间
 * @return 0: 成功, 其他: 错误码
 */
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
/**
 * @brief 获取已用空间
 * @param used_space 输出已用空间
 * @return 0: 成功, 其他: 错误码
 */
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
/**
 * @brief 计算碎片化程度
 * @param fragmentation_percent 输出碎片化百分比
 * @return 0: 成功, 其他: 错误码
 */
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
    g_tlv_ctx.header->version = TLV_SYSTEM_VERSION;
    g_tlv_ctx.header->tag_count = 0;
    g_tlv_ctx.header->data_region_start = TLV_DATA_ADDR;
    g_tlv_ctx.header->data_region_size = TLV_BACKUP_ADDR - TLV_DATA_ADDR;
    g_tlv_ctx.header->next_free_addr = TLV_DATA_ADDR;
    g_tlv_ctx.header->total_writes = 0;
    g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();
    g_tlv_ctx.header->free_space = g_tlv_ctx.header->data_region_size;
    g_tlv_ctx.header->used_space = 0;
    g_tlv_ctx.header->fragment_count = 0;

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

    // 检查版本兼容性，主版本必须相同，当前子版本大于文件系统版本
    if (!tlv_version_compatible(TLV_SYSTEM_VERSION, g_tlv_ctx.header->version))
    {
        return TLV_ERROR_VERSION;
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
    ret = tlv_port_fram_read(addr + sizeof(header) + header.length, &stored_crc, sizeof(stored_crc));
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

/* ============================ 私有函数：内部备份（无状态检查）============================ */

static int tlv_backup_all_internal(void)
{
    if (!g_tlv_ctx.header || !g_tlv_ctx.index_table)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    uint32_t backup_size = TLV_DATA_REGION_SIZE;
    uint32_t offset = 0;
    int ret = TLV_OK;

    // 分批备份
    while (offset < backup_size)
    {
        uint32_t chunk_size = (backup_size - offset > TLV_BUFFER_SIZE) ? TLV_BUFFER_SIZE : (backup_size - offset);

        // 读取管理区
        ret = tlv_port_fram_read(TLV_HEADER_ADDR + offset,
                                 g_tlv_ctx.static_buffer,
                                 chunk_size);
        if (ret != TLV_OK)
        {
            return ret;
        }

        // 写入备份区
        ret = tlv_port_fram_write(TLV_BACKUP_ADDR + offset,
                                  g_tlv_ctx.static_buffer,
                                  chunk_size);
        if (ret != TLV_OK)
        {
            return ret;
        }

        offset += chunk_size;
    }

    return TLV_OK;
}
