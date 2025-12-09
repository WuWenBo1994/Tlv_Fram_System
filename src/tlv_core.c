/**
 * @file tlv_core.c
 * @brief TLV FRAM存储系统核心读写逻辑
 */

#include "tlv_fram.h"
#include "tlv_index.h"
#include "tlv_port.h"
#include "tlv_utils.h"
#include "tlv_meta_table.h"
#include "tlv_migration.h"

/* ============================ 全局静态变量 ============================ */
/* 存储系统上下文 */
static tlv_context_t g_tlv_ctx = {0};

/* 静态分配的内存（替代malloc） */
static tlv_system_header_t g_static_header;
static tlv_index_table_t g_static_index;

/* 流式操作上下文 */
static tlv_stream_context_t g_stream_ctx = {0};

/* 错误上下文 */
static tlv_error_context_t g_last_error = {0};
/* ============================ 错误处理宏 ============================ */

#if TLV_DEBUG
/**
 * @brief 记录错误并返回
 * @param err 错误码
 * @param tag_val 相关的 tag（可选，传0表示无关）
 */
#define TLV_SET_ERROR(err, tag_val) \
    tlv_set_last_error((err), (tag_val), __LINE__, __func__)

#define TLV_TAG_ERROR(err) \
    tlv_set_last_error((err), (tag), __LINE__, __func__)

#else
/**
 * @brief 精简版（不记录行号和函数名）
 */
#define TLV_SET_ERROR(err, tag_val) \
    tlv_set_last_error((err), (tag_val), 0, NULL)
#define TLV_TAG_ERROR(err) \
    tlv_set_last_error((err), (tag), 0, NULL)
#endif

/* ============================ 私有函数声明 ============================ */

static int system_header_init(void);
static int system_header_load(void);
static int system_header_save(void);
static int system_header_verify(void);
static uint32_t allocate_space(uint32_t size);
static int write_data_block(uint16_t tag, const void *data, uint16_t len, uint32_t addr);
static const tlv_meta_const_t *get_meta(uint16_t tag);
static int tlv_backup_all_internal(void);
static void transaction_snapshot_create(void);
static void transaction_snapshot_rollback(void);
static void transaction_snapshot_commit(void);
static void increase_used_space(uint32_t size);
static void reduce_used_space(uint32_t size);
static int tlv_set_last_error(int error_code, uint16_t tag, uint32_t line, const char *function);

/* ============================ 版本API实现 ============================ */
const char *tlv_get_version(void)
{
    return TLV_FILE_SYSTEM_VERSION;
}

const tlv_context_t *tlv_get_context(void)
{
    return &g_tlv_ctx;
}

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
    g_tlv_ctx.meta_table = tlv_get_meta_table();
    g_tlv_ctx.meta_table_size = tlv_get_meta_table_size();

    // 初始化快照
    memset(&g_tlv_ctx.snapshot, 0, sizeof(g_tlv_ctx.snapshot));
    g_tlv_ctx.snapshot.is_active = false;

    // 清零数据
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
            // 索引表损坏,尝试从备份恢复
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
        tlv_printf("WARNING: Formatting initialized system - all data will be lost!\n");
    }

    // 保存格式化原来的状态
    tlv_state_t old_state = g_tlv_ctx.state;

    // 确保指针已设置
    if (!g_tlv_ctx.header || !g_tlv_ctx.index_table)
    {
        // 如果还未初始化,先设置指针
        g_tlv_ctx.header = &g_static_header;
        g_tlv_ctx.index_table = &g_static_index;
    }

    // 初始化系统Header
    int ret = system_header_init();
    if (ret != TLV_OK)
    {
        goto error_exit;
    }

    if (magic != 0)
    {
        g_tlv_ctx.header->magic = magic;
    }

    // 初始化索引系统
    ret = tlv_index_init(&g_tlv_ctx);
    if (ret != TLV_OK)
    {
        goto error_exit;
    }

    // 保存Header和索引表
    ret = system_header_save();
    if (ret != TLV_OK)
    {
        goto error_exit;
    }

    ret = tlv_index_save(&g_tlv_ctx);
    if (ret != TLV_OK)
    {
        goto error_exit;
    }

    // 备份管理区
    ret = tlv_backup_all_internal();
    if (ret != TLV_OK)
    {
        goto error_exit;
    }

    // 设置状态为已格式化
    g_tlv_ctx.state = TLV_STATE_FORMATTED;
    tlv_printf("Format completed. Please call tlv_init() before operations.\n");
    return TLV_OK;

error_exit:
    // 设置状态为错误
    g_tlv_ctx.state = TLV_STATE_ERROR;
    tlv_printf("Format failed. error: %d.\n", ret);
    return ret;
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
 * @return 0: 成功, 其他: 错误码
 * @note 数据的写入过程是数据先落盘,但索引是提交点,只有索引落盘了,数据才可见
 */
