# CORTEX_MPS2_QEMU_IAR_GCC(GCC) Demo在QEMU中模拟运行FreeRTOS

Specifications

- MCU: Arm-M55/Arm-M33 可配置
- OS: FreeRTOS
- Memory:
- PCI:

## Step1 删-组织文件

下载FreeRTOS仓库，原始文件目录结构如下

```bash
$ tree -L 2
.
├── FreeRTOS
│   ├── Demo
│   ├── License
│   ├── README.md
│   ├── Source
│   ├── Test
│   └── links_to_doc_pages_for_the_demo_projects.url
├── FreeRTOS+TCP.url
├── FreeRTOS-Plus
│   ├── Demo
│   ├── README.md
│   ├── Source
│   ├── Test
│   ├── ThirdParty
│   └── VisualStudio_StaticProjects
├── GitHub-FreeRTOS-Home.url
├── History.txt
├── LICENSE.md
├── Quick_Start_Guide.url
├── README.md
├── Upgrading-to-FreeRTOS.url
├── cspell.config.yaml
├── manifest.yml
└── tools
    ├── aws_config_offline
    ├── aws_config_quick_start
    ├── cmock
    └── uncrustify.cfg

16 directories, 13 files
```

### 需要保留的核心目录

|目录| 保留原因|
| --|--|
|FreeRTOS/Source/| FreeRTOS内核源码（tasks.c/queue.c/list.c等）|
|FreeRTOS/Demo/| 需从中提取CORTEX_MPS2_QEMU_IAR_GCC演示项目|
|FreeRTOS/License/| 保留许可证文件（可选）|
|FreeRTOS-Plus/ | 整个Plus目录包含扩展组件（TCP、CLI等），基础项目不需要 (可选)|

### 需要选择性删除的内容

1. `FreeRTOS/Demo/`

```bash
# 保留目标演示项目和Common，删除其他Demo
# 获得如下目录结构
FreeRTOS/Demo/
├── CORTEX_MPS2_QEMU_IAR_GCC
└── Common
```

2. `FreeRTOS/Source/`

```bash
# 保留MemMang，GCC, 删除非GCC移植层
# 可选：删除不用的内存管理方案（保留heap_4.c）
# 获得如下目录结构
FreeRTOS/Source/portable/
├── CMakeLists.txt
├── GCC // GCC 移植层
└── MemMang // 内存管理样例程序 heap1-5 
```

3. `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC`

```bash
# 删除IAR工具链文件
FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build
└── gcc
```

# Step2 编译测试运行

```bash
Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc
$ make -j8 
$ qemu-system-arm -M mps2-an385 -cpu cortex-m3 -nographic -kernel ./output/RTOSDemo.out -semihosting
```

参数说明

1. `-M mps2-an385`指定FPGA开发板型号mps2-an385
2. `-cpu cortex-m3`指定cpu型号是cortex-m3和编译选项`-mcpu=cortex-m3`保持一致
3. `-nographic` 禁用图形界面，纯命令行模式
4. `-semihosting` 启用半主机配置，允许目标机通过调试器使用主机资源

### `-semihosting`半主机说明

半主机配置允许目标系统(被模拟的ARM芯片)通过调试器使用主机(运行QEMU的电脑)的资源，主要可以实现：

输入/输出重定向：将目标系统的printf/scanf重定向到主机终端
文件操作：在目标系统中访问主机文件系统
调试支持：实现更复杂的调试功能
系统控制：如退出模拟器等

### `-trace`跟踪功能说明

`-trace` 参数将QEMU内部事件记录到指定文件：

- events=events.txt：指定跟踪事件定义文件
- 记录内容包括：
  - CPU异常/中断
  - 内存访问
  - 设备I/O操作
  - 虚拟机状态变化
  - 自定义的跟踪点

### `-d`调试功能说明

`-d` 参数启用QEMU的调试输出，可以记录：

|选项| 记录内容|
|-|-|
|trace:free_rtos_trace| FreeRTOS特定事件(任务切换、队列操作等)|
|cpu_reset| CPU复位信息|
|int| 中断信息|
|exec| 执行的指令|
|mmu| 内存管理单元操作|
|guest_errors |客户机错误|
