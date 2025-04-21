/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/******************************************************************************
 * See https://www.freertos.org/freertos-on-qemu-mps2-an385-model.html for
 * instructions.
 *
 * This project provides two demo applications.  A simple blinky style project,
 * and a more comprehensive test and demo application.  The
 * mainCREATE_SIMPLE_BLINKY_DEMO_ONLY constant, defined in this file, is used to
 * select between the two.  The simply blinky demo is implemented and described
 * in main_blinky.c.  The more comprehensive test and demo application is
 * implemented and described in main_full.c.
 *
 * This file implements the code that is not demo specific, including the
 * hardware setup and FreeRTOS hook functions.
 *
 * Running in QEMU:
 * Use the following commands to start the application running in a way that
 * enables the debugger to connect, omit the "-s -S" to run the project without
 * the debugger:
 * qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -kernel
 * [path-to]/RTOSDemo.out -monitor none -nographic -serial stdio -s -S
 */

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Standard includes. */
#include <stdio.h>
#include <string.h>

/* TODO: Steps for adding TraceRecorder are tagged with comments like this. */
/* TODO: This way, Eclipse IDEs can provide a summary in the Tasks window. */
/* TODO: To open Tasks, select Window -> Show View -> Tasks (or Other) */

/* TODO TraceRecorder (Step 1): Include trcRecorder.h to access the API. */
#include <trcRecorder.h>

/* This project provides two demo applications.  A simple blinky style demo
 * application, and a more comprehensive test and demo application.  The
 * mainCREATE_SIMPLE_BLINKY_DEMO_ONLY setting is used to select between the two.
 *
 * If mainCREATE_SIMPLE_BLINKY_DEMO_ONLY is 1 then the blinky demo will be
 * built. The blinky demo is implemented and described in main_blinky.c.
 *
 * If mainCREATE_SIMPLE_BLINKY_DEMO_ONLY is not 1 then the comprehensive test
 * and demo application will be built.  The comprehensive test and demo
 * application is implemented and described in main_full.c. */
#define mainCREATE_SIMPLE_BLINKY_DEMO_ONLY 1

/* printf() output uses the UART.  These constants define the addresses of the
 * required UART registers. */
#define UART0_ADDRESS (0x40004000UL)
#define UART0_DATA (*(((volatile uint32_t *)(UART0_ADDRESS + 0UL))))
#define UART0_STATE (*(((volatile uint32_t *)(UART0_ADDRESS + 4UL))))
#define UART0_CTRL (*(((volatile uint32_t *)(UART0_ADDRESS + 8UL))))
#define UART0_BAUDDIV (*(((volatile uint32_t *)(UART0_ADDRESS + 16UL))))
#define TX_BUFFER_MASK (1UL)

/*
 * main_blinky() is used when mainCREATE_SIMPLE_BLINKY_DEMO_ONLY is set to 1.
 * main_full() is used when mainCREATE_SIMPLE_BLINKY_DEMO_ONLY is set to 0.
 */
extern void main_blinky(void);
extern void main_full(void);

/*
 * Only the comprehensive demo uses application hook (callback) functions.  See
 * https://www.FreeRTOS.org/a00016.html for more information.
 */
void vFullDemoTickHookFunction(void);
void vFullDemoIdleFunction(void);

/*
 * Printf() output is sent to the serial port.  Initialise the serial hardware.
 */
static void prvUARTInit(void);

/*-----------------------------------------------------------*/
#include "semphr.h"
typedef struct {
  uint8_t *buffer;                // 缓冲区指针
  size_t head;                    // 写入位置索引（需原子操作）
  size_t tail;                    // 读取位置索引（需原子操作）
  size_t capacity;                // 缓冲区总容量（必须为2的幂）
  SemaphoreHandle_t mutex;        // FreeRTOS互斥锁
  StaticSemaphore_t mutex_buffer; // 静态分配互斥锁内存
} RingBuffer_t;
// 计算掩码（利用capacity为2的幂的特性）
#define RINGBUF_MASK(ring) (ring->capacity - 1)

// 初始化环形缓冲区（静态内存分配）
RingBuffer_t *ringbuf_init(size_t size) {
  // 确保size是2的幂（便于位操作优化）
  configASSERT((size & (size - 1)) == 0);

  RingBuffer_t *ring = pvPortMalloc(sizeof(RingBuffer_t));
  ring->buffer = pvPortMalloc(size);
  ring->head = ring->tail = 0;
  ring->capacity = size;

  // 创建静态互斥锁（解决优先级反转）
  ring->mutex = xSemaphoreCreateMutexStatic(&ring->mutex_buffer);
  configASSERT(ring->mutex != NULL);

  return ring;
}