int tlv_write(uint16_t tag, const void *data, uint16_t len)
{
    if (!data || len == 0 || tag == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    if (!g_tlv_ctx.header || !g_tlv_ctx.index_table)
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

    // 创建事务快照
    transaction_snapshot_create();
    // 查找现有索引
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, tag);
    uint32_t target_addr;
    uint32_t old_block_size = 0;
    uint32_t new_block_size = TLV_BLOCK_SIZE(len);
    bool is_update = false;                                               // 是否是更新操作
    bool need_add_index = false;                                          // 是否需要新增索引
    bool has_free_slot = g_tlv_ctx.header->tag_count < TLV_MAX_TAG_COUNT; // 判断索引表是否有空闲的槽位

    if (index && (index->flags & TLV_FLAG_VALID))
    {
        // 读取旧Header
        tlv_data_block_header_t old_header;
        ret = tlv_port_fram_read(index->data_addr, &old_header, sizeof(old_header));
        if (ret != TLV_OK)
        {
            return ret;
        }

        old_block_size = TLV_BLOCK_SIZE(old_header.length);
        if (new_block_size <= old_block_size)
        {
            // 数据需要更新
            is_update = true;

            // 原地更新,新数据更小或相等
            target_addr = index->data_addr;

            // 更新 used_space：减少旧的,增加新的
            reduce_used_space(old_block_size);
            increase_used_space(new_block_size);
        }
        else // 数据大小变大,需要重新分配新的槽位
        {
            // 索引表满,不分配空间
            if (!has_free_slot)
            {
                return TLV_ERROR_NO_INDEX_SPACE;
            }

            // 分配新空间
            target_addr = allocate_space(new_block_size);
            if (target_addr == 0)
            {
                return TLV_ERROR_NO_MEMORY_SPACE;
            }

            // 需要新增索引
            need_add_index = true;
        }
    }
    else // Tag不存在,新写入操作
    {
        // 索引表满,不分配空间
        if (!has_free_slot)
        {
            return TLV_ERROR_NO_INDEX_SPACE;
        }

        // 新Tag,分配空间
        target_addr = allocate_space(new_block_size);
        if (target_addr == 0)
        {
            return TLV_ERROR_NO_MEMORY_SPACE;
        }

        // 需要新增索引
        need_add_index = true;
    }

    // 写入数据块
    ret = write_data_block(tag, data, len, target_addr);
    if (ret != TLV_OK)
    {
        // ========== 写入失败,回滚 ==========
        tlv_printf("write_data_block failed: %d\n", ret);

        // 写入失败,回滚所有状态,包括nextfree,避免未写入成功的内存成为碎片
        transaction_snapshot_rollback();
        // 保存回滚后的header
        ret = system_header_save();

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

            // 旧块不再有效,减少 used_space
            reduce_used_space(old_block_size);

            // 统计碎片数量及碎片大小
            g_tlv_ctx.header->fragment_count++;
            g_tlv_ctx.header->fragment_size += old_block_size;
        }

        tlv_index_entry_t *new_index = tlv_index_add(&g_tlv_ctx, tag, target_addr);
        if (!new_index)
        {
            // 这不应该发生（我们已经检查过）,且可以分配脏块给新索引使用
            tlv_printf("CRITICAL: Index add failed unexpectedly!\n");
            TLV_ASSERT(false);
            return TLV_ERROR_NO_INDEX_SPACE;
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

    // ========== 提交事务 ==========
    transaction_snapshot_commit();

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
    uint32_t fragUsagePercent = 0;
    ret = tlv_calculate_fragmentation(&fragUsagePercent);
    if (ret == TLV_OK)
    {
        if (fragUsagePercent >= TLV_AUTO_DEFRAG_THRESHOLD)
        {
            ret = tlv_defragment();
            if (ret != TLV_OK)
            {
                tlv_printf("CRITICAL: auto defragment failed unexpectedly!\n");
                return ret;
            }
        }
    }
#endif

    return TLV_OK;
}

/**
 * @brief 读取TLV数据
 * @param tag Tag值
 * @param buf 输出缓冲区
 * @param len 缓冲区大小（输入）,实际读取大小（输出）
 * @return 0: 成功, 其他: 错误码
 * @note tlv_read 读取时不检查meta表,因为如果固件升级导致meta表变化也可以从内存中读取出遗留数据,便于向后兼容
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
        // 执行到这里,输出缓冲区一定能承载旧数据,否则之前的read_data_block就会报错 TLV_ERROR_NO_BUFFER_MEMORY
        // 需要升级
        tlv_printf("Migrating tag 0x%04X on read: v%u -> v%u\n",
                   tag, index->version, meta->version);
        // 迁移数据(原地,在 buf 中)
        // output_size传递给tlv_migrate_tag,由迁移函数判断缓冲区是否足够迁移
        uint16_t old_len = read_len; // 旧数据长度
        uint16_t new_len = 0;
        ret = tlv_migrate_tag(&g_tlv_ctx, tag, buf, old_len, &new_len, output_size,
                              index->version);
        if (ret == TLV_OK)
        {
            // 迁移成功,写回FRAM
            int write_ret = tlv_write(tag, buf, new_len);
            if (write_ret < 0)
            {
                // 写回失败,警告但仍返回迁移后的数据
                tlv_printf("WARNING: Migration successful but write back failed (err: %d)\n",
                           write_ret);
            }

            // 更新返回的数据长度
            read_len = new_len;
            // 索引版本已在 tlv_write() 中更新
        }
        else if (ret == TLV_ERROR_NO_BUFFER_MEMORY)
        {
            // 缓冲区不够,告知用户需要的大小
            *len = new_len; // 迁移函数应设置需要的大小
            tlv_printf("ERROR: Buffer too small for migration\n");
            tlv_printf("  Need: %u, Have: %u\n", new_len, output_size);
            return ret;
        }
        else
        {
            // 其他迁移错误,降级返回旧数据
            tlv_printf("WARNING: Migration failed (err: %d), returning old data\n", ret);

            read_len = output_size;
            int reread_ret = read_data_block(index->data_addr, buf, &read_len);
            if (reread_ret != TLV_OK)
            {
                // 重新读取失败
                tlv_printf("ERROR: Re-read failed (err: %d), data corrupted!\n", reread_ret);
                return reread_ret;
            }
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

    // 先获取索引信息,计算块大小
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, tag);
    if (!index || !(index->flags & TLV_FLAG_VALID))
    {
        return TLV_ERROR_NOT_FOUND;
    }

    // 读取数据块大小
    tlv_data_block_header_t header;
    int ret = tlv_port_fram_read(index->data_addr, &header, sizeof(header));
    if (ret == TLV_OK)
    {
        uint32_t block_size = sizeof(header) + header.length + sizeof(uint16_t);

        // 更新统计
        reduce_used_space(block_size);
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
        if (write_idx >= TLV_MAX_TAG_COUNT)
            break;
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

    if (total_valid <= 0 || total_valid >= TLV_MAX_TAG_COUNT)
    {
        return;
    }

// 2. 对压缩后的有效项排序
// 插入排序,原有的排序是按地址排列基本有序的,对于近乎有序的排列接近O(n),逆序最差是O(n^2)
#if 1
    for (int i = 1; i < total_valid; i++)
    {
        tlv_index_entry_t entry = g_tlv_ctx.index_table->entries[i];
        int j = i - 1;

        // 和有序区最后一个元素比较,如果比有序区最后一个元素小,则插入有序区中,否则就在当前位置
        while (j >= 0 && g_tlv_ctx.index_table->entries[j].data_addr > entry.data_addr)
        {
            g_tlv_ctx.index_table->entries[j + 1] = g_tlv_ctx.index_table->entries[j];
            j--;
        }
        g_tlv_ctx.index_table->entries[j + 1] = entry;
    }
#else
    // 选择排序 O(n^2)
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
#endif
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
 * @note 清除无效的tag,按地址排序整理内存及索引
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

    tlv_printf("Valid tags: %lu\n", (unsigned long)total_tags);

    if (total_tags == 0)
    {
        // 没有有效数据,清空
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

    // 1. 先备份当前管理区到备份区
    ret = tlv_backup_all_internal();
    if (ret != TLV_OK)
    {
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
            break; // 已压缩,后面都是空的
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

        // 如果不在紧凑位置,移动它
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
    uint32_t new_free_space = (region_size > new_allocated) ? (region_size - new_allocated) : 0;

    g_tlv_ctx.header->data_region_start = TLV_DATA_ADDR;
    g_tlv_ctx.header->data_region_size = region_size;
    g_tlv_ctx.header->tag_count = total_tags;
    g_tlv_ctx.header->next_free_addr = write_pos;
    g_tlv_ctx.header->free_space = new_free_space;
    g_tlv_ctx.header->used_space = new_allocated;
    g_tlv_ctx.header->fragment_count = 0;
    g_tlv_ctx.header->fragment_size = 0;

    // 保存更新
    ret = tlv_index_save(&g_tlv_ctx);
    if (ret != TLV_OK)
    {
        return ret;
    }
    ret = system_header_save();
    if (ret != TLV_OK)
    {
        return ret;
    }
    // 整理完成后备份区域同步
    ret = tlv_backup_all_internal();
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
        tlv_printf("ERROR: Backup header magic invalid (0x%08lX)\n",
                   (unsigned long)backup_header.magic);
        return TLV_ERROR_CORRUPTED;
    }

    // 验证CRC
    uint16_t calc_crc = tlv_crc16(&backup_header,
                                  sizeof(backup_header) - sizeof(uint16_t));
    if (calc_crc != backup_header.header_crc16)
    {
        tlv_printf("ERROR: Backup header CRC mismatch\n");
        return TLV_ERROR_CORRUPTED;
    }

    // 验证数据合理性
    if (backup_header.data_region_size !=
        (TLV_BACKUP_ADDR - TLV_DATA_ADDR))
    {
        tlv_printf("ERROR: Backup header data size mismatch\n");
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

    // 检查版本兼容性,主版本必须相同,当前子版本大于文件系统版本
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

int read_data_block(uint32_t addr, void *buf, uint16_t *len)
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
        return TLV_ERROR_NO_BUFFER_MEMORY;
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

/* ============================ 私有函数：快照管理============================ */
/**
 * @brief 创建快照
 */
static void transaction_snapshot_create(void)
{
    g_tlv_ctx.snapshot.next_free_addr = g_tlv_ctx.header->next_free_addr;
    g_tlv_ctx.snapshot.used_space = g_tlv_ctx.header->used_space;
    g_tlv_ctx.snapshot.free_space = g_tlv_ctx.header->free_space;
    g_tlv_ctx.snapshot.fragment_count = g_tlv_ctx.header->fragment_count;
    g_tlv_ctx.snapshot.fragment_size = g_tlv_ctx.header->fragment_size;
    g_tlv_ctx.snapshot.tag_count = g_tlv_ctx.header->tag_count;
    g_tlv_ctx.snapshot.is_active = true;
}

/**
 * @brief 回滚快照
 */
static void transaction_snapshot_rollback(void)
{
    if (g_tlv_ctx.snapshot.is_active)
    {
        g_tlv_ctx.header->next_free_addr = g_tlv_ctx.snapshot.next_free_addr;
        g_tlv_ctx.header->used_space = g_tlv_ctx.snapshot.used_space;
        g_tlv_ctx.header->free_space = g_tlv_ctx.snapshot.free_space;
        g_tlv_ctx.header->fragment_count = g_tlv_ctx.snapshot.fragment_count;
        g_tlv_ctx.header->fragment_size = g_tlv_ctx.snapshot.fragment_size;
        g_tlv_ctx.header->tag_count = g_tlv_ctx.snapshot.tag_count;
        g_tlv_ctx.snapshot.is_active = false;
    }
}

/**
 * @brief 提交快照（清除快照标记）
 */
static void transaction_snapshot_commit(void)
{
    g_tlv_ctx.snapshot.is_active = false;
}

/**
 * @brief 增加已使用空间
 */
static void increase_used_space(uint32_t size)
{
    g_tlv_ctx.header->used_space += size;
}
/**
 * @brief 减少已使用空间
 */
static void reduce_used_space(uint32_t size)
{
    g_tlv_ctx.header->used_space -= size;
}

/* ============================ 流式操作私有函数 ============================ */

/**
 * @brief 句柄索引转换为句柄
 * @param index 内部索引
 * @return 句柄
 */
static inline tlv_stream_handle_t index_to_handle(int index)
{
    if (index < 0 || index >= TLV_MAX_STREAM_HANDLES)
    {
        return TLV_STREAM_INVALID_HANDLE;
    }
    // 句柄编码：高16位是魔数，低16位是索引
    return (tlv_stream_handle_t)((TLV_STREAM_MAGIC & 0xFFFF0000) | (index & 0xFFFF));
}

/**
 * @brief 句柄转换为索引
 * @param handle 句柄
 * @return 索引（-1 表示无效）
 */
static inline int handle_to_index(tlv_stream_handle_t handle)
{
    // 验证魔数
    if ((handle & 0xFFFF0000) != (TLV_STREAM_MAGIC & 0xFFFF0000))
    {
        return -1;
    }

    int index = handle & 0xFFFF;
    if (index < 0 || index >= TLV_MAX_STREAM_HANDLES)
    {
        return -1;
    }

    return index;
}

/**
 * @brief 查找空闲的流句柄
 * @return 句柄（TLV_INVALID_HANDLE 表示失败）
 */
static tlv_stream_handle_t find_free_stream_handle(void)
{
    for (int i = 0; i < TLV_MAX_STREAM_HANDLES; i++)
    {
        if (g_stream_ctx.handles[i].state == TLV_STREAM_STATE_IDLE)
        {
            // 清空并初始化魔数
            memset(&g_stream_ctx.handles[i], 0, sizeof(tlv_stream_context_internal_t));
            g_stream_ctx.handles[i].magic = TLV_STREAM_MAGIC;
            return index_to_handle(i);
        }
    }
    return TLV_STREAM_INVALID_HANDLE;
}

/**
 * @brief 验证句柄有效性并获取内部结构
 * @param handle 句柄
 * @param expected_state 期望的状态
 * @return 内部结构指针（NULL 表示无效）
 */
static tlv_stream_context_internal_t *validate_and_get_handle(
    tlv_stream_handle_t handle,
    tlv_stream_state_t expected_state)
{
    int index = handle_to_index(handle);
    if (index < 0)
    {
        return NULL;
    }

    tlv_stream_context_internal_t *h = &g_stream_ctx.handles[index];

    // 验证魔数
    if (h->magic != TLV_STREAM_MAGIC)
    {
        return NULL;
    }

    // 验证状态
    if (h->state != expected_state)
    {
        return NULL;
    }

    return h;
}

/**
 * @brief 释放流句柄
 * @param handle 句柄
 */
static void release_stream_handle(tlv_stream_handle_t handle)
{
    int index = handle_to_index(handle);
    if (index >= 0 && index < TLV_MAX_STREAM_HANDLES)
    {
        memset(&g_stream_ctx.handles[index], 0, sizeof(tlv_stream_context_internal_t));
    }
}
/* ============================ 流式写入API ============================ */

/**
 * @brief 开始分段写入
 * @param tag Tag值
 * @param total_len 总数据长度
 * @return 写入句柄（TLV_INVALID_HANDLE 表示失败，通过 tlv_get_last_error() 获取错误码）
 */
tlv_stream_handle_t tlv_write_begin(uint16_t tag, uint16_t total_len)
{
    if (tag == 0 || total_len == 0)
    {
        TLV_TAG_ERROR(TLV_ERROR_INVALID_PARAM);
        return TLV_STREAM_INVALID_HANDLE;
    }

    if (!g_tlv_ctx.header || !g_tlv_ctx.index_table)
    {
        TLV_TAG_ERROR(TLV_ERROR_INVALID_PARAM);
        return TLV_STREAM_INVALID_HANDLE;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        TLV_TAG_ERROR(TLV_ERROR);
        return TLV_STREAM_INVALID_HANDLE;
    }

    // 查找元数据
    const tlv_meta_const_t *meta = get_meta(tag);
    if (!meta)
    {
        TLV_TAG_ERROR(TLV_ERROR_NOT_FOUND);
        return TLV_STREAM_INVALID_HANDLE;
    }

    // 检查长度
    if (total_len > meta->max_length)
    {
        TLV_TAG_ERROR(TLV_ERROR_INVALID_PARAM);
        return TLV_STREAM_INVALID_HANDLE;
    }

    // 查找空闲句柄
    tlv_stream_handle_t handle = find_free_stream_handle();
    if (handle == TLV_STREAM_INVALID_HANDLE)
    {
        TLV_TAG_ERROR(TLV_ERROR_INVALID_HANDLE);
        return TLV_STREAM_INVALID_HANDLE;
    }

    int index = handle_to_index(handle);
    tlv_stream_context_internal_t *h = &g_stream_ctx.handles[index];

    // 创建事务快照
    transaction_snapshot_create();

    // 查找现有索引
    tlv_index_entry_t *idx = tlv_index_find(&g_tlv_ctx, tag);
    uint32_t target_addr;
    uint32_t old_block_size = 0;
    uint32_t new_block_size = TLV_BLOCK_SIZE(total_len);
    bool need_add_index = false;
    bool has_free_slot = g_tlv_ctx.header->tag_count < TLV_MAX_TAG_COUNT;

    h->old_index = NULL;
    h->old_block_size = 0;

    if (idx && (idx->flags & TLV_FLAG_VALID))
    {
        // 读取旧Header
        tlv_data_block_header_t old_header;
        int ret = tlv_port_fram_read(idx->data_addr, &old_header, sizeof(old_header));
        if (ret != TLV_OK)
        {
            transaction_snapshot_rollback();
            release_stream_handle(handle);
            TLV_TAG_ERROR(ret);
            return TLV_STREAM_INVALID_HANDLE;
        }

        old_block_size = TLV_BLOCK_SIZE(old_header.length);
        h->old_version = old_header.version;

        if (new_block_size <= old_block_size)
        {
            // 原地更新
            target_addr = idx->data_addr;
            reduce_used_space(old_block_size);
            increase_used_space(new_block_size);
        }
        else
        {
            // 需要重新分配
            if (!has_free_slot)
            {
                transaction_snapshot_rollback();
                release_stream_handle(handle);
                TLV_TAG_ERROR(TLV_ERROR_NO_INDEX_SPACE);
                return TLV_STREAM_INVALID_HANDLE;
            }

            target_addr = allocate_space(new_block_size);
            if (target_addr == 0)
            {
                transaction_snapshot_rollback();
                release_stream_handle(handle);
                TLV_TAG_ERROR(TLV_ERROR_NO_MEMORY_SPACE);
                return TLV_STREAM_INVALID_HANDLE;
            }

            need_add_index = true;
            h->old_index = idx;
            h->old_block_size = old_block_size;
        }
    }
    else
    {
        // 新Tag
        if (!has_free_slot)
        {
            transaction_snapshot_rollback();
            release_stream_handle(handle);
            TLV_TAG_ERROR(TLV_ERROR_NO_INDEX_SPACE);
            return TLV_STREAM_INVALID_HANDLE;
        }

        target_addr = allocate_space(new_block_size);
        if (target_addr == 0)
        {
            transaction_snapshot_rollback();
            release_stream_handle(handle);
            TLV_TAG_ERROR(TLV_ERROR_NO_MEMORY_SPACE);
            return TLV_STREAM_INVALID_HANDLE;
        }

        need_add_index = true;
    }

    // 初始化句柄
    h->tag = tag;
    h->data_addr = target_addr;
    h->current_offset = sizeof(tlv_data_block_header_t);
    h->total_len = total_len;
    h->processed_len = 0;
    h->crc16 = tlv_crc16_init();
    h->state = TLV_STREAM_STATE_WRITING;

    // 构建并写入 Header
    tlv_data_block_header_t header = {0};
    header.tag = tag;
    header.length = total_len;
    header.version = meta->version;
    header.flags = 0;
    header.timestamp = tlv_port_get_timestamp_s();

    // 读取旧的 write_count
    if (!need_add_index && idx)
    {
        tlv_data_block_header_t old_hdr;
        if (tlv_port_fram_read(target_addr, &old_hdr, sizeof(old_hdr)) == TLV_OK &&
            old_hdr.tag == tag)
        {
            header.write_count = old_hdr.write_count + 1;
        }
        else
        {
            header.write_count = 1;
        }
    }
    else
    {
        header.write_count = 1;
    }

    // 更新 CRC
    h->crc16 = tlv_crc16_update(h->crc16, &header, sizeof(header));

    // 写入 Header
    int ret = tlv_port_fram_write(target_addr, &header, sizeof(header));
    if (ret != TLV_OK)
    {
        transaction_snapshot_rollback();
        release_stream_handle(handle);
        TLV_TAG_ERROR(ret);
        return TLV_STREAM_INVALID_HANDLE;
    }

#if TLV_DEBUG
    tlv_printf("Stream write begin: handle=0x%08X, tag=0x%04X, len=%u\n",
               handle, tag, total_len);
#endif

    return handle;
}

/**
 * @brief 写入数据段
 * @param handle 写入句柄
 * @param data 数据指针
 * @param len 数据长度
 * @return TLV_OK: 成功, 其他: 错误码
 */
int tlv_write_chunk(tlv_stream_handle_t handle, const void *data, uint16_t len)
{
    if (!data || len == 0)
    {
        return TLV_SET_ERROR(TLV_ERROR_INVALID_PARAM, g_last_error.tag);
    }

    tlv_stream_context_internal_t *h = validate_and_get_handle(handle, TLV_STREAM_STATE_WRITING);
    if (!h)
    {
        return TLV_SET_ERROR(TLV_ERROR_INVALID_HANDLE, g_last_error.tag);
    }

    // 检查是否超出总长度
    if (h->processed_len + len > h->total_len)
    {
        tlv_printf("ERROR: Chunk exceeds total length (%u + %u > %u)\n",
                   h->processed_len, len, h->total_len);
        return TLV_SET_ERROR(TLV_ERROR_INVALID_PARAM, g_last_error.tag);
    }

    // 写入数据
    int ret = tlv_port_fram_write(h->data_addr + h->current_offset, data, len);
    if (ret != TLV_OK)
    {
        tlv_printf("ERROR: FRAM write failed at offset %u\n", h->current_offset);
        return TLV_SET_ERROR(ret, g_last_error.tag);
    }

    // 更新 CRC
    h->crc16 = tlv_crc16_update(h->crc16, data, len);

    // 更新状态
    h->current_offset += len;
    h->processed_len += len;

#if TLV_DEBUG
    tlv_printf("Write chunk: handle=0x%08X, len=%u, progress=%u/%u\n",
               handle, len, h->processed_len, h->total_len);
#endif

    return TLV_OK;
}

/**
 * @brief 完成分段写入
 * @param handle 写入句柄
 * @return TLV_OK: 成功, 其他: 错误码
 */
int tlv_write_end(tlv_stream_handle_t handle)
{
    tlv_stream_context_internal_t *h = validate_and_get_handle(handle, TLV_STREAM_STATE_WRITING);
    if (!h)
    {
        return TLV_SET_ERROR(TLV_ERROR_INVALID_HANDLE, g_last_error.tag);
    }

    // 检查是否写满
    if (h->processed_len != h->total_len)
    {
        tlv_printf("ERROR: Incomplete write (%u/%u bytes)\n",
                   h->processed_len, h->total_len);
        return TLV_SET_ERROR(TLV_ERROR_INVALID_STATE, g_last_error.tag);
    }

    // 完成 CRC 计算
    uint16_t crc = tlv_crc16_final(h->crc16);

    // 写入 CRC
    int ret = tlv_port_fram_write(h->data_addr + h->current_offset, &crc, sizeof(crc));
    if (ret != TLV_OK)
    {
        tlv_printf("ERROR: CRC write failed\n");
        transaction_snapshot_rollback();
        system_header_save();
        release_stream_handle(handle);
        return TLV_SET_ERROR(ret, g_last_error.tag);
    }

    // 更新索引
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, h->tag);
    bool need_add_index = (h->old_index != NULL);

    if (need_add_index)
    {
        // 标记旧索引为脏
        if (h->old_index && (h->old_index->flags & TLV_FLAG_VALID))
        {
            h->old_index->flags = TLV_FLAG_DIRTY;
            reduce_used_space(h->old_block_size);
            g_tlv_ctx.header->fragment_count++;
            g_tlv_ctx.header->fragment_size += h->old_block_size;
        }

        // 添加新索引
        tlv_index_entry_t *new_index = tlv_index_add(&g_tlv_ctx, h->tag, h->data_addr);
        if (!new_index)
        {
            tlv_printf("CRITICAL: Index add failed\n");
            transaction_snapshot_rollback();
            system_header_save();
            release_stream_handle(handle);
            return TLV_SET_ERROR(TLV_ERROR_NO_INDEX_SPACE, g_last_error.tag);
        }
    }
    else
    {
        // 更新现有索引
        tlv_index_update(&g_tlv_ctx, h->tag, h->data_addr);
    }

    // 保存索引
    ret = tlv_index_save(&g_tlv_ctx);
    if (ret != TLV_OK)
    {
        tlv_printf("ERROR: Index save failed\n");
        release_stream_handle(handle);
        return TLV_SET_ERROR(ret, g_last_error.tag);
    }

    // 提交事务
    transaction_snapshot_commit();

    // 更新统计
    g_tlv_ctx.header->total_writes++;
    g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();

    ret = system_header_save();
    if (ret != TLV_OK)
    {
        release_stream_handle(handle);
        return TLV_SET_ERROR(ret, g_last_error.tag);
    }

#if TLV_DEBUG
    tlv_printf("Stream write completed: handle=0x%08X, tag=0x%04X, len=%u\n",
               handle, h->tag, h->total_len);
#endif

    // 释放句柄
    release_stream_handle(handle);

    // 检查是否自动整理碎片
#if TLV_AUTO_CLEAN_FRAGEMENT
    uint32_t fragUsagePercent = 0;
    ret = tlv_calculate_fragmentation(&fragUsagePercent);
    if (ret == TLV_OK && fragUsagePercent >= TLV_AUTO_DEFRAG_THRESHOLD)
    {
        ret = tlv_defragment();
        if (ret != TLV_OK)
        {
            tlv_printf("WARNING: Auto defragment failed: %d\n", ret);
            TLV_SET_ERROR(ret, g_last_error.tag);
        }
    }
#endif

    return TLV_OK;
}

