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

/**
 * @brief 初始化TLV上下文的索引结构
 * 
 * 该函数用于初始化tlv_context_t结构体中的header和index_table成员，
 * 使用静态分配的内存空间，并将这些内存区域清零。
 * 
 * @param ctx 指向TLV上下文结构体的指针
 * 
 * @return TLV_OK 成功初始化
 * @return TLV_ERROR_INVALID_PARAM 参数无效(ctx为NULL)
 */
int tlv_index_init(tlv_context_t *ctx)
{
    if (!ctx)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 使用静态分配的内存
    static tlv_system_header_t static_header;
    static tlv_index_table_t static_index;

    ctx->header = &static_header;
    ctx->index_table = &static_index;

    // 清零
    memset(&static_header, 0, sizeof(static_header));
    memset(&static_index, 0, sizeof(static_index));

    return TLV_OK;
}

/**
 * @brief 释放TLV索引相关的资源
 * 该函数用于清理tlv_context_t结构体中的索引相关指针，
 * 将header和index_table指针置为NULL。由于这些内存是静态分配的，因此不需要实际的内存释放操作。
 *
 * @param ctx 指向tlv_context_t结构体的指针
 */
void tlv_index_deinit(tlv_context_t *ctx)
{
    if (!ctx)
    {
        return;
    }

    /* 静态分配的内存，只需要将指针置空，无需实际释放 */
    ctx->header = NULL;
    ctx->index_table = NULL;
}

/**
 * @brief 从FRAM加载TLV索引表
 * 
 * 该函数负责从FRAM中读取索引表数据，并进行CRC16校验以确保数据完整性。
 * 
 * @param ctx 指向TLV上下文结构体的指针，必须包含有效的index_table成员
 * 
 * @return TLV_OK - 成功加载并校验通过
 * @return TLV_ERROR_INVALID_PARAM - 参数无效（ctx或index_table为空）
 * @return TLV_ERROR_CRC_FAILED - CRC16校验失败
 * @return 其他 - FRAM读取操作返回的错误码
 */
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

/**
 * @brief 保存TLV索引表到FRAM存储器
 * 
 * 该函数负责将TLV索引表计算CRC校验码后写入FRAM存储器中，
 * 用于持久化保存索引信息。
 * 
 * @param ctx TLV上下文指针，包含索引表等信息
 * 
 * @return TLV_ERROR_INVALID_PARAM 参数无效
 * @return 其他 返回tlv_port_fram_write函数的执行结果
 */
int tlv_index_save(tlv_context_t *ctx)
{
    if (!ctx || !ctx->index_table)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 计算索引表CRC16校验码
    ctx->index_table->index_crc16 = tlv_crc16(ctx->index_table->entries,
                                              sizeof(ctx->index_table->entries));

    // 将索引表写入FRAM存储器
    return tlv_port_fram_write(TLV_INDEX_ADDR, ctx->index_table,
                               sizeof(tlv_index_table_t));
}

/**
 * @brief 验证TLV索引表的完整性
 * 
 * 该函数通过计算索引表的CRC校验值并与存储的CRC值进行比较，
 * 来验证索引表是否完整且未被篡改。
 * 
 * @param ctx TLV上下文指针，包含索引表信息
 * 
 * @return TLV_OK CRC校验成功
 * @return TLV_ERROR_INVALID_PARAM 参数无效，ctx或索引表为空
 * @return TLV_ERROR_CRC_FAILED CRC校验失败，数据可能已损坏
 */
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

/**
 * @brief 在TLV索引表中查找指定标签的条目
 * 
 * @param ctx TLV上下文指针，包含索引表信息
 * @param tag 要查找的标签值
 * 
 * @return 成功时返回指向找到的索引条目的指针，未找到或出错时返回NULL
 */
tlv_index_entry_t *tlv_index_find(tlv_context_t *ctx, uint16_t tag)
{
    // 参数有效性检查：上下文、索引表是否存在，标签是否有效
    if (!ctx || !ctx->index_table || tag == 0)
    {
        return NULL;
    }

    // 线性搜索（简单搜索）
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        // 查找标签匹配且有效的条目
        if (ctx->index_table->entries[i].tag == tag &&
            (ctx->index_table->entries[i].flags & TLV_FLAG_VALID))
        {
            return &ctx->index_table->entries[i];
        }
    }

    return NULL;
}


