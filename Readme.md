## 1. 状态机详解

### 状态定义和含义

```c++
typedef enum {  
    TLV_STATE_UNINITIALIZED = 0,  // 未初始化/需要格式化  
    TLV_STATE_INITIALIZED,         // 正常工作状态  
    TLV_STATE_ERROR,               // 发生错误，不可用  
    TLV_STATE_FORMATTED            // 刚格式化完成
} tlv_state_t;
```

### 状态转换图

```
系统启动
    ↓
┌─────────────────────────────────────────┐
│ tlv_init()                              │
│  └→ 读取FRAM Header                     │
└─────────────────────────────────────────┘
    ↓
    ├─ Header有效? ─YES→ 索引表有效? ─YES→ [INITIALIZED] → 正常使用
    │                        │
    │                        NO (索引损坏)
    │                        ↓
    │                    尝试备份恢复
    │                        ├─ 成功 → [INITIALIZED]
    │                        └─ 失败 → [ERROR]
    │
    NO (Header无效/首次启动)
    ↓
[UNINITIALIZED] ─┐
                 │
        需要调用 tlv_format()
                 ↓
            [FORMATTED] → 可以正常使用 → [INITIALIZED]


[ERROR] → 需要手动修复或重新格式化
```

## 2. 状态详细说明

### TLV_STATE_UNINITIALIZED（未初始化）

**含义**：

- FRAM中没有有效的TLV系统数据
- 首次使用，或数据完全损坏

**出现时机**：

```c
tlv_init_result_t result = tlv_init();
if (result == TLV_INIT_FIRST_BOOT) {  
// 状态为 TLV_STATE_UNINITIALIZED
}
```

**特征**：

- Header魔数不匹配 (`magic != TLV_SYSTEM_MAGIC`)
- Header CRC16校验失败
- FRAM是全新的/被擦除过

**必须操作**：调用 `tlv_format()` 初始化

### TLV_STATE_INITIALIZED（已初始化/正常工作）

**含义**：

- 系统正常工作状态
- Header和索引表都有效
- 可以进行所有读写操作

**出现时机**：

```c
tlv_init_result_t result = tlv_init();
if (result == TLV_INIT_OK) {  
    // 状态为 TLV_STATE_INITIALIZED
}
```

**允许的操作**：

- ✅ `tlv_write()` / `tlv_read()` / `tlv_delete()`
- ✅ `tlv_defragment()` / `tlv_verify_all()`
- ✅ `tlv_backup_all()` / `tlv_restore_from_backup()`

---

### TLV_STATE_ERROR（错误状态）

**含义**：

- 系统遇到严重错误，无法工作
- 可能是硬件故障或数据严重损坏

**出现时机**：

```C
// 初始化失败
if (tlv_init() == TLV_INIT_ERROR) {  
	// 状态为 TLV_STATE_ERROR
} 

// 或运行时检测到严重错误
if (g_tlv_ctx.state == TLV_STATE_ERROR) {  
    // 所有操作都会被拒绝
}
```

**原因**：

- FRAM硬件初始化失败
- 内存分配失败（静态内存异常）
- 备份恢复也失败
- 索引表和备份都损坏

**恢复方法**：

```C
// 方法1：尝试从备份恢复
int ret = tlv_restore_from_backup();
if (ret == TLV_OK) {
    g_tlv_ctx.state = TLV_STATE_INITIALIZED;
} 
// 方法2：强制重新格式化（会丢失所有数据）
tlv_format(0);
```

---

### TLV_STATE_FORMATTED（刚格式化完成）

**含义**：

- 刚执行完 `tlv_format()`
- 系统已格式化，可以使用
- 是一个过渡状态，通常马上变为 `INITIALIZED`

**出现时机**：

```C
tlv_format(0);
// 此时状态为 TLV_STATE_FORMATTED 

// 第一次写入后自动变为 INITIALIZED
tlv_write(TAG_SYSTEM_CONFIG, data, len);
// 现在状态为 TLV_STATE_INITIALIZED
```

**特征**：

- Header已初始化，魔数正确
- 索引表为空（tag_count = 0）
- 数据区为空

