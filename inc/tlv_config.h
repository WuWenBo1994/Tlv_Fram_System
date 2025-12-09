/**
 * @file tlv_config.h
 * @brief TLV FRAM存储系统配置文件
 */
 
#ifndef TLV_CONFIG_H
#define TLV_CONFIG_H
 
#include <stdint.h>
#include <stdbool.h>
 
/* ============================ 版本信息 ============================ */
#define TLV_FILE_SYSTEM_VERSION   "1.3.0"

/* ============================ 基础配置 ============================ */
 
/** FRAM总大小 */
#define TLV_FRAM_SIZE                (128 * 1024)     // 128KB
 
/** 支持的最大Tag数量 */
#define TLV_MAX_TAG_COUNT            256       
 
/** 使用CRC16 */
#define TLV_USE_CRC16                1
 
/** 不使用线程安全（裸机环境） */
#define TLV_THREAD_SAFE              0
 
/** 启用版本兼容迁移 */
#define TLV_ENABLE_MIGRATION         1
  
/** 读取时惰性迁移 */
#define TLV_LAZY_MIGRATE_ON_READ     1

/** 启动时自动批量迁移（可选）*/
#define TLV_AUTO_MIGRATE_ON_BOOT     0

/** 使用碎片自动整理功能 */
#define TLV_AUTO_CLEAN_FRAGEMENT     1

/** 触发碎片整理的百分比 */
#define TLV_AUTO_DEFRAG_THRESHOLD    20

/** 调试模式     */
#define TLV_DEBUG                    0 

/** 使用断言     */
#define TLV_ENABLE_STATIC_ASSERT      1
/* ============================ 内存配置 ============================ */
 
/** 读写缓冲区大小（静态分配） */
#define TLV_BUFFER_SIZE              512
 
/* ============================ 地址配置 ============================ */
 
/** 系统Header起始地址 */
#define TLV_HEADER_ADDR              0x0000
 
/** Tag索引表起始地址 */
#define TLV_INDEX_ADDR               0x0200
 
/** 数据区起始地址 */
#define TLV_DATA_ADDR                0x1000
 
/** 备份区起始地址 */
#define TLV_BACKUP_ADDR              0x1F000
 
/** 备份区数据区大小   */
#define TLV_DATA_REGION_SIZE         (TLV_FRAM_SIZE - TLV_BACKUP_ADDR)

/* ============================ 系统版本 ============================ */
#define TLV_SYSTEM_VERSION          0x0100      // V1.0

/* ============================ 魔数定义 ============================ */
 
/** 系统魔数 */
#define DEFAULT_TLV_SYSTEM_MAGIC     0x544C5646  // "TLVF" Default Magic
#define WRGV_TLV_SYSTEM_MAGIC        0x57524756  // "WRGV"
#define LRGV_TLV_SYSTEM_MAGIC        0x4C524756  // "LRGV"

#define TLV_SYSTEM_MAGIC             DEFAULT_TLV_SYSTEM_MAGIC

/** 数据块魔数 */
#define TLV_BLOCK_MAGIC              0x44415441  // "DATA"
 
/* ============================ 错误代码 ============================ */
 
/** 成功 */
#define TLV_OK                       0
 
/** 通用错误 */
#define TLV_ERROR                   -1
 
/** 参数错误 */
#define TLV_ERROR_INVALID_PARAM     -2
 
/** 缓冲区内存不足 */
#define TLV_ERROR_NO_BUFFER_MEMORY  -3
 
/** 未找到 */
#define TLV_ERROR_NOT_FOUND         -4
 
/** CRC校验失败 */
#define TLV_ERROR_CRC_FAILED        -5
 
/** 版本不支持 */
#define TLV_ERROR_VERSION           -6
 
/** 内存空间不足 */
#define TLV_ERROR_NO_MEMORY_SPACE   -7
 
/** 索引空间不足 */
#define TLV_ERROR_NO_INDEX_SPACE    -8

/** 数据损坏 */
#define TLV_ERROR_CORRUPTED         -9

/* ============================ 编译检查 ============================ */
 
#if TLV_FRAM_SIZE < (64 * 1024)
    #error "FRAM size too small, minimum 64KB required"
#endif
 
#if TLV_MAX_TAG_COUNT > 256
    #error "Too many tags, maximum 256 supported (no hashtable)"
#endif

/* ============================ 断言检查 ============================ */
// 静态断言开关宏 - 默认开启，可通过编译器选项关闭
#ifndef TLV_ENABLE_STATIC_ASSERT
    #define TLV_ENABLE_STATIC_ASSERT 1
#endif
#if TLV_ENABLE_STATIC_ASSERT
    #if __STDC_VERSION__ >= 201112L
    // C11及以上版本支持标准_Static_assert
    #define STATIC_ASSERT_TAG(cond, msg) _Static_assert(cond, #msg)
    #define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
    #else
    // C11以下版本使用自定义静态断言
    // 两层宏展开确保__LINE__正确展开为行号
    #define CONCAT_IMPL(x, y, z) x##y##_##z
    #define CONCAT(x, y, z) CONCAT_IMPL(x, y, z)

    // 用于标识符风格的消息
    #define STATIC_ASSERT_TAG(cond, msg) \
        typedef char CONCAT(static_assert_, msg, __LINE__)[(cond) ? 1 : -1]

    // 用于字符串风格的消息""
    #define STATIC_ASSERT(cond, msg) \
        typedef char CONCAT(static_assert_msg_, __LINE__, unused)[(cond) ? 1 : -1]
    #endif
#else
    // 禁用静态断言
    #define STATIC_ASSERT_TAG(cond, msg)
    #define STATIC_ASSERT(cond, msg)
#endif

// 为了向后兼容，如果没有定义STATIC_ASSERT_MSG，则让它等同于STATIC_ASSERT
#ifndef STATIC_ASSERT
    #define STATIC_ASSERT(cond, msg) STATIC_ASSERT_TAG(cond, msg)
#endif


#if TLV_DEBUG
    #define TLV_ASSERT(cond) \
        do { \
            if (!(cond)) { \
                printf("TLV_ASSERT failed: %s:%d\n", __FILE__, __LINE__); \
                while (1); \
            } \
        } while (0)

    #define tlv_printf(...)   printf(__VA_ARGS__)
#else
    #define TLV_ASSERT(cond)
    #define tlv_printf(...)   
#endif

#endif /* TLV_CONFIG_H */
