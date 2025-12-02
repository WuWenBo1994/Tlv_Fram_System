/**
 * @file system_config_versions.h
 * @brief 系统配置数据结构版本定义
 */
 
// ========== 版本1（固件v1.0） ==========
typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint16_t version;
    uint8_t  language;
    uint8_t  timezone;
} system_config_v1_t;  // 8字节
 
// ========== 版本2（固件v2.0） ==========
typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint16_t version;
    uint8_t  language;
    uint8_t  timezone;
    uint32_t flags;        // 新增：功能标志
    char     product[16];  // 新增：产品名称
    uint32_t reserved;     // 预留
} system_config_v2_t;  // 32字节
 
// ========== 版本3（固件v3.0） ==========
typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint16_t version;
    uint8_t  language;
    uint8_t  timezone;
    uint32_t flags;
    char     product[32];  // 扩展：产品名称从16→32
    uint32_t serial_number;// 新增：序列号
    uint8_t  hw_version;   // 新增：硬件版本
    uint8_t  reserved[7];  // 对齐
} system_config_v3_t;  // 56字节
