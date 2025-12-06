/**
 * @file tlv_port.c
 * @brief TLV FRAM存储系统硬件移植层实现（使用已有SPI接口）
 */
 
#include "tlv_port.h"
//#include "bsp_fram.h"  // 你的SPI驱动头文件
#include "Main.h"
 
/* ============================ 外部SPI接口（假设你已经实现） ============================ */
 

 
/* ============================ FRAM硬件接口实现（适配已有接口） ============================ */
 
int tlv_port_fram_init(void)
{
    // 如果你的SPI已经初始化过,这里可以为空
    // 或者调用你的初始化函数
//    FRAM_Init();
    
    return TLV_OK;
}
 
int tlv_port_fram_read(uint32_t addr, void *data, uint32_t size)
{
    if (!data || size == 0) {
        return TLV_ERROR_INVALID_PARAM;
    }
	
	int ret = Fram_Read(addr, size, (uint8_t *)data);
	
    return (ret == 0) ? TLV_OK : TLV_ERROR;
}
 
int tlv_port_fram_write(uint32_t addr, const void *data, uint32_t size)
{
    if (!data || size == 0) {
        return TLV_ERROR_INVALID_PARAM;
    }
	
	int ret = Fram_Write(addr, size, (uint8_t *)data);
    
    return (ret == 0) ? TLV_OK : TLV_ERROR;
}
 
/* ============================ 时间接口实现 ============================ */
 
uint32_t tlv_port_get_timestamp_s(void)
{
    // 使用你的系统时间函数
    return HAL_GetTime();
}
 
uint32_t tlv_port_get_timestamp_ms(void)
{
    return HAL_GetTick();
}