## 3. tlv_format() 使用详解

### 函数原型

```C
int tlv_format(uint32_t magic);
```

### 功能

完全初始化FRAM，创建全新的TLV系统结构

### 使用场景

#### 场景1：首次启动（必须）

```C
void first_boot_example(void)
{
    tlv_init_result_t result = tlv_init();
  
    if (result == TLV_INIT_FIRST_BOOT) {
        printf("First boot detected, formatting...\n");
      
        // 使用默认魔数（TLV_SYSTEM_MAGIC）
        int ret = tlv_format(0);
      
        if (ret == TLV_OK) {
            printf("Format successful!\n");
            // 现在可以写入数据
            tlv_write(TAG_SYSTEM_CONFIG, &config, sizeof(config));
        } else {
            printf("Format failed: %d\n", ret);
        }
    }
}
```

#### 场景2：数据损坏无法恢复（强制）

```C
void corruption_recovery_example(void)
{
    tlv_init_result_t result = tlv_init();
  
    if (result == TLV_INIT_ERROR) {
        printf("System corrupted!\n");
      
        // 尝试恢复
        if (tlv_restore_from_backup() != TLV_OK) {
            printf("Backup restore failed, formatting...\n");
          
            // 最后手段：格式化（会丢失所有数据）
            tlv_format(0);
        }
    }
}
```

#### 场景3：用户主动清除所有数据

```C
void factory_reset_example(void)
{
    printf("Factory reset requested\n");
    printf("Are you sure? (y/n): ");
  
    char input = getchar();
    if (input == 'y') {
        // 备份重要数据（可选）
        uint8_t serial[32];
        uint16_t len = sizeof(serial);
        tlv_read(TAG_SYSTEM_SERIAL_NUMBER, serial, &len);
      
        // 格式化
        tlv_format(0);
      
        // 恢复重要数据
        tlv_write(TAG_SYSTEM_SERIAL_NUMBER, serial, len);
      
        printf("Factory reset complete\n");
    }
}
```

#### 场景4：使用自定义魔数

```C
void custom_magic_example(void)
{
    // 为不同产品使用不同魔数，防止误识别
    #define PRODUCT_A_MAGIC  0x50524F41  // "PROA"
    #define PRODUCT_B_MAGIC  0x50524F42  // "PROB"
  
    tlv_format(PRODUCT_A_MAGIC);
  
    // 以后初始化时会检查魔数
    tlv_init();
}
```

### 执行流程

```C
int tlv_format(uint32_t magic)
{
    // ========== 步骤1：初始化系统Header ==========
    memset(g_tlv_ctx.header, 0, sizeof(tlv_system_header_t));
  
    g_tlv_ctx.header->magic = (magic != 0) ? magic : TLV_SYSTEM_MAGIC;
    g_tlv_ctx.header->version = TLV_SYSTEM_VERSION;  // v1.0
    g_tlv_ctx.header->tag_count = 0;
    g_tlv_ctx.header->data_region_start = TLV_DATA_ADDR;
    g_tlv_ctx.header->data_region_size = TLV_BACKUP_ADDR - TLV_DATA_ADDR;
    g_tlv_ctx.header->next_free_addr = TLV_DATA_ADDR;
    g_tlv_ctx.header->free_space = g_tlv_ctx.header->data_region_size;
    g_tlv_ctx.header->used_space = 0;
  
    // 计算Header CRC16
    g_tlv_ctx.header->header_crc16 = tlv_crc16(...);
  
    // ========== 步骤2：初始化索引表（全部清零） ==========
    tlv_index_init(&g_tlv_ctx);  // 分配静态内存并清零
  
    // ========== 步骤3：写入Header到FRAM ==========
    fram_write(TLV_HEADER_ADDR, g_tlv_ctx.header, sizeof(tlv_system_header_t));
  
    // ========== 步骤4：写入空索引表到FRAM ==========
    tlv_index_save(&g_tlv_ctx);
  
    // ========== 步骤5：备份管理区 ==========
    tlv_backup_all();
  
    // ========== 步骤6：更新状态 ==========
    g_tlv_ctx.state = TLV_STATE_FORMATTED;
  
    return TLV_OK;
}
```