/**
 * @brief 取消分段写入
 * @param handle 写入句柄
 */
void tlv_write_abort(tlv_stream_handle_t handle)
{
    tlv_stream_context_internal_t *h = validate_and_get_handle(handle, TLV_STREAM_STATE_WRITING);
    if (!h)
    {
        return;
    }

    tlv_printf("Aborting stream write: handle=0x%08X, tag=0x%04X, written=%u/%u\n",
               handle, h->tag, h->processed_len, h->total_len);

    // 回滚事务
    transaction_snapshot_rollback();
    system_header_save();

    // 统计碎片
    uint32_t wasted_size = TLV_BLOCK_SIZE(h->total_len);
    g_tlv_ctx.header->fragment_count++;
    g_tlv_ctx.header->fragment_size += wasted_size;

    // 释放句柄
    release_stream_handle(handle);
}

/* ============================ 流式读取API ============================ */

/**
 * @brief 开始分段读取
 * @param tag Tag值
 * @param total_len 输出总数据长度
 * @return 读取句柄（TLV_INVALID_HANDLE 表示失败）
 */
tlv_stream_handle_t tlv_read_begin(uint16_t tag, uint16_t *total_len)
{
    if (tag == 0 || !total_len)
    {
        TLV_TAG_ERROR(TLV_ERROR_INVALID_PARAM);
        return TLV_STREAM_INVALID_HANDLE;
    }

    if (g_tlv_ctx.state != TLV_STATE_INITIALIZED)
    {
        TLV_TAG_ERROR(TLV_ERROR);
        return TLV_STREAM_INVALID_HANDLE;
    }

    // 查找索引
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, tag);
    if (!index || !(index->flags & TLV_FLAG_VALID))
    {
        TLV_TAG_ERROR(TLV_ERROR_NOT_FOUND);
        return TLV_STREAM_INVALID_HANDLE;
    }

    // 查找空闲句柄
    tlv_stream_handle_t handle = find_free_stream_handle();
    if (handle == TLV_STREAM_INVALID_HANDLE)
    {
        TLV_TAG_ERROR(TLV_ERROR_INVALID_HANDLE);
        return TLV_STREAM_INVALID_HANDLE;
    }

    int idx = handle_to_index(handle);
    tlv_stream_context_internal_t *h = &g_stream_ctx.handles[idx];

    // 读取 Header
    tlv_data_block_header_t header;
    int ret = tlv_port_fram_read(index->data_addr, &header, sizeof(header));
    if (ret != TLV_OK)
    {
        release_stream_handle(handle);
        TLV_TAG_ERROR(ret);
        return TLV_STREAM_INVALID_HANDLE;
    }

    // 验证 tag 匹配
    if (header.tag != tag)
    {
        tlv_printf("ERROR: Tag mismatch (expected 0x%04X, got 0x%04X)\n",
                   tag, header.tag);
        release_stream_handle(handle);
        TLV_TAG_ERROR(TLV_ERROR);
        return TLV_STREAM_INVALID_HANDLE;
    }

    // 初始化句柄
    h->tag = tag;
    h->data_addr = index->data_addr;
    h->current_offset = sizeof(tlv_data_block_header_t);
    h->total_len = header.length;
    h->processed_len = 0;
    h->crc16 = tlv_crc16_init();
    h->crc16 = tlv_crc16_update(h->crc16, &header, sizeof(header));
    h->state = TLV_STREAM_STATE_READING;

    *total_len = header.length;

