/**
 * @file tlv_migration.h
 * @brief TLV数据版本迁移管理
 */
 
#ifndef TLV_MIGRATION_H
#define TLV_MIGRATION_H
 
#include "tlv_types.h"
 
#ifdef __cplusplus
extern "C" {
#endif
 
/**
 * @brief 检查并迁移Tag数据
 * @param tag Tag标识
 * @param data 数据缓冲区（输入旧数据，输出新数据）
 * @param len 长度（输入/输出）
 * @param current_ver 当前版本
 * @return 0: 成功（无需迁移或迁移成功）, 其他: 错误码
 */
int tlv_migrate_tag(uint16_t tag, void *data, uint16_t *len, uint8_t current_ver);
 
/**
 * @brief 批量迁移所有Tag
 * @return 迁移的Tag数量
 */
int tlv_migrate_all(void);
 
/**
 * @brief 获取迁移统计信息
 * @param migrated 已迁移数量
 * @param failed 失败数量
 * @return 0: 成功
 */
int tlv_get_migration_stats(uint32_t *migrated, uint32_t *failed);
 
#ifdef __cplusplus
}
#endif
 
#endif /* TLV_MIGRATION_H */