### FRAM内存布局（格式化后）

```
┌─────────────────────────────────────────────────────────────┐
│ 0x0000: System Header (256B)                                │
│         - magic: 0x544C5646                                 │
│         - version: 0x0100                                   │
│         - tag_count: 0                                      │
│         - next_free_addr: 0x0800                            │
│         - free_space: 122880 (120KB)                        │
│         - header_crc16: 0xXXXX                              │
├─────────────────────────────────────────────────────────────┤
│ 0x0100: Index Table (1538B)                                 │
│         - entries[256]: 全部为0                             │
│         - index_crc16: 0xXXXX                               │
├─────────────────────────────────────────────────────────────┤
│ 0x0800: Data Region (120KB)                                 │
│         - 全部空闲，可以分配                                 │
│         - ...                                               │
├─────────────────────────────────────────────────────────────┤
│ 0x1E000: Backup Region (2KB)                                │
│         - Header的备份 (256B)                               │
│         - Index Table的备份 (1538B)                         │
└─────────────────────────────────────────────────────────────┘
```

## 4. 备份区域使用详解

### 备份区域布局

```C
// 地址定义
#define TLV_HEADER_ADDR    0x0000     // Header起始
#define TLV_INDEX_ADDR     0x0100     // 索引表起始
#define TLV_DATA_ADDR      0x0800     // 数据区起始
#define TLV_BACKUP_ADDR    0x1E000    // 备份区起始
 
// 备份区域包含
// [TLV_BACKUP_ADDR + 0]       : Header的备份 (256B)
// [TLV_BACKUP_ADDR + 256]     : Index Table的备份 (1538B)
// [TLV_BACKUP_ADDR + 1794]    : 保留扩展
```

### 完整的备份恢复流程

#### tlv_backup_all() - 创建备份

```C
int tlv_backup_all(void)
{
    // ========== 计算需要备份的大小 ==========
    // 从Header开始，到Index Table结束
    uint32_t backup_size = TLV_BACKUP_ADDR - TLV_HEADER_ADDR;
    // = 0x1E000 - 0x0000 = 122880 字节
  
    // ========== 分批读取管理区 ==========
    uint32_t offset = 0;
    while (offset < backup_size) {
        uint32_t chunk_size = (backup_size - offset > TLV_BUFFER_SIZE) ?
                              TLV_BUFFER_SIZE : (backup_size - offset);
      
        // 读取管理区（Header + Index）
        tlv_port_fram_read(TLV_HEADER_ADDR + offset, 
                          g_tlv_ctx.static_buffer, 
                          chunk_size);
      
        // 写入备份区
        tlv_port_fram_write(TLV_BACKUP_ADDR + offset, 
                           g_tlv_ctx.static_buffer, 
                           chunk_size);
      
        offset += chunk_size;
    }
  
    // ========== 更新备份时间 ==========
    g_tlv_ctx.header->last_update_time = tlv_port_get_timestamp_s();
    system_header_save();
  
    return TLV_OK;
}
```

**实际执行**（假设TLV_BUFFER_SIZE=512）：

```
第1批：读 0x0000~0x01FF → 写 0x1E000~0x1E1FF (Header + 部分Index)
第2批：读 0x0200~0x03FF → 写 0x1E200~0x1E3FF (Index剩余部分)
第3批：读 0x0400~0x05FF → 写 0x1E400~0x1E5FF (Index结束 + 数据区开始)
...继续，直到复制完 TLV_BACKUP_ADDR - TLV_HEADER_ADDR 字节
```

#### tlv_restore_from_backup() - 从备份恢复