#if TLV_DEBUG
    tlv_printf("Stream read begin: handle=0x%08X, tag=0x%04X, len=%u\n",
               handle, tag, header.length);
#endif

    return handle;
}

/**
 * @brief 读取数据段
 * @param handle 读取句柄
 * @param buf 输出缓冲区
 * @param len 请求读取长度
 * @return 实际读取长度（>=0 成功，<0 错误码）
 */
int tlv_read_chunk(tlv_stream_handle_t handle, void *buf, uint16_t *len)
{
    if (!buf || *len == 0)
    {
        return TLV_SET_ERROR(TLV_ERROR_INVALID_PARAM, g_last_error.error_code);
    }

    tlv_stream_context_internal_t *h = validate_and_get_handle(handle, TLV_STREAM_STATE_READING);
    if (!h)
    {
        TLV_SET_ERROR(TLV_ERROR_INVALID_HANDLE, g_last_error.error_code);
        return TLV_ERROR_INVALID_HANDLE;
    }

    // 计算实际可读取长度
    uint16_t request_len = *len;
    uint16_t remaining = h->total_len - h->processed_len;
    uint16_t actual_len = (request_len > remaining) ? remaining : request_len;

    if (actual_len == 0)
    {
        *len = 0;
        return TLV_OK;
    }

    // 读取数据
    int ret = tlv_port_fram_read(h->data_addr + h->current_offset, buf, actual_len);
    if (ret != TLV_OK)
    {
        return TLV_SET_ERROR(ret, g_last_error.error_code);
    }

    h->crc16 = tlv_crc16_update(h->crc16, buf, actual_len);
    h->current_offset += actual_len;
    h->processed_len += actual_len;

    *len = actual_len;

#if TLV_DEBUG
    tlv_printf("Read chunk: handle=0x%08X, len=%u, progress=%u/%u\n",
               handle, actual_len, h->processed_len, h->total_len);
#endif
    return TLV_OK;
}

