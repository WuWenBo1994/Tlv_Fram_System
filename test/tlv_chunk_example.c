#include "tlv_file_system.h"


void example_usage(void)
{
  	// 初始化
    tlv_init_result_t init_result = tlv_init();
	if (init_result == TLV_INIT_OK)
	{
		;
	}
    else if (init_result == TLV_INIT_FIRST_BOOT || init_result == TLV_INIT_ERROR) 
	{
        int ret = tlv_format(0);
		if(ret == TLV_OK){
			init_result = tlv_init();
			if(init_result != TLV_INIT_OK)
			{
				return;
			}
		}
		else
		{
			return;
		}
    }
	else if(init_result == TLV_INIT_RECOVERED){
		init_result = tlv_init();
		if(init_result != TLV_INIT_OK)
		{
			return;
		}
	}
	
	int ret;
	uint16_t len,total_len;
	tlv_stream_handle_t h;
	char buf[20];
	
	total_len = 128;
	
	for(int i=0; i<sizeof(buf); i++)
	{
		buf[i] = i;
	}
	
	h = tlv_write_begin(TAG_SYSTEM_CONFIG, total_len);
	if( h != TLV_STREAM_INVALID_HANDLE)
	{
		int remain = total_len;
		int cell_size = sizeof(buf);
		
		while(remain > 0)
		{
			len = remain > cell_size ? cell_size : remain;
			ret = tlv_write_chunk(h, buf, len);
			remain -= cell_size;
		}

		tlv_write_end(h);
	}
	
	total_len = 128;
	for(int i=0; i<sizeof(buf); i++)
	{
		buf[i] = 0;
	}
	h = tlv_read_begin(TAG_SYSTEM_CONFIG, &total_len);
	if( h != TLV_STREAM_INVALID_HANDLE)
	{
		int remain = total_len;
		int cell_size = sizeof(buf);
		
		while(remain > 0)
		{
			len = cell_size;
			ret = tlv_read_chunk(h, buf, &len);
			remain -= cell_size;
		}

		tlv_read_end(h);
	}
	
}


