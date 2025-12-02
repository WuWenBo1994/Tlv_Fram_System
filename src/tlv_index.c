/**
 * @file tlv_index.c
 * @brief TLV索引管理模块（简化版，数组查找）
 */

#include "tlv_index.h"
#include "tlv_port.h"
#include "tlv_utils.h"
#include "tlv_meta_table.h"
#include <string.h>

/* ============================ 私有函数声明 ============================ */

static const tlv_meta_const_t *find_meta_by_tag(uint16_t tag);
static bool is_tag_region_valid(uint16_t tag, uint32_t addr, uint32_t size);

/* ============================ 索引表管理实现 ============================ */

int tlv_index_init(tlv_context_t *ctx)
{
    if (!ctx)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 使用静态分配的内存，不使用malloc
    static tlv_system_header_t static_header;
    static tlv_index_table_t static_index;

    ctx->header = &static_header;
    ctx->index_table = &static_index;

    // 清零索引表
    memset(ctx->index_table, 0, sizeof(tlv_index_table_t));

    // 检查静态内存大小是否足够
    if (sizeof(tlv_system_header_t) + sizeof(tlv_index_table_t) >
        TLV_BUFFER_SIZE)
    {
        return TLV_ERROR_NO_MEMORY;
    }

    return TLV_OK;
}

void tlv_index_deinit(tlv_context_t *ctx)
{
    if (!ctx)
    {
        return;
    }

    // 静态分配，不需要释放
    ctx->header = NULL;
    ctx->index_table = NULL;
}

int tlv_index_load(tlv_context_t *ctx)
{
    if (!ctx || !ctx->index_table)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 从FRAM读取索引表
    int ret = tlv_port_fram_read(TLV_INDEX_ADDR, ctx->index_table,
                                 sizeof(tlv_index_table_t));
    if (ret != TLV_OK)
    {
        return ret;
    }

    // 校验索引表CRC16
    uint16_t calc_crc = tlv_crc16(ctx->index_table->entries,
                                  sizeof(ctx->index_table->entries));
    if (calc_crc != ctx->index_table->index_crc16)
    {
        return TLV_ERROR_CRC_FAILED;
    }

    return TLV_OK;
}

int tlv_index_save(tlv_context_t *ctx)
{
    if (!ctx || !ctx->index_table)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 计算索引表CRC16
    ctx->index_table->index_crc16 = tlv_crc16(ctx->index_table->entries,
                                              sizeof(ctx->index_table->entries));

    // 写入FRAM
    return tlv_port_fram_write(TLV_INDEX_ADDR, ctx->index_table,
                               sizeof(tlv_index_table_t));
}

int tlv_index_verify(tlv_context_t *ctx)
{
    if (!ctx || !ctx->index_table)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 计算CRC并对比
    uint16_t calc_crc = tlv_crc16(ctx->index_table->entries,
                                  sizeof(ctx->index_table->entries));

    if (calc_crc != ctx->index_table->index_crc16)
    {
        return TLV_ERROR_CRC_FAILED;
    }

    return TLV_OK;
}

/* ============================ 索引查找实现（数组查找） ============================ */

tlv_index_entry_t *tlv_index_find(tlv_context_t *ctx, uint16_t tag)
{
    if (!ctx || !ctx->index_table || tag == 0)
    {
        return NULL;
    }

    // 线性搜索（简单搜索）
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        if (ctx->index_table->entries[i].tag == tag &&
            (ctx->index_table->entries[i].flags & TLV_FLAG_VALID))
        {
            return &ctx->index_table->entries[i];
        }
    }

    return NULL;
}

// 新增：快速查找（使用元数据表的预计算索引）
static inline int get_tag_index(uint16_t tag)
{
    // 如果元数据表有序，可以使用二分查找
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        if (TLV_META_MAP[i].tag == tag)
        {
            return i;
        }
        if (TLV_META_MAP[i].tag == 0xFFFF)
        {
            break;
        }
    }
    return -1;
}

tlv_index_entry_t *tlv_index_find_fast(tlv_context_t *ctx, uint16_t tag)
{
    if (!ctx || !ctx->index_table || tag == 0)
    {
        return NULL;
    }

    // 尝试使用预计算索引（如果元数据表有序）
    int meta_index = get_tag_index(tag);
    if (meta_index >= 0 && meta_index < TLV_MAX_TAG_COUNT)
    {
        tlv_index_entry_t *entry = &ctx->index_table->entries[meta_index];
        if (entry->tag == tag && (entry->flags & TLV_FLAG_VALID))
        {
            return entry;
        }
    }

    // 回退到线性搜索
    return tlv_index_find(ctx, tag);
}