```C
int tlv_restore_from_backup(void)
{
    uint32_t backup_size = TLV_BACKUP_ADDR - TLV_HEADER_ADDR;
  
    // ========== 步骤1：验证备份的有效性 ==========
    // 先读取备份的Header
    tlv_system_header_t backup_header;
    tlv_port_fram_read(TLV_BACKUP_ADDR, &backup_header, sizeof(backup_header));
  
    // 检查魔数
    if (backup_header.magic != TLV_SYSTEM_MAGIC) {
        return TLV_ERROR_CORRUPTED;  // 备份也损坏了
    }
  
    // 检查CRC16
    uint16_t calc_crc = tlv_crc16(&backup_header, 
                                   sizeof(backup_header) - sizeof(uint16_t));
    if (calc_crc != backup_header.header_crc16) {
        return TLV_ERROR_CRC_FAILED;  // 备份CRC失败
    }
  
    // ========== 步骤2：分批恢复到管理区 ==========
    uint32_t offset = 0;
    while (offset < backup_size) {
        uint32_t chunk_size = (backup_size - offset > TLV_BUFFER_SIZE) ?
                              TLV_BUFFER_SIZE : (backup_size - offset);
      
        // 从备份区读取
        tlv_port_fram_read(TLV_BACKUP_ADDR + offset, 
                          g_tlv_ctx.static_buffer, 
                          chunk_size);
      
        // 写入到管理区
        tlv_port_fram_write(TLV_HEADER_ADDR + offset, 
                           g_tlv_ctx.static_buffer, 
                           chunk_size);
      
        offset += chunk_size;
    }
  
    // ========== 步骤3：重新加载 ==========
    system_header_load();
    tlv_index_load(&g_tlv_ctx);
  
    return TLV_OK;
}
```

### 备份使用场景

#### 场景1：定期自动备份

```C
void periodic_backup_task(void)
{
    static uint32_t last_backup_time = 0;
    uint32_t current_time = tlv_port_get_timestamp_s();
  
    // 每小时备份一次
    if (current_time - last_backup_time >= 3600) {
        tlv_backup_all();
        last_backup_time = current_time;
    }
}
```

#### 场景2：关键操作前备份

```C
void critical_update_with_backup(void)
{
    // 更新前备份
    tlv_backup_all();
  
    // 执行关键更新
    int ret = tlv_write(TAG_SYSTEM_CONFIG, &new_config, sizeof(new_config));
  
    if (ret < 0) {
        // 失败了，恢复备份
        tlv_restore_from_backup();
    }
}
```

#### 场景3：启动时检测损坏

```C
void boot_sequence(void)
{
    tlv_init_result_t result = tlv_init();
  
    switch (result) {
        case TLV_INIT_OK:
            // 正常
            break;
          
        case TLV_INIT_RECOVERED:
            // 已从备份恢复
            printf("WARNING: Restored from backup\n");
            break;
          
        case TLV_INIT_FIRST_BOOT:
            // 首次启动
            tlv_format(0);
            break;
          
        case TLV_INIT_ERROR:
            // 备份也失败了
            printf("FATAL: Cannot recover, need format\n");
            break;
    }
}
```

#### 场景4：固件升级前后

```C
void firmware_upgrade_example(void)
{
    // 升级前备份
    printf("Backing up before upgrade...\n");
    tlv_backup_all();
  
    // 执行固件升级
    firmware_upgrade();
  
    // 升级后验证
    uint32_t corrupted = 0;
    if (tlv_verify_all(&corrupted) != TLV_OK || corrupted > 0) {
        printf("Data corrupted after upgrade, restoring...\n");
        tlv_restore_from_backup();
    }
}
```

### 备份策略建议

| 策略                 | 触发时机       | 优点               | 缺点                  |
| -------------------- | -------------- | ------------------ | --------------------- |
| **定期备份**   | 每小时/每天    | 自动化，不丢失数据 | 备份时有写FRAM开销    |
| **写入前备份** | 每次关键写入前 | 最安全             | 性能影响大            |
| **计数备份**   | 每100次写入后  | 折中方案           | 可能丢失最近100次写入 |
| **手动备份**   | 用户/应用触发  | 灵活               | 依赖应用逻辑          |

### 推荐配置

