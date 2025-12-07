/**
 * @file tlv_index.h
 * @brief TLV索引管理模块（简化版）
 */
 
#ifndef TLV_INDEX_H
#define TLV_INDEX_H
 
#include "tlv_types.h"
 
#ifdef __cplusplus
extern "C" {
#endif
 
/* ============================ 索引表管理 ============================ */
 
/**
 * @brief 初始化索引系统
 * @param ctx 全局上下文
 * @return 0: 成功, 其他: 错误码
 */
int tlv_index_init(const tlv_context_t *ctx);
 
/**
 * @brief 反初始化索引系统
 * @param ctx 全局上下文
 */
void tlv_index_deinit(const tlv_context_t *ctx);
 
/**
 * @brief 从FRAM加载索引表
 * @param ctx 全局上下文
 * @return 0: 成功, 其他: 错误码
 */
int tlv_index_load(const tlv_context_t *ctx);
 
/**
 * @brief 将索引表保存到FRAM
 * @param ctx 全局上下文
 * @return 0: 成功, 其他: 错误码
 */
int tlv_index_save(const tlv_context_t *ctx);
 
/**
 * @brief 校验索引表完整性
 * @param ctx 全局上下文
 * @return 0: 成功, 其他: 错误码
 */
int tlv_index_verify(const tlv_context_t *ctx);
 
/* ============================ 索引查找 ============================ */
 
/**
 * @brief 查找Tag索引（线性搜索）
 * @param ctx 全局上下文
 * @param tag Tag值
 * @return 索引指针,NULL表示未找到
 */
tlv_index_entry_t* tlv_index_find(const tlv_context_t *ctx, uint16_t tag);
 
/**
 * @brief 快速查找（优化版）
 * @param ctx 全局上下文
 * @param tag Tag值
 * @return 索引指针,NULL表示未找到
 */
tlv_index_entry_t* tlv_index_find_fast(const tlv_context_t *ctx, uint16_t tag);
 
/**
 * @brief 查找空闲索引槽位
 * @param ctx 全局上下文
 * @return 索引指针,NULL表示已满
 */
tlv_index_entry_t* tlv_index_find_free_slot(const tlv_context_t *ctx);
 
/* ============================ 索引操作 ============================ */
 
/**
 * @brief 添加新索引
 * @param ctx 全局上下文
 * @param tag Tag值
 * @param addr 数据地址
 * @return 索引指针,NULL表示失败
 */
tlv_index_entry_t* tlv_index_add(const tlv_context_t *ctx, uint16_t tag, uint32_t addr);
 
/**
 * @brief 更新索引
 * @param ctx 全局上下文
 * @param tag Tag值
 * @param addr 新数据地址
 * @return 0: 成功, 其他: 错误码
 */
int tlv_index_update(const tlv_context_t *ctx, uint16_t tag, uint32_t addr);
 
/**
 * @brief 删除索引
 * @param ctx 全局上下文
 * @param tag Tag值
 * @return 0: 成功, 其他: 错误码
 */
int tlv_index_remove(const tlv_context_t *ctx, uint16_t tag);
 
#ifdef __cplusplus
}
#endif
 
#endif /* TLV_INDEX_H */
