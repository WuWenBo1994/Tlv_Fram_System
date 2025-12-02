/**
 * @file tlv_types.h
 * @brief TLV FRAM存储系统数据结构定义（简化版）
 */
 
#ifndef TLV_TYPES_H
#define TLV_TYPES_H
 
#include "tlv_config.h"
#include <stdint.h>
#include <stdbool.h>
 
/* ============================ 基础类型 ============================ */
 
/** TLV状态枚举 */
typedef enum {
    TLV_STATE_UNINITIALIZED = 0,
    TLV_STATE_INITIALIZED,
    TLV_STATE_ERROR,
    TLV_STATE_FORMATTED
} tlv_state_t;
 
/** 初始化结果枚举 */
typedef enum {
    TLV_INIT_FIRST_BOOT = 0,
    TLV_INIT_OK,
    TLV_INIT_RECOVERED,
    TLV_INIT_ERROR
} tlv_init_result_t;
 
/** TLV标志位定义 */
typedef enum {
    TLV_FLAG_VALID      = 0x0001,
    TLV_FLAG_DIRTY      = 0x0002,
    TLV_FLAG_BACKUP     = 0x0004,
    TLV_FLAG_ENCRYPTED  = 0x0008,
    TLV_FLAG_CRITICAL   = 0x0010,
} tlv_flag_t;
 
/* ============================ 系统管理结构 ============================ */
 
/** 系统Header结构（256字节） */
typedef struct __attribute__((packed)) {
    uint32_t magic;                 // 魔数 0x544C5646
    uint16_t version;               // 格式版本号
    uint16_t tag_count;             // 当前使用的Tag数量
    uint32_t data_region_start;     // 数据区起始地址
    uint32_t data_region_size;      // 数据区大小
    uint32_t next_free_addr;        // 下一个可分配地址
    uint32_t total_writes;          // 总写入次数
    uint32_t last_update_time;      // 最后更新时间戳
    uint32_t free_space;            // 可用空间
    uint32_t used_space;            // 已用空间
    uint8_t  reserved[220];         // 保留扩展
    uint16_t header_crc16;          // Header自身CRC16（改为2字节）
} tlv_system_header_t;
 
/** Tag索引表项结构（6字节，简化） */
typedef struct __attribute__((packed)) {
    uint16_t tag;                    // Tag值（0x0000为无效）
    uint8_t  flags;                  // 状态标志（1字节）
    uint8_t  version;                // 数据版本号
    uint32_t data_addr;              // 数据块在FRAM中的绝对地址
} tlv_index_entry_t;
 
/** Tag索引表结构（简化，1536字节） */
typedef struct __attribute__((packed)) {
    tlv_index_entry_t entries[TLV_MAX_TAG_COUNT];
    uint16_t index_crc16;            // 索引表CRC16
} tlv_index_table_t;
 
/* ============================ 数据块结构 ============================ */
 
/** TLV数据块Header结构（14字节，CRC16版本） */
typedef struct __attribute__((packed)) {
    uint16_t tag;                    // Tag值
    uint16_t length;                 // 实际数据长度
    uint8_t  version;                // 数据版本号
    uint8_t  flags;                  // 块标志
    uint32_t timestamp;              // 写入时间戳
    uint32_t write_count;            // 写入计数
} tlv_data_block_header_t;
 
/** 完整的TLV数据块,永远不会实例化这个数组，仅表示数据结构 */
typedef struct __attribute__((packed)) {
    tlv_data_block_header_t header;  // 数据块Header (14字节)
    uint8_t  data[];                 // 变长数据
    // 数据后紧跟：uint16_t crc16 (2字节)
} tlv_data_block_t;

/* ============================ 数据迁移结构 ============================ */
/**
 * @brief 版本迁移函数指针
 * @param old_data 旧版本数据
 * @param old_len 旧版本长度
 * @param new_data 新版本数据缓冲区
 * @param new_len 新版本缓冲区大小（输入），实际长度（输出）
 * @param old_ver 旧版本号
 * @param new_ver 新版本号
 * @return 0: 成功, 其他: 错误码
 */
typedef int (*tlv_migration_func_t)(const void *old_data, uint16_t old_len,
                                     void *new_data, uint16_t *new_len,
                                     uint8_t old_ver, uint8_t new_ver);
 
/**
 * @brief 版本迁移规则
 */
typedef struct {
    uint16_t tag;                  // Tag标识
    uint8_t  from_version;         // 源版本
    uint8_t  to_version;           // 目标版本
    tlv_migration_func_t migrate;  // 迁移函数
} tlv_migration_rule_t;
 
/* ============================ 元数据结构 ============================ */
 
/** 元数据常量表项结构（简化） */
typedef struct {
    uint16_t tag;                    // Tag标识
    uint16_t max_length;             // 最大数据长度
    uint8_t  priority;               // 优先级(0-255)
    uint8_t  version;                // 数据结构版本
    uint8_t  backup_enable;          // 是否需要备份
    const char *name;                // 描述名称(调试用)
    tlv_migration_func_t migrate;    // 迁移函数（可选）
} tlv_meta_const_t;
 
/** 运行时信息结构（简化） */
typedef struct {
    tlv_index_entry_t *index_ptr;    // 指向FRAM索引表项
    const tlv_meta_const_t *meta;    // 指向常量元数据
} tlv_runtime_info_t;
 
/** 全局上下文结构（简化，无malloc） */
typedef struct {
    tlv_state_t state;               // 系统状态
    tlv_system_header_t *header;     // 系统Header指针
    tlv_index_table_t *index_table;  // 索引表指针
    const tlv_meta_const_t *meta_table; // 元数据表
    uint16_t meta_table_size;        // 元数据表大小
    
    // 静态分配的缓冲区，不使用malloc
    uint8_t static_buffer[TLV_BUFFER_SIZE];
} tlv_context_t;
 
/* ============================ 统计结构 ============================ */
 
/** TLV统计信息 */
typedef struct {
    uint32_t total_tags;             // 总Tag数
    uint32_t valid_tags;             // 有效Tag数
    uint32_t dirty_tags;             // 脏数据Tag数
    uint32_t free_space;             // 可用空间
    uint32_t used_space;             // 已用空间
    uint32_t fragmentation;          // 碎片化程度
    uint32_t corruption_count;       // 损坏计数
} tlv_statistics_t;
 
/* ============================ 地址范围检查宏 ============================ */
 
/** 检查地址是否在有效范围内 */
#define TLV_IS_VALID_ADDR(addr) \
    ((addr) >= TLV_DATA_ADDR && (addr) < TLV_BACKUP_ADDR)
 
/** 检查大小是否会导致越界 */
#define TLV_IS_SIZE_SAFE(addr, size) \
    ((addr) >= TLV_DATA_ADDR && (addr) + (size) <= TLV_BACKUP_ADDR)
 
/** 检查两个区域是否重叠 */
#define TLV_REGIONS_OVERLAP(start1, size1, start2, size2) \
    (((start1) <= (start2) && (start2) < ((start1) + (size1))) || \
     ((start2) <= (start1) && (start1) < ((start2) + (size2))))
 




#endif /* TLV_TYPES_H */
