/**
 * @file tlv_migration.h
 * @brief TLV数据版本迁移（简化版）
 */
 
#ifndef TLV_MIGRATION_H
#define TLV_MIGRATION_H
 
#ifdef __cplusplus
extern "C" {
#endif

#include "tlv_types.h"
 



/**
 * @brief 迁移单个Tag（内部函数）
 * @param tag Tag标识
 * @param data 数据缓冲区（输入旧数据，输出新数据）
 * @param old_len 旧数据长度
 * @param new_len 新数据长度（输出）
 * @param max_size 缓冲区最大容量
 * @param current_ver 当前版本
 * @return 0: 成功
 */
int tlv_migrate_tag(uint16_t tag,
                    void *data,
                    uint16_t old_len,
                    uint16_t *new_len,
                    uint16_t max_size,
                    uint8_t current_ver);

 /**
 * @brief 批量迁移所有Tag（可选）
 * @return 迁移的Tag数量，< 0: 错误
 */
int tlv_migrate_all(void);
 
/**
 * @brief 获取迁移统计信息（可选）
 */
int tlv_get_migration_stats(uint32_t *migrated, uint32_t *failed);
 
 
#ifdef __cplusplus
}
#endif
 
#endif /* TLV_MIGRATION_H */
