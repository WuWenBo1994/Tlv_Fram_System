/**
 * @file test_migration.c
 * @brief 系统配置迁移测试
 */
 
#include "tlv_fram.h"
#include "system_config_versions.h"
#include <stdio.h>
#include <string.h>
 
void test_system_config_migration(void)
{
    printf("=== System Config Migration Test ===\n\n");
 
    tlv_init();
    tlv_format(0);
 
    // ========== 测试1：V1 → V2 → V3 完整迁移 ==========
    printf("Test 1: V1 → V2 → V3 Full Migration\n");
    printf("─────────────────────────────────────\n");
 
    // 1.1 写入V1数据（模拟旧固件）
    printf("Step 1: Write V1 data\n");
    system_config_v1_t config_v1 = {
        .signature = CONFIG_DEFAULT_SIGNATURE,
        .version = 0x0100,
        .language = 1,  // Chinese
        .timezone = 8   // UTC+8
    };
 
    // 临时改元数据版本为1（模拟旧固件）
    tlv_write(TAG_SYSTEM_CONFIG, &config_v1, sizeof(config_v1));
 
    // 手动设置索引版本为1
    tlv_index_entry_t *index = tlv_index_find(&g_tlv_ctx, TAG_SYSTEM_CONFIG);
    index->version = 1;
    tlv_index_save(&g_tlv_ctx);
 
    printf("  Written: V1, %u bytes\n", (unsigned)sizeof(config_v1));
    printf("  signature=0x%08lX, language=%u, timezone=%u\n\n",
           (unsigned long)config_v1.signature,
           config_v1.language, config_v1.timezone);
 
    // 1.2 读取并自动迁移到V3
    printf("Step 2: Read and auto-migrate to V3\n");
    system_config_v3_t config_v3;
    uint16_t len = sizeof(config_v3);
 
    int ret = tlv_read(TAG_SYSTEM_CONFIG, &config_v3, &len);
 
    if (ret == TLV_OK) {
        printf("  ✅ Migration successful\n");
        printf("  Final version: V3, %u bytes\n", len);
        printf("  signature=0x%08lX\n", (unsigned long)config_v3.signature);
        printf("  language=%u, timezone=%u\n",
               config_v3.language, config_v3.timezone);
        printf("  flags=0x%08lX\n", (unsigned long)config_v3.flags);
        printf("  product=\"%s\"\n", config_v3.product);
        printf("  serial=0x%08lX\n", (unsigned long)config_v3.serial_number);
        printf("  hw_version=%u\n", config_v3.hw_version);
 
        // 验证数据完整性
        bool ok = true;
        if (config_v3.signature != config_v1.signature) {
            printf("  ❌ signature mismatch\n");
            ok = false;
        }
        if (config_v3.language != config_v1.language) {
            printf("  ❌ language mismatch\n");
            ok = false;
        }
        if (config_v3.timezone != config_v1.timezone) {
            printf("  ❌ timezone mismatch\n");
            ok = false;
        }
        if (ok) {
            printf("  ✅ Data integrity OK\n");
        }
    } else {
        printf("  ❌ Migration failed: %d\n", ret);
    }
    printf("\n");
 
    // ========== 测试2：V2 → V3 迁移 ==========
    printf("Test 2: V2 → V3 Direct Migration\n");
    printf("─────────────────────────────────────\n");
 
    // 2.1 写入V2数据
    printf("Step 1: Write V2 data\n");
    system_config_v2_t config_v2 = {
        .signature = 0xDEADBEEF,
        .version = 0x0200,
        .language = 2,  // Japanese
        .timezone = 9,  // UTC+9
        .flags = CONFIG_FLAG_AUTO_SAVE | CONFIG_FLAG_DEBUG_MODE,
        .reserved = 0
    };
    strncpy(config_v2.product, "TestProduct", sizeof(config_v2.product));
 
    tlv_write(TAG_SYSTEM_CONFIG, &config_v2, sizeof(config_v2));
 
    // 手动设置索引版本为2
    index = tlv_index_find(&g_tlv_ctx, TAG_SYSTEM_CONFIG);
    index->version = 2;
    tlv_index_save(&g_tlv_ctx);
 
    printf("  Written: V2, %u bytes\n", (unsigned)sizeof(config_v2));
    printf("  product=\"%s\"\n\n", config_v2.product);
 
    // 2.2 读取并迁移到V3
    printf("Step 2: Read and migrate to V3\n");
    len = sizeof(config_v3);
    ret = tlv_read(TAG_SYSTEM_CONFIG, &config_v3, &len);
 
    if (ret == TLV_OK) {
        printf("  ✅ Migration successful\n");
        printf("  product=\"%s\" (expanded to 32B)\n", config_v3.product);
        printf("  serial=0x%08lX (new field)\n",
               (unsigned long)config_v3.serial_number);
        printf("  hw_version=%u (new field)\n", config_v3.hw_version);
 
        // 验证产品名称保留
        if (strncmp(config_v3.product, config_v2.product,
                    sizeof(config_v2.product)) == 0) {
            printf("  ✅ Product name preserved\n");
        } else {
            printf("  ❌ Product name corrupted\n");
        }
    } else {
        printf("  ❌ Migration failed: %d\n", ret);
    }
    printf("\n");
 
    // ========== 测试3：已是V3,不应迁移 ==========
    printf("Test 3: V3 → V3 (No Migration)\n");
    printf("─────────────────────────────────────\n");
 
    system_config_v3_t config_v3_before;
    memcpy(&config_v3_before, &config_v3, sizeof(config_v3));
 
    len = sizeof(config_v3);
    ret = tlv_read(TAG_SYSTEM_CONFIG, &config_v3, &len);
 
    if (ret == TLV_OK) {
        if (memcmp(&config_v3, &config_v3_before, sizeof(config_v3)) == 0) {
            printf("  ✅ No migration, data unchanged\n");
        } else {
            printf("  ❌ Data changed (should not happen!)\n");
        }
    } else {
        printf("  ❌ Read failed: %d\n", ret);
    }
    printf("\n");
 
    // ========== 测试4：缓冲区边界测试 ==========
    printf("Test 4: Buffer Size Tests\n");
    printf("─────────────────────────────────────\n");
 
    printf("  V1 size: %u bytes\n", (unsigned)sizeof(system_config_v1_t));
    printf("  V2 size: %u bytes\n", (unsigned)sizeof(system_config_v2_t));
    printf("  V3 size: %u bytes\n", (unsigned)sizeof(system_config_v3_t));
    printf("  MaxLen:  %u bytes\n", 64);
 
    if (sizeof(system_config_v3_t) <= 64) {
        printf("  ✅ All versions fit in MaxLen\n");
    } else {
        printf("  ❌ V3 exceeds MaxLen!\n");
    }
    printf("\n");
 
    printf("=== Test Complete ===\n");
}
 
