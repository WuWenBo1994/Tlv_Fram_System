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
/* ============================ 流操作API ============================ */

/**
 * @brief 开始分段写入
 * @param tag Tag值
 * @param total_len 总数据长度
 * @return 写入句柄（TLV_INVALID_HANDLE 表示失败，通过 tlv_get_last_error() 获取错误码）
 */
tlv_stream_handle_t tlv_write_begin(uint16_t tag, uint16_t total_len);

/**
 * @brief 写入数据段
 * @param handle 写入句柄
 * @param data 数据指针
 * @param len 数据长度
 * @return TLV_OK: 成功, 其他: 错误码
 */
int tlv_write_chunk(tlv_stream_handle_t handle, const void *data, uint16_t len);

/**
 * @brief 完成分段写入
 * @param handle 写入句柄
 * @return TLV_OK: 成功, 其他: 错误码
 */
int tlv_write_end(tlv_stream_handle_t handle);


/**
 * @brief 取消分段写入
 * @param handle 写入句柄
 */
void tlv_write_abort(tlv_stream_handle_t handle);



/**
 * @brief 开始分段读取
 * @param tag Tag值
 * @param total_len 输出总数据长度
 * @return 读取句柄（TLV_INVALID_HANDLE 表示失败）
 */
tlv_stream_handle_t tlv_read_begin(uint16_t tag, uint16_t *total_len);

/**
 * @brief 读取数据段
 * @param handle 读取句柄
 * @param buf 输出缓冲区
 * @param len 请求读取长度
 * @return 实际读取长度（>=0 成功，<0 错误码）
 */
int tlv_read_chunk(tlv_stream_handle_t handle, void *buf, uint16_t *len);

/**
 * @brief 完成分段读取
 * @param handle 读取句柄
 * @return TLV_OK: 成功, 其他: 错误码
 */
int tlv_read_end(tlv_stream_handle_t handle);

/**
 * @brief 取消分段读取
 * @param handle 读取句柄
 */
void tlv_read_abort(tlv_stream_handle_t handle);

/* ============================ 错误处理API ============================ */
 
/**
 * @brief 获取最后一次错误码
 * @return 错误码（0 表示无错误）
 */
int tlv_get_last_error(void);
 
/**
 * @brief 获取最后一次错误的详细信息
 * @param error_ctx 输出错误上下文（可选，传NULL只返回错误码）
 * @return 错误码
 */
int tlv_get_last_error_ex(tlv_error_context_t *error_ctx);
 
/**
 * @brief 清除错误状态
 */
void tlv_clear_error(void);
 
/**
 * @brief 获取错误码对应的描述字符串
 * @param error_code 错误码
 * @return 错误描述字符串
 */
const char *tlv_get_error_string(int error_code);
 
#if TLV_ENABLE_ERROR_TRACKING
/**
 * @brief 获取错误历史记录
 * @param history 输出缓冲区
 * @param count 缓冲区大小（输入），实际记录数（输出）
 * @return TLV_OK: 成功
 */
int tlv_get_error_history(tlv_error_context_t *history, uint8_t *count);
 
/**
 * @brief 清除错误历史
 */
void tlv_clear_error_history(void);
#endif

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