// 销毁环形缓冲区
void ringbuf_free(RingBuffer_t *ring) {
  vSemaphoreDelete(ring->mutex);
  vPortFree(ring->buffer);
  vPortFree(ring);
}
#include "stdbool.h"
bool ringbuf_write(RingBuffer_t *ring, const uint8_t *data, size_t len) {
  if (xSemaphoreTake(ring->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false; // 获取锁超时
  }

  // 计算可用空间（无符号数减法自动取模）
  size_t available = ring->capacity - (ring->head - ring->tail);
  if (available < len) {
    xSemaphoreGive(ring->mutex);
    return false;
  }

  // 分两段写入（绕过缓冲区末尾）
  size_t first_part = ring->capacity - (ring->head & RINGBUF_MASK(ring));
  first_part = (first_part > len) ? len : first_part;
  memcpy(ring->buffer + (ring->head & RINGBUF_MASK(ring)), data, first_part);

  if (len > first_part) {
    memcpy(ring->buffer, data + first_part, len - first_part);
  }

  // 使用原子操作更新head（避免编译器重排序）
  size_t new_head = ring->head + len;
  __atomic_store_n(&ring->head, new_head, __ATOMIC_RELEASE);

  xSemaphoreGive(ring->mutex);
  return true;
}

size_t ringbuf_read(RingBuffer_t *ring, uint8_t *data, size_t len) {
  if (xSemaphoreTake(ring->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return 0; // 获取锁超时
  }

  size_t available = ring->head - ring->tail;
  if (available == 0) {
    xSemaphoreGive(ring->mutex);
    return 0;
  }

  len = (len > available) ? available : len;

  // 分两段读取
  size_t first_part = ring->capacity - (ring->tail & RINGBUF_MASK(ring));
  first_part = (first_part > len) ? len : first_part;
  memcpy(data, ring->buffer + (ring->tail & RINGBUF_MASK(ring)), first_part);

  if (len > first_part) {
    memcpy(data + first_part, ring->buffer, len - first_part);
  }

  // 原子操作更新tail
  size_t new_tail = ring->tail + len;
  __atomic_store_n(&ring->tail, new_tail, __ATOMIC_RELEASE);

  xSemaphoreGive(ring->mutex);
  return len;
}
// 获取可读数据量（无需加锁，原子读取）
size_t ringbuf_available(RingBuffer_t *ring) {
  size_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
  size_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
  return head - tail;
}

// 获取剩余空间（无需加锁）
size_t ringbuf_remaining(RingBuffer_t *ring) {
  return ring->capacity - ringbuf_available(ring);
}
// 写入任务
void vProducerTask(void *pv) {
  RingBuffer_t *ring = (RingBuffer_t *)pv;
  uint8_t data[64];
  static int bias = 0;
  while (1) {
    for (int i = 0; i < 64; i++) {
      data[i] = i + bias;
    }
    if (!ringbuf_write(ring, data, sizeof(data))) {
      printf("ringbuf w: %d bytes\n", 0);
      vTaskDelay(1); // 缓冲区满时等待
    }else{
        bias++;
        printf("ringbuf w: %d bytes\n", 64);
    }
  }
}

// 读取任务
void vConsumerTask(void *pv) {
  RingBuffer_t *ring = (RingBuffer_t *)pv;
  uint8_t data[64];
  while (1) {
    size_t read = ringbuf_read(ring, data, sizeof(data));
    if (read == 0) {
      printf("ringbuf r: %d bytes\n", 0);
      vTaskDelay(1); // 缓冲区空时等待
    }else {
        printf("ringbuf r: %d bytes, %d, %d, %d, %d\n", read, data[0], data[1], data[2],data[3]);
    }
  }
}

// 监控任务（检测竞争）
void vMonitorTask(void *pv) {
  RingBuffer_t *ring = (RingBuffer_t *)pv;
  while (1) {
    size_t avail = ringbuf_available(ring);
    configASSERT(avail <= ring->capacity); // 断言缓冲区无溢出
    vTaskDelay(1000);
  }
}

// 启动测试
void test_ringbuffer() {
  RingBuffer_t *ring = ringbuf_init(1024); // 1KB缓冲区
  xTaskCreate(vProducerTask, "Prod", 128, ring, 2, NULL);
  xTaskCreate(vConsumerTask, "Cons", 128, ring, 2, NULL);
  xTaskCreate(vMonitorTask, "Mon", 128, ring, 3, NULL);
}
void vAlwaysTask(void *pv) {
  for (;;) {
    printf("TickCount: %d\n", xTaskGetTickCount());
    vTaskDelay(1000);
  }
}

void vFaultyTask(void *pv) {
  (void *)(pv);
  // uint8_t buffer[512];  // 栈空间过大（FreeRTOS默认任务栈可能仅128字节）
  // while(1) {
  //     memset(buffer, 0, sizeof(buffer)); // 触发溢出
  //     vTaskDelay(100);
  // }
  // int a = 0xbebebebe;
  // int *pa = (int *)a;
  // int c = *pa;  // 触发HardFault
  // printf("c: %d\n", c);
  vTaskDelete(NULL);
}

// 任务创建时分配不足栈空间

void main(void) {
  /* See https://www.freertos.org/freertos-on-qemu-mps2-an385-model.html for
   * instructions. */

  /* Initializing TraceRecorder. Using #if (configUSE_TRACE_FACILITY == 1)
   * is normally not needed. TraceRecorder API calls are normally ignored
   * and produce no code when configUSE_TRACE_FACILITY is 0, assuming
   * trcRecorder.h is included. However, this was missing for
   * xTraceTimestampSetPeriod() in TraceRecorder v4.10.2. */
#if (configUSE_TRACE_FACILITY == 1)

  /* TODO TraceRecorder (Step 2): Call xTraceInitialize early in main().
   * This should be called before any FreeRTOS calls are made. */
  xTraceInitialize();

  /* TODO TraceRecorder (Step 3): Call xTraceEnable to start tracing. */
  xTraceEnable(TRC_START);

  /* Extra step needed for using TraceRecorder on QEMU. */
  xTraceTimestampSetPeriod(configCPU_CLOCK_HZ / configTICK_RATE_HZ);

#endif

  /* Hardware initialisation.  printf() output uses the UART for IO. */
  prvUARTInit();
  printf("enter main\n");

/* The mainCREATE_SIMPLE_BLINKY_DEMO_ONLY setting is described at the top
 * of this file. */
#if (mainCREATE_SIMPLE_BLINKY_DEMO_ONLY == 1)
  {
    test_ringbuffer();
    xTaskCreate(vAlwaysTask, "Always", 64, NULL, 1, NULL);

    //xTaskCreate(vFaultyTask, "Faulty", 64, NULL, 1, NULL);

    main_blinky();
  }
#else
  {
    main_full();
  }
#endif
}
/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook(void) {
  /* vApplicationMallocFailedHook() will only be called if
   * configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
   * function that will get called if a call to pvPortMalloc() fails.
   * pvPortMalloc() is called internally by the kernel whenever a task, queue,
   * timer or semaphore is created using the dynamic allocation (as opposed to
   * static allocation) option.  It is also called by various parts of the
   * demo application.  If heap_1.c, heap_2.c or heap_4.c is being used, then
   * the size of the	heap available to pvPortMalloc() is defined by
   * configTOTAL_HEAP_SIZE in FreeRTOSConfig.h, and the xPortGetFreeHeapSize()
   * API function can be used to query the size of free heap space that remains
   * (although it does not provide information on how the remaining heap might
   * be fragmented).  See http://www.freertos.org/a00111.html for more
   * information. */
  printf("\r\n\r\nMalloc failed\r\n");
  portDISABLE_INTERRUPTS();

  for (;;) {
  }
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook(void) {
  /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   * to 1 in FreeRTOSConfig.h.  It will be called on each iteration of the idle
   * task.  It is essential that code added to this hook function never attempts
   * to block in any way (for example, call xQueueReceive() with a block time
   * specified, or call vTaskDelay()).  If application tasks make use of the
   * vTaskDelete() API function to delete themselves then it is also important
   * that vApplicationIdleHook() is permitted to return to its calling function,
   * because it is the responsibility of the idle task to clean up memory
   * allocated by the kernel to any task that has since deleted itself. */
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
  (void)pcTaskName;
  (void)pxTask;

  /* Run time stack overflow checking is performed if
   * configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
   * function is called if a stack overflow is detected. */
  printf("\r\n\r\nStack overflow in %s\r\n", pcTaskName);

  printf("[FAULT] Stack overflow in %s! Killing task.\n", pcTaskName);
  vTaskDelete(pxTask); // 终止故障任务
  portDISABLE_INTERRUPTS();

  for (;;) {
  }
}
/*-----------------------------------------------------------*/

void vApplicationTickHook(void) {
  /* This function will be called by each tick interrupt if
   * configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h.  User code can be
   * added here, but the tick hook is called from an interrupt context, so
   * code must not attempt to block, and only the interrupt safe FreeRTOS API
   * functions can be used (those that end in FromISR()). */

#if (mainCREATE_SIMPLE_BLINKY_DEMO_ONLY != 1)
  {
    extern void vFullDemoTickHookFunction(void);

    vFullDemoTickHookFunction();
  }
#endif /* mainCREATE_SIMPLE_BLINKY_DEMO_ONLY */
}
/*-----------------------------------------------------------*/

void vApplicationDaemonTaskStartupHook(void) {
  /* This function will be called once only, when the daemon task starts to
   * execute (sometimes called the timer task).  This is useful if the
   * application includes initialisation code that would benefit from executing
   * after the scheduler has been started. */

  xTraceEnable(TRC_START);
}
/*-----------------------------------------------------------*/

void vAssertCalled(const char *pcFileName, uint32_t ulLine) {
  volatile uint32_t ulSetToNonZeroInDebuggerToContinue = 0;

  /* Called if an assertion passed to configASSERT() fails.  See
   * http://www.freertos.org/a00110.html#configASSERT for more information. */

  printf("ASSERT! Line %d, file %s\r\n", (int)ulLine, pcFileName);

  taskENTER_CRITICAL();
  {
    /* You can step out of this function to debug the assertion by using
     * the debugger to set ulSetToNonZeroInDebuggerToContinue to a non-zero
     * value. */
    while (ulSetToNonZeroInDebuggerToContinue == 0) {
      __asm volatile("NOP");
      __asm volatile("NOP");
    }
  }
  taskEXIT_CRITICAL();
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetIdleTaskMemory() to provide the memory that
 * is used by the Idle task. */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {
  /* If the buffers to be provided to the Idle task are declared inside this
   * function then they must be declared static - otherwise they will be
   * allocated on the stack and so not exists after this function exits. */
  static StaticTask_t xIdleTaskTCB;
  static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

  /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
   * state will be stored. */
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

  /* Pass out the array that will be used as the Idle task's stack. */
  *ppxIdleTaskStackBuffer = uxIdleTaskStack;

  /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
   * Note that, as the array is necessarily of type StackType_t,
   * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
 * application must provide an implementation of
 * vApplicationGetTimerTaskMemory() to provide the memory that is used by the
 * Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize) {
  /* If the buffers to be provided to the Timer task are declared inside this
   * function then they must be declared static - otherwise they will be
   * allocated on the stack and so not exists after this function exits. */
  static StaticTask_t xTimerTaskTCB;
  static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

  /* Pass out a pointer to the StaticTask_t structure in which the Timer
   * task's state will be stored. */
  *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

  /* Pass out the array that will be used as the Timer task's stack. */
  *ppxTimerTaskStackBuffer = uxTimerTaskStack;

  /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
   * Note that, as the array is necessarily of type StackType_t,
   * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
  *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
/*-----------------------------------------------------------*/

static void prvUARTInit(void) {
  UART0_BAUDDIV = 16;
  UART0_CTRL = 1;
}
/*-----------------------------------------------------------*/

int __write(int iFile, char *pcString, int iStringLength) {
  int iNextChar;

  /* Avoid compiler warnings about unused parameters. */
  (void)iFile;

  /* Output the formatted string to the UART. */
  for (iNextChar = 0; iNextChar < iStringLength; iNextChar++) {
    while ((UART0_STATE & TX_BUFFER_MASK) != 0) {
    }

    UART0_DATA = *pcString;
    pcString++;
  }

  return iStringLength;
}
/*-----------------------------------------------------------*/

void *malloc(size_t size) {
  (void)size;

  /* This project uses heap_4 so doesn't set up a heap for use by the C
   * library - but something is calling the C library malloc().  See
   * https://freertos.org/a00111.html for more information. */
  printf("\r\n\r\nUnexpected call to malloc() - should be usine "
         "pvPortMalloc()\r\n");
  portDISABLE_INTERRUPTS();

  for (;;) {
  }
}
