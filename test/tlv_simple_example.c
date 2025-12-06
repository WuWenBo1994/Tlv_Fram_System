/**
 * @file tlv_simple_example.c
 * @brief TLV系统简化版使用示例（纯裸机）
 */
 
#include "tlv_file_system.h"
#include <string.h>
 
// 假设这些是你在port.c中实现的硬件接口
extern int fram_init(void);
extern int fram_read(uint32_t addr, void *data, uint32_t size);
extern int fram_write(uint32_t addr, const void *data, uint32_t size);
 
// 移植层实现（简化）
int tlv_port_fram_init(void) {
    return fram_init();
}
 
int tlv_port_fram_read(uint32_t addr, void *data, uint32_t size) {
    return fram_read(addr, data, size);
}
 
int tlv_port_fram_write(uint32_t addr, const void *data, uint32_t size) {
    return fram_write(addr, data, size);
}
 
uint32_t tlv_port_get_timestamp_s(void) {
    // 实现你的时间戳获取
    return 0;
}

 
void example_usage(void)
{
    // 初始化
    tlv_init_result_t init_result = tlv_init();
    if (init_result == TLV_INIT_FIRST_BOOT) {
        tlv_format(0);
        init_result = tlv_init();
        if (init_result != TLV_INIT_OK) {
            // 初始化失败
            return;
        }
    }
 
    // 写入系统配置
    uint32_t config = 0x12345678;
    tlv_write(TAG_SYSTEM_CONFIG, &config, sizeof(config));
 
    // 读取系统配置
    uint32_t read_config;
    uint16_t len = sizeof(read_config);
    tlv_read(TAG_SYSTEM_CONFIG, &read_config, &len);
 
    // 批量操作（简化）
    float offsets[3] = {1.0f, 2.0f, 3.0f};
    uint16_t tags[] = {TAG_SENSOR_OFFSET_X, TAG_SENSOR_OFFSET_Y, TAG_SENSOR_OFFSET_Z};
    const void *datas[] = {&offsets[0], &offsets[1], &offsets[2]};
    uint16_t lengths[] = {sizeof(float), sizeof(float), sizeof(float)};
    
    tlv_write_batch(tags, 3, datas, lengths);
 
    // 获取统计
    tlv_statistics_t stats;
    tlv_get_statistics(&stats);
    
    // 不需要显式反初始化（静态分配）
}
