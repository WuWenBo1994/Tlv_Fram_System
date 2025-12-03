#ifndef __SYSTEM_CONFIG_VERSIONS_H
#define __SYSTEM_CONFIG_VERSIONS_H

// V1: 8字节
typedef struct
{
    uint32_t signature;
    uint16_t version;
    uint8_t language;
    uint8_t timezone;
} system_config_v1_t;

// V2: 32字节
typedef struct
{
    uint32_t signature;
    uint16_t version;
    uint8_t language;
    uint8_t timezone;
    uint32_t flags;    // 新增
    char product[16];  // 新增
    uint32_t reserved; // 预留
} system_config_v2_t;

/**
 * @brief 支持跨版本迁移：V1 → V2 → V3
 */

// V3: 56字节
typedef struct
{
    uint32_t signature;
    uint16_t version;
    uint8_t language;
    uint8_t timezone;
    uint32_t flags;
    char product[32];       // 扩展：16 → 32
    uint32_t serial_number; // 新增
    uint8_t hw_version;     // 新增
    uint8_t reserved[7];
} system_config_v3_t;



int migrate_system_config(void *data,
                          uint16_t old_len,
                          uint16_t *new_len,
                          uint16_t max_size,
                          uint8_t old_ver,
                          uint8_t new_ver);
#endif