```C
void recommended_backup_strategy(void)
{
    // 策略1：格式化后立即备份
    if (tlv_get_state() == TLV_STATE_FORMATTED) {
        tlv_backup_all();
    }
  
    // 策略2：每100次写入后自动备份
    static uint32_t last_write_count = 0;
    if (g_tlv_ctx.header->total_writes - last_write_count >= 100) {
        tlv_backup_all();
        last_write_count = g_tlv_ctx.header->total_writes;
    }
  
    // 策略3：关键Tag写入前备份
    void write_critical_tag(uint16_t tag, const void *data, uint16_t len)
    {
        const tlv_meta_const_t *meta = get_meta(tag);
        if (meta && meta->backup_enable) {
            tlv_backup_all();  // 先备份
        }
        tlv_write(tag, data, len);
    }
}
```

## 5. 完整使用示例

```C
void complete_lifecycle_example(void)
{
    // ========== 启动阶段 ==========
    printf("=== System Boot ===\n");
    tlv_init_result_t result = tlv_init();
  
    switch (result) {
        case TLV_INIT_FIRST_BOOT:
            printf("First boot, formatting...\n");
            tlv_format(0);
            // 状态：FORMATTED → INITIALIZED
            break;
          
        case TLV_INIT_OK:
            printf("System OK\n");
            // 状态：INITIALIZED
            break;
          
        case TLV_INIT_RECOVERED:
            printf("Recovered from backup\n");
            // 状态：INITIALIZED
            break;
          
        case TLV_INIT_ERROR:
            printf("FATAL ERROR\n");
            // 状态：ERROR
            return;
    }
  
    // ========== 正常使用阶段 ==========
    printf("\n=== Normal Operation ===\n");
  
    uint32_t config = 0x12345678;
    tlv_write(TAG_SYSTEM_CONFIG, &config, sizeof(config));
  
    // 定期备份
    tlv_backup_all();
  
    // ========== 模拟错误 ==========
    printf("\n=== Simulating Corruption ===\n");
  
    // 人为破坏Header（测试用）
    uint8_t garbage[256] = {0};
    tlv_port_fram_write(TLV_HEADER_ADDR, garbage, 256);
  
    // 重新初始化
    printf("\n=== Reinitialize ===\n");
    result = tlv_init();
  
    if (result == TLV_INIT_ERROR) {
        printf("Header corrupted, trying backup...\n");
        if (tlv_restore_from_backup() == TLV_OK) {
            printf("Restore successful!\n");
            // 验证数据
            uint32_t read_config;
            uint16_t len = sizeof(read_config);
            tlv_read(TAG_SYSTEM_CONFIG, &read_config, &len);
            printf("Config recovered: 0x%08lX\n", (unsigned long)read_config);
        }
    }
  
    // ========== 工厂复位 ==========
    printf("\n=== Factory Reset ===\n");
    tlv_format(0);
    printf("All data erased\n");
}
```

## 6. 状态检查和错误处理

```C
// 通用操作前检查
int safe_operation(void)
{
    switch (g_tlv_ctx.state) {
        case TLV_STATE_INITIALIZED:
        case TLV_STATE_FORMATTED:
            // 可以操作
            return TLV_OK;
          
        case TLV_STATE_UNINITIALIZED:
            printf("ERROR: System not initialized, call tlv_format() first\n");
            return TLV_ERROR;
          
        case TLV_STATE_ERROR:
            printf("ERROR: System in error state, try restore or format\n");
            return TLV_ERROR;
          
        default:
            return TLV_ERROR;
    }
}
```

---

## 总结

### 状态总结

| 状态                    | 含义     | 可操作 | 如何进入                     | 如何退出              |
| ----------------------- | -------- | ------ | ---------------------------- | --------------------- |
| **UNINITIALIZED** | 未初始化 | ❌     | `tlv_init()`返回FIRST_BOOT | 调用 `tlv_format()` |
| **INITIALIZED**   | 正常工作 | ✅     | 成功加载或恢复               | 发生错误              |
| **ERROR**         | 错误状态 | ❌     | 初始化/恢复失败              | 修复或格式化          |
| **FORMATTED**     | 刚格式化 | ✅     | `tlv_format()`完成         | 第一次写入            |

### 关键操作总结

- **tlv_format()**：清空所有数据，重建TLV系统
- **tlv_backup_all()**：备份Header+Index到备份区
- **tlv_restore_from_backup()**：从备份区恢复
- **建议**：关键操作前备份，定期自动备份