/**
 * @brief 完成分段读取
 * @param handle 读取句柄
 * @return TLV_OK: 成功, 其他: 错误码
 */
int tlv_read_end(tlv_stream_handle_t handle)
{
    tlv_stream_context_internal_t *h = validate_and_get_handle(handle, TLV_STREAM_STATE_READING);
    if (!h)
    {
        return TLV_SET_ERROR(TLV_ERROR_INVALID_HANDLE, g_last_error.error_code);
    }

    // 检查是否读完
    if (h->processed_len != h->total_len)
    {
        tlv_printf("WARNING: Incomplete read (%u/%u bytes)\n",
                   h->processed_len, h->total_len);
        release_stream_handle(handle);
        return TLV_SET_ERROR(TLV_ERROR_INVALID_STATE, g_last_error.error_code);
    }

    // 读取存储的 CRC
    uint16_t stored_crc;
    int ret = tlv_port_fram_read(h->data_addr + h->current_offset, &stored_crc, sizeof(stored_crc));
    if (ret != TLV_OK)
    {
        tlv_printf("ERROR: CRC read failed\n");
        release_stream_handle(handle);
        return TLV_SET_ERROR(ret, g_last_error.error_code);
    }

    // 验证 CRC
    uint16_t calc_crc = tlv_crc16_final(h->crc16);
    if (calc_crc != stored_crc)
    {
        tlv_printf("ERROR: CRC mismatch (calc=0x%04X, stored=0x%04X)\n",
                   calc_crc, stored_crc);
        release_stream_handle(handle);
        return TLV_SET_ERROR(TLV_ERROR_CRC_FAILED, g_last_error.error_code);
    }

#if TLV_DEBUG
    tlv_printf("Stream read completed: handle=0x%08X, tag=0x%04X, len=%u, CRC=0x%04X\n",
               handle, h->tag, h->total_len, calc_crc);
#endif

    // 释放句柄
    release_stream_handle(handle);

    return TLV_OK;
}