tlv_index_entry_t *tlv_index_find_free_slot(tlv_context_t *ctx)
{
    if (!ctx || !ctx->index_table)
    {
        return NULL;
    }

    // 查找空闲槽位（tag为0表示未使用）
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        if (ctx->index_table->entries[i].tag == 0)
        {
            return &ctx->index_table->entries[i];
        }
    }

    return NULL;
}

/* ============================ 索引操作实现 ============================ */

tlv_index_entry_t *tlv_index_add(tlv_context_t *ctx, uint16_t tag, uint32_t addr)
{
    if (!ctx || tag == 0 || !TLV_IS_VALID_ADDR(addr))
    {
        return NULL;
    }

    // 检查是否已存在
    tlv_index_entry_t *existing = tlv_index_find(ctx, tag);
    if (existing)
    {
        existing->data_addr = addr;
        existing->flags |= TLV_FLAG_VALID;
        return existing;
    }

    // 查找空闲槽位
    tlv_index_entry_t *entry = tlv_index_find_free_slot(ctx);
    if (!entry)
    {
        return NULL;
    }

    // 填充索引项
    entry->tag = tag;
    entry->data_addr = addr;
    entry->flags = TLV_FLAG_VALID;
    entry->version = find_meta_by_tag(tag) ? find_meta_by_tag(tag)->version : 1;

    // 更新Tag计数
    ctx->header->tag_count++;

    // 检查区域有效性（编译时检查的运行时验证）
    const tlv_meta_const_t *meta = find_meta_by_tag(tag);
    if (meta && !is_tag_region_valid(tag, addr, meta->max_length + 20))
    {
        // 恢复
        memset(entry, 0, sizeof(tlv_index_entry_t));
        ctx->header->tag_count--;
        return NULL;
    }

    return entry;
}

int tlv_index_update(tlv_context_t *ctx, uint16_t tag, uint32_t addr)
{
    if (!ctx || tag == 0 || !TLV_IS_VALID_ADDR(addr))
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    tlv_index_entry_t *entry = tlv_index_find(ctx, tag);
    if (!entry)
    {
        return TLV_ERROR_NOT_FOUND;
    }

    entry->data_addr = addr;
    entry->flags |= TLV_FLAG_VALID;

    return TLV_OK;
}

int tlv_index_remove(tlv_context_t *ctx, uint16_t tag)
{
    if (!ctx || tag == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    tlv_index_entry_t *entry = tlv_index_find(ctx, tag);
    if (!entry)
    {
        return TLV_ERROR_NOT_FOUND;
    }

    // 清空索引项
    memset(entry, 0, sizeof(tlv_index_entry_t));

    // 更新Tag计数
    if (ctx->header->tag_count > 0)
    {
        ctx->header->tag_count--;
    }

    return TLV_OK;
}

/* ============================ 私有函数实现 ============================ */

static const tlv_meta_const_t *find_meta_by_tag(uint16_t tag)
{
    for (uint32_t i = 0; i < sizeof(TLV_META_MAP) / sizeof(tlv_meta_const_t); i++)
    {
        if (TLV_META_MAP[i].tag == tag)
        {
            return &TLV_META_MAP[i];
        }
        if (TLV_META_MAP[i].tag == 0xFFFF)
        {
            break;
        }
    }
    return NULL;
}

static bool is_tag_region_valid(uint16_t tag, uint32_t addr, uint32_t size)
{
    if (!TLV_IS_VALID_ADDR(addr) || !TLV_IS_SIZE_SAFE(addr, size))
    {
        return false;
    }

    // 检查是否与现有Tag区域冲突
    for (uint32_t i = 0; i < sizeof(TLV_META_MAP) / sizeof(tlv_meta_const_t); i++)
    {
        if (TLV_META_MAP[i].tag != 0xFFFF && TLV_META_MAP[i].tag != tag)
        {
            uint32_t other_addr = TLV_DATA_ADDR;                   // 这里简化处理
            uint32_t other_size = TLV_META_MAP[i].max_length + 20; // Header+Data+CRC

            if (TLV_REGIONS_OVERLAP(addr, size, other_addr, other_size))
            {
                return false;
            }
        }
    }

    return true;
}
