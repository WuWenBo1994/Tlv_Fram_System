#ifndef TLV_META_TABLE_H
#define TLV_META_TABLE_H
 
#include "tlv_types.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif


/* ============================ 内联函数 ============================ */
/**
 * @brief 根据TLV标签值获取对应的标签名称
 *
 * @param tag 要查询的TLV标签值
 * @return 返回标签对应的名称字符串,如果未找到或名称为空则返回"Unknown"
 */
static inline const char *tlv_get_tag_name(const tlv_meta_const_t *meta_table, uint16_t tag)
{
    // 遍历TLV元数据映射表查找匹配的标签
    for (int i = 0; meta_table[i].tag != 0xFFFF; i++)
    {
        // 找到匹配的标签时,返回对应的名称
        if (meta_table[i].tag == tag)
        {
            return meta_table[i].name ? meta_table[i].name : "Unknown";
        }
    }
    // 未找到匹配的标签,返回默认名称
    return "Unknown";
}

/**
 * @brief 根据标签值获取对应TLV项的最大长度
 *
 * @param tag 要查询的标签值
 * @return 返回对应标签的最大长度,如果未找到则返回0
 */
static inline uint16_t tlv_get_tag_max_length(const tlv_meta_const_t *meta_table, uint16_t tag)
{
    // 遍历TLV元数据映射表查找匹配的标签
    for (int i = 0; meta_table[i].tag != 0xFFFF; i++)
    {
        // 找到匹配的标签,返回其最大长度
        if (meta_table[i].tag == tag)
        {
            return meta_table[i].max_length;
        }
    }
    // 未找到匹配的标签,返回0
    return 0;
}

/**
 * @brief 根据名称查找对应的TLV标签值
 *
 * @param name 要查找的TLV项名称
 * @return 返回找到的标签值,如果未找到或输入为空则返回0xFFFF
 */
static inline uint16_t tlv_find_tag_by_name(const tlv_meta_const_t *meta_table, const char *name)
{
    // 检查输入参数有效性
    if (!name)
        return 0xFFFF;

    // 遍历TLV元数据映射表查找匹配的名称
    for (int i = 0; meta_table[i].tag != 0xFFFF; i++)
    {
        // 比较当前项名称与目标名称是否匹配
        if (meta_table[i].name && strcmp(meta_table[i].name, name) == 0)
        {
            return meta_table[i].tag;
        }
    }
    // 未找到匹配项,返回无效标签值
    return 0xFFFF;
}
 
/* ============================ 外部声明 ============================ */
const tlv_meta_const_t* tlv_get_meta_table(void);
int tlv_get_meta_table_size(void);

#ifdef __cplusplus
}
#endif
 
#endif /* TLV_META_TABLE_H */