/**
 * @brief 取消分段读取
 * @param handle 读取句柄
 */
void tlv_read_abort(tlv_stream_handle_t handle)
{
    tlv_stream_context_internal_t *h = validate_and_get_handle(handle, TLV_STREAM_STATE_READING);
    if (!h)
    {
        return;
    }

    tlv_printf("Aborting stream read: handle=0x%08X, tag=0x%04X, read=%u/%u\n",
               handle, h->tag, h->processed_len, h->total_len);

    // 释放句柄
    release_stream_handle(handle);
}

/* ============================ 错误处理内部函数 ============================ */

/**
 * @brief 设置最后一次错误
 * @param error_code 错误码
 * @param tag 相关的 tag
 * @param line 代码行号
 * @param function 函数名
 */
static int tlv_set_last_error(int error_code, uint16_t tag,
                              uint32_t line, const char *function)
{
    g_last_error.error_code = error_code;
    g_last_error.tag = tag;
    g_last_error.timestamp = tlv_port_get_timestamp_s();

#if TLV_DEBUG
    g_last_error.line = line;
    g_last_error.function = function;

    tlv_printf("ERROR: code=%d, tag=0x%04X, at %s:%u\n",
               error_code, tag, function ? function : "unknown", line);
#else
    g_last_error.line = 0;
    g_last_error.function = NULL;
#endif

#if TLV_ENABLE_ERROR_TRACKING
    // 保存到历史记录
    g_error_history[g_error_history_index] = g_last_error;
    g_error_history_index = (g_error_history_index + 1) % TLV_ERROR_HISTORY_SIZE;
#endif
    return error_code;
}

