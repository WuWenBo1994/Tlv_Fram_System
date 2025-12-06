/**
 * @file tlv_utils.h
 * @brief TLV FRAM存储系统工具函数声明（CRC16版）
 */
 
#ifndef TLV_UTILS_H
#define TLV_UTILS_H
 
#include "tlv_config.h"
#include "tlv_types.h"
#include <stdint.h>
#include <stdbool.h>
 
/* ============================ CRC16校验 ============================ */
 
/**
 * @brief 计算CRC16初始化值
 * @return CRC16初始值
 */
uint16_t tlv_crc16_init(void);
 
/**
 * @brief 更新CRC16计算
 * @param crc 当前CRC值
 * @param data 数据指针
 * @param size 数据大小
 * @return 更新后的CRC值
 */
uint16_t tlv_crc16_update(uint16_t crc, const void *data, uint32_t size);
 
/**
 * @brief 完成CRC16计算
 * @param crc 当前CRC值
 * @return 最终CRC值
 */
uint16_t tlv_crc16_final(uint16_t crc);
 
/**
 * @brief 一次性计算CRC16
 * @param data 数据指针
 * @param size 数据大小
 * @return CRC16值
 */
uint16_t tlv_crc16(const void *data, uint32_t size);
 
/* ============================ 字节序转换 ============================ */
 
/**
 * @brief 小端转大端16位
 * @param value 值
 * @return 转换后的值
 */
uint16_t tlv_htobe16(uint16_t value);
 
/**
 * @brief 大端转小端16位
 * @param value 值
 * @return 转换后的值
 */
uint16_t tlv_betoh16(uint16_t value);
 
/**
 * @brief 小端转大端32位
 * @param value 值
 * @return 转换后的值
 */
uint32_t tlv_htobe32(uint32_t value);
 
/**
 * @brief 大端转小端32位
 * @param value 值
 * @return 转换后的值
 */
uint32_t tlv_betoh32(uint32_t value);
 
/* ============================ 对齐操作 ============================ */
/**
 * @brief 向上对齐到指定边界
 * @param size 原始大小
 * @param align 对齐边界
 * @return 对齐后大小
 */
uint32_t tlv_align_up(uint32_t size, uint32_t align);
 
/**
 * @brief 检查是否对齐
 * @param addr 地址
 * @param align 对齐边界
 * @return true: 已对齐, false: 未对齐
 */
bool tlv_is_aligned(uint32_t addr, uint32_t align);
 
/* ============================ 安全内存操作 ============================ */
 
/**
 * @brief 安全内存复制（带边界检查）
 * @param dst 目标缓冲区
 * @param dst_size 目标缓冲区大小
 * @param src 源缓冲区
 * @param src_size 源数据大小
 * @return 实际复制大小
 */
uint32_t tlv_memcpy_safe(void *dst, uint32_t dst_size, 
                        const void *src, uint32_t src_size);
 
/**
 * @brief 安全内存设置（带边界检查）
 * @param dst 目标缓冲区
 * @param dst_size 目标缓冲区大小
 * @param value 设置值
 * @param size 设置大小
 * @return 实际设置大小
 */
uint32_t tlv_memset_safe(void *dst, uint32_t dst_size, 
                        uint8_t value, uint32_t size);
 
/* ============================ 版本比较 ============================ */
 
/**
 * @brief 比较版本号
 * @param v1 版本1
 * @param v2 版本2
 * @return 0: 相等, <0: v1<v2, >0: v1>v2
 */
int tlv_version_compare(uint16_t v1, uint16_t v2);
 
/**
 * @brief 检查版本兼容性
 * @param current 当前版本
 * @param required 要求版本
 * @return true: 兼容, false: 不兼容
 */
bool tlv_version_compatible(uint16_t current, uint16_t required);
 
/* ============================ 时间工具 ============================ */
 
/**
 * @brief 计算时间差（毫秒）
 * @param start 起始时间戳
 * @param end 结束时间戳
 * @return 时间差（毫秒）
 */
uint32_t tlv_time_diff(uint32_t start, uint32_t end);
 
#endif /* TLV_UTILS_H */
