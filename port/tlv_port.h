/**
 * @file tlv_port.h
 * @brief TLV FRAM存储系统硬件移植层接口（裸机简化版）
 */
 
#ifndef TLV_PORT_H
#define TLV_PORT_H
 
#include "tlv_config.h"
#include <stdint.h>
 
#ifdef __cplusplus
extern "C" {
#endif
 
/* ============================ FRAM硬件接口 ============================ */
 
/**
 * @brief 初始化FRAM硬件接口
 * @return 0: 成功, 其他: 错误码
 */
int tlv_port_fram_init(void);
 
/**
 * @brief 读取FRAM数据
 * @param addr FRAM地址（相对地址,从0开始）
 * @param data 数据缓冲区
 * @param size 读取大小
 * @return 0: 成功, 其他: 错误码
 */
int tlv_port_fram_read(uint32_t addr, void *data, uint32_t size);
 
/**
 * @brief 写入FRAM数据
 * @param addr FRAM地址（相对地址,从0开始）
 * @param data 数据缓冲区
 * @param size 写入大小
 * @return 0: 成功, 其他: 错误码
 */
int tlv_port_fram_write(uint32_t addr, const void *data, uint32_t size);
 
/* ============================ 时间接口 ============================ */
 
/**
 * @brief 获取当前时间戳（秒）
 * @return 时间戳（可以是启动后的秒数或实际RTC时间）
 */
uint32_t tlv_port_get_timestamp_s(void);
 
/**
 * @brief 获取当前时间戳（毫秒）
 * @return 时间戳（可以是启动后的毫秒数）
 */
uint32_t tlv_port_get_timestamp_ms(void);
 
#ifdef __cplusplus
}
#endif
 
#endif /* TLV_PORT_H */