/* ============================ 错误查询API ============================ */

/**
 * @brief 获取最后一次错误码
 * @return 错误码（0 表示无错误）
 */
int tlv_get_last_error(void)
{
    return g_last_error.error_code;
}

/**
 * @brief 获取最后一次错误的详细信息
 * @param error_ctx 输出错误上下文（可选，传NULL只返回错误码）
 * @return 错误码
 */
int tlv_get_last_error_ex(tlv_error_context_t *error_ctx)
{
    if (error_ctx)
    {
        *error_ctx = g_last_error;
    }
    return g_last_error.error_code;
}

/**
 * @brief 清除错误状态
 */
void tlv_clear_error(void)
{
    memset(&g_last_error, 0, sizeof(tlv_error_context_t));
}

/**
 * @brief 获取错误码对应的描述字符串
 * @param error_code 错误码
 * @return 错误描述字符串
 */
const char *tlv_get_error_string(int error_code)
{
    switch (error_code)
    {
    case TLV_OK:
        return "Success";
    case TLV_ERROR:
        return "Generic error";
    case TLV_ERROR_INVALID_PARAM:
        return "Invalid parameter";
    case TLV_ERROR_NOT_FOUND:
        return "Tag not found";
    case TLV_ERROR_NO_MEMORY_SPACE:
        return "No memory space";
    case TLV_ERROR_NO_INDEX_SPACE:
        return "No index space";
    case TLV_ERROR_CRC_FAILED:
        return "CRC check failed";
    case TLV_ERROR_CORRUPTED:
        return "Data corrupted";
    case TLV_ERROR_INVALID_HANDLE:
        return "Invalid handle";
    case TLV_ERROR_INVALID_STATE:
        return "Invalid state";
    case TLV_ERROR_NO_BUFFER_MEMORY:
        return "Buffer too small";
    default:
        return "Unknown error";
    }
}

#if TLV_ENABLE_ERROR_TRACKING
/**
 * @brief 获取错误历史记录
 * @param history 输出缓冲区
 * @param count 缓冲区大小（输入），实际记录数（输出）
 * @return TLV_OK: 成功
 */
int tlv_get_error_history(tlv_error_context_t *history, uint8_t *count)
{
    if (!history || !count || *count == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    uint8_t max_count = *count;
    uint8_t actual_count = 0;

    // 从最新的错误开始复制
    for (int i = 0; i < TLV_ERROR_HISTORY_SIZE && actual_count < max_count; i++)
    {
        int idx = (g_error_history_index - 1 - i + TLV_ERROR_HISTORY_SIZE) % TLV_ERROR_HISTORY_SIZE;

        if (g_error_history[idx].error_code != 0)
        {
            history[actual_count++] = g_error_history[idx];
        }
    }

    *count = actual_count;
    return TLV_OK;
}

/**
 * @brief 清除错误历史
 */
void tlv_clear_error_history(void)
{
    memset(g_error_history, 0, sizeof(g_error_history));
    g_error_history_index = 0;
}
#endif