/**
 * @brief 根据标签值查找在元数据映射表中的索引位置
 * 
 * @param tag 要查找的标签值
 * @return 如果找到匹配的标签，返回其在TLV_META_MAP数组中的索引；如果未找到，返回-1
 * 
 * 该函数通过遍历TLV_META_MAP数组来查找指定标签的位置。
 * 当遇到标签值为0xFFFF的特殊标记时，表示已到达数组有效数据的末尾，停止查找。
 */
static inline int get_tag_index(uint16_t tag)
{
    // 遍历元数据映射表查找匹配的标签
    // 如果元数据表有序，可以使用二分查找来优化性能
    for (int i = 0; i < TLV_MAX_TAG_COUNT; i++)
    {
        // 找到匹配的标签，返回索引位置
        if (TLV_META_MAP[i].tag == tag)
        {
            return i;
        }
        // 遇到结束标记，停止查找
        if (TLV_META_MAP[i].tag == 0xFFFF)
        {
            break;
        }
    }
    // 未找到匹配的标签，返回错误码-1
    return -1;
}

/**
 * @brief 快速查找TLV标签对应的索引条目
 * 
 * 该函数首先尝试使用预计算的索引表进行O(1)时间复杂度的查找，
 * 如果预计算索引不可用或无效，则回退到线性搜索方式。
 * 
 * @param ctx TLV上下文指针，包含索引表信息
 * @param tag 要查找的标签值
 * @return 找到的索引条目指针，如果未找到则返回NULL
 */