/**
 * @brief 测试跨版本迁移（V1直接到V3）
 */
void test_cross_version_migration(void)
{
    printf("=== Cross-Version Migration Test ===\n\n");
 
    // 准备V1数据
    uint8_t buffer[64];
    system_config_v1_t *v1 = (system_config_v1_t *)buffer;
    v1->signature = 0x12345678;
    v1->version = 0x0100;
    v1->language = 0;
    v1->timezone = 0;
 
    uint16_t len = sizeof(system_config_v1_t);
    uint16_t new_len = 0;
 
    printf("Before: V1, %u bytes\n", len);
    printf("  signature=0x%08lX\n", (unsigned long)v1->signature);
 
    // 执行 V1 → V3 迁移（跨版本）
    int ret = migrate_system_config(buffer, len, &new_len, sizeof(buffer), 1, 3);
 
    if (ret == TLV_OK) {
        printf("\n✅ Cross-version migration successful\n");
        printf("After: V3, %u bytes\n", new_len);
 
        system_config_v3_t *v3 = (system_config_v3_t *)buffer;
        printf("  signature=0x%08lX\n", (unsigned long)v3->signature);
        printf("  product=\"%s\"\n", v3->product);
        printf("  serial=0x%08lX\n", (unsigned long)v3->serial_number);
    } else {
        printf("\n❌ Cross-version migration failed: %d\n", ret);
    }
 
    printf("\n=== Test Complete ===\n");
}
