/**
 * @file tlv_fram.h
 * @brief TLV FRAM存储系统对外API接口（裸机简化版）
 */
 
#ifndef TLV_FRAM_H
#define TLV_FRAM_H
 
#include "tlv_config.h"
#include "tlv_types.h"
 
#ifdef __cplusplus
extern "C" {
#endif
 
/* ============================ 系统管理API ============================ */
/**
 * @brief 获取tlv系统版本
 * @return 系统版本
 */
const char* tlv_get_version(void); 

/**
 * @brief 初始化TLV存储系统
 * @return 初始化结果
 */
tlv_init_result_t tlv_init(void);
 
/**
 * @brief 反初始化TLV存储系统
 * @return 0: 成功, 其他: 错误码
 */
int tlv_deinit(void);
 
/**
 * @brief 格式化FRAM存储区
 * @param magic 魔数（可选,0使用默认）
 * @return 0: 成功, 其他: 错误码
 */
int tlv_format(uint32_t magic);
 
/**
 * @brief 获取系统状态
 * @return 系统状态
 */
tlv_state_t tlv_get_state(void);
 
/* ============================ 数据操作API ============================ */
 
/**
 * @brief 写入TLV数据
 * @param tag Tag值
 * @param data 数据指针
 * @param len 数据长度
 * @return 实际写入长度,负数表示错误
 */
int tlv_write(uint16_t tag, const void *data, uint16_t len);
 
/**
 * @brief 读取TLV数据
 * @param tag Tag值
 * @param buf 输出缓冲区
 * @param len 缓冲区大小（输入）,实际读取大小（输出）
 * @return 0: 成功, 其他: 错误码
 */
int tlv_read(uint16_t tag, void *buf, uint16_t *len);
 
/**
 * @brief 删除TLV数据
 * @param tag Tag值
 * @return 0: 成功, 其他: 错误码
 */
int tlv_delete(uint16_t tag);

/**
 * @brief 强制保存所有挂起的更改
 * @return 0: 成功, 其他: 错误码
 */
int tlv_flush(void);
 
/**
 * @brief 检查Tag是否存在
 * @param tag Tag值
 * @return true: 存在, false: 不存在
 */
bool tlv_exists(uint16_t tag);
 
/**
 * @brief 获取Tag数据长度
 * @param tag Tag值
 * @param len 输出长度
 * @return 0: 成功, 其他: 错误码
 */
int tlv_get_length(uint16_t tag, uint16_t *len);
 
/* ============================ 批量操作API ============================ */
 
/**
 * @brief 批量读取
 * @param tags Tag数组
 * @param count Tag数量
 * @param buffers 缓冲区数组
 * @param lengths 长度数组
 * @return 成功读取的数量
 */
int tlv_read_batch(const uint16_t *tags, uint16_t count, 
                   void **buffers, uint16_t *lengths);
 
/**
 * @brief 批量写入
 * @param tags Tag数组
 * @param count Tag数量
 * @param datas 数据数组
 * @param lengths 长度数组
 * @return 成功写入的数量
 */
int tlv_write_batch(const uint16_t *tags, uint16_t count,
                    const void **datas, const uint16_t *lengths);
 
/* ============================ 查询与统计API ============================ */
 
/**
 * @brief 获取统计信息
 * @param stats 统计信息输出
 * @return 0: 成功, 其他: 错误码
 */
int tlv_get_statistics(tlv_statistics_t *stats);
 
/**
 * @brief 遍历所有Tag
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 遍历的Tag数量
 */
typedef void (*tlv_foreach_callback_t)(uint16_t tag, void *user_data);
int tlv_foreach(tlv_foreach_callback_t callback, void *user_data);
 
/* ============================ 维护管理API ============================ */
 
/**
 * @brief 碎片整理
 * @return 0: 成功, 其他: 错误码
 */
int tlv_defragment(void);
 
/**
 * @brief 校验所有数据
 * @param corrupted_count 输出损坏数量
 * @return 0: 成功, 其他: 错误码
 */
int tlv_verify_all(uint32_t *corrupted_count);
 
/**
 * @brief 备份所有数据到备份区
 * @return 0: 成功, 其他: 错误码
 */
int tlv_backup_all(void);
 
/**
 * @brief 从备份区恢复数据
 * @return 0: 成功, 其他: 错误码
 */
int tlv_restore_from_backup(void);
 
/* ============================ 空间管理API ============================ */
 
/**
 * @brief 获取可用空间
 * @param free_space 输出可用空间
 * @return 0: 成功, 其他: 错误码
 */
int tlv_get_free_space(uint32_t *free_space);
 
/**
 * @brief 获取已用空间
 * @param used_space 输出已用空间
 * @return 0: 成功, 其他: 错误码
 */
int tlv_get_used_space(uint32_t *used_space);
 
/**
 * @brief 计算碎片化程度
 * @param fragmentation_percent 输出碎片化百分比
 * @return 0: 成功, 其他: 错误码
 */
int tlv_calculate_fragmentation(uint32_t *fragmentation_percent);
 
#ifdef __cplusplus
}
#endif
 
#endif /* TLV_FRAM_H */