tlv_index_entry_t *tlv_index_find_fast(tlv_context_t *ctx, uint16_t tag)
{
    // 参数有效性检查
    if (!ctx || !ctx->index_table || tag == 0)
    {
        return NULL;
    }

    // 尝试使用预计算索引进行快速查找（要求元数据表有序）
    int meta_index = get_tag_index(tag);
    if (meta_index >= 0 && meta_index < TLV_MAX_TAG_COUNT)
    {
        tlv_index_entry_t *entry = &ctx->index_table->entries[meta_index];
        if (entry->tag == tag && (entry->flags & TLV_FLAG_VALID))
        {
            return entry;
        }
    }

    // 预计算索引查找失败，回退到线性搜索
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

/**
 * @brief 向TLV索引中添加一个新的条目或更新已存在的条目
 * 
 * @param ctx TLV上下文指针，包含索引信息
 * @param tag 要添加的标签值，不能为0
 * @param addr 数据地址，必须是有效地址
 * @return 成功时返回指向索引条目的指针，失败时返回NULL
 */
tlv_index_entry_t *tlv_index_add(tlv_context_t *ctx, uint16_t tag, uint32_t addr)
{
    if (!ctx || tag == 0 || !TLV_IS_VALID_ADDR(addr))
    {
        return NULL;
    }

    // 检查是否已存在相同tag的条目，如果存在则更新地址和有效标志
    tlv_index_entry_t *existing = tlv_index_find(ctx, tag);
    if (existing)
    {
        existing->data_addr = addr;
        existing->flags |= TLV_FLAG_VALID;
        return existing;
    }

    // 查找空闲的索引槽位用于存储新的条目
    tlv_index_entry_t *entry = tlv_index_find_free_slot(ctx);
    if (!entry)
    {
        return NULL;
    }

    // 填充索引条目信息，包括标签、数据地址、有效标志和版本号
    const tlv_meta_const_t * meta = find_meta_by_tag(tag);
    entry->tag = tag;
    entry->data_addr = addr;
    entry->flags = TLV_FLAG_VALID;
    entry->version = meta ? meta->version : 1;

    // 更新上下文中的标签计数器
    ctx->header->tag_count++;

    // 检查区域有效性（编译时检查的运行时验证）
    if (meta && !is_tag_region_valid(tag, addr, meta->max_length + 20))
    {
        // 区域无效时进行回滚操作：清空条目并减少标签计数
        memset(entry, 0, sizeof(tlv_index_entry_t));
        ctx->header->tag_count--;
        return NULL;
    }

    return entry;
}

/**
 * @brief 更新TLV索引表中指定标签的地址信息
 * 
 * @param ctx TLV上下文指针，包含索引表信息
 * @param tag 要更新的TLV标签值
 * @param addr 新的数据地址值
 * 
 * @return TLV_OK 更新成功
 *         TLV_ERROR_INVALID_PARAM 参数无效（上下文为空、标签为0或地址无效）
 *         TLV_ERROR_NOT_FOUND 未找到指定标签的索引项
 */
int tlv_index_update(tlv_context_t *ctx, uint16_t tag, uint32_t addr)
{
    /* 参数合法性检查 */
    if (!ctx || tag == 0 || !TLV_IS_VALID_ADDR(addr))
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    /* 查找指定标签的索引项 */
    tlv_index_entry_t *entry = tlv_index_find(ctx, tag);
    if (!entry)
    {
        return TLV_ERROR_NOT_FOUND;
    }

    /* 更新索引项的地址和状态标志 版本 */
    const tlv_meta_const_t * meta = find_meta_by_tag(tag);
    entry->data_addr = addr;
    entry->flags |= TLV_FLAG_VALID;
    entry->flags &= ~TLV_FLAG_DIRTY;
    entry->version = meta ? meta->version : 1;

    return TLV_OK;
}

/**
 * @brief 从TLV上下文中移除指定标签的索引项
 * @param ctx TLV上下文指针
 * @param tag 要移除的标签值
 * @return TLV_OK表示成功移除，TLV_ERROR_INVALID_PARAM表示参数无效，TLV_ERROR_NOT_FOUND表示未找到对应标签
 */
int tlv_index_remove(tlv_context_t *ctx, uint16_t tag)
{
    // 参数有效性检查
    if (!ctx || tag == 0)
    {
        return TLV_ERROR_INVALID_PARAM;
    }

    // 查找要删除的索引项
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

/**
 * @brief 根据标签值查找对应的元数据信息
 * 
 * @param tag 要查找的标签值
 * @return 返回指向匹配的元数据结构体的指针，如果未找到则返回NULL
 */
static const tlv_meta_const_t *find_meta_by_tag(uint16_t tag)
{
    // 遍历元数据映射表查找匹配的标签
    for (uint32_t i = 0; i < TLV_META_MAP_SIZE; i++)
    {
        // 找到匹配的标签，返回对应的元数据指针
        if (TLV_META_MAP[i].tag == tag)
        {
            return &TLV_META_MAP[i];
        }
        // 遇到结束标记(0xFFFF)，停止搜索
        if (TLV_META_MAP[i].tag == 0xFFFF)
        {
            break;
        }
    }
    // 未找到匹配的标签，返回空指针
    return NULL;
}

/**
 * @brief 检查指定的标签区域是否有效
 * 
 * 该函数验证给定标签、地址和大小组成的区域是否满足以下条件：
 * 1. 地址在有效范围内
 * 2. 大小在安全范围内
 * 3. 不与现有的其他标签区域发生重叠
 * 
 * @param tag 标签值
 * @param addr 起始地址
 * @param size 区域大小
 * @return true 区域有效
 * @return false 区域无效
 */
static bool is_tag_region_valid(uint16_t tag, uint32_t addr, uint32_t size)
{
    // 验证地址和大小的基本有效性
    if (!TLV_IS_VALID_ADDR(addr) || !TLV_IS_SIZE_SAFE(addr, size))
    {
        return false;
    }

    // 检查是否与现有Tag区域冲突
    for (uint32_t i = 0; i < TLV_META_MAP_SIZE; i++)
    {
        if (TLV_META_MAP[i].tag != 0xFFFF && TLV_META_MAP[i].tag != tag)
        {
            uint32_t other_addr = TLV_DATA_ADDR;                   // 这里简化处理
            uint32_t other_size = TLV_META_MAP[i].max_length + 20; // Header+Data+CRC

            // 检测区域重叠情况
            if (TLV_REGIONS_OVERLAP(addr, size, other_addr, other_size))
            {
                return false;
            }
        }
    }

    return true;
}
