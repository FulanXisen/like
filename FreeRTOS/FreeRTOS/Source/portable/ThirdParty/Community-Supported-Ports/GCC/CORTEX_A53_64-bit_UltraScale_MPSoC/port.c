/*
 * FreeRTOS Kernel V10.3.0
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Copyright (C) 2014 - 2020 Xilinx, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

/* Standard includes. */
#include <stdlib.h>

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "xpseudo_asm.h"

/* Xilinx includes. */
#include "xscugic.h"


#ifndef configINTERRUPT_CONTROLLER_BASE_ADDRESS
	#error configINTERRUPT_CONTROLLER_BASE_ADDRESS must be defined.  See http://www.freertos.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html
#endif

#ifndef configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET
	#error configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET must be defined.  See http://www.freertos.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html
#endif

#ifndef configUNIQUE_INTERRUPT_PRIORITIES
	#error configUNIQUE_INTERRUPT_PRIORITIES must be defined.  See http://www.freertos.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html
#endif

#ifndef configSETUP_TICK_INTERRUPT
	#error configSETUP_TICK_INTERRUPT() must be defined.  See http://www.freertos.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html
#endif /* configSETUP_TICK_INTERRUPT */

#ifndef configMAX_API_CALL_INTERRUPT_PRIORITY
	#error configMAX_API_CALL_INTERRUPT_PRIORITY must be defined.  See http://www.freertos.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html
#endif

#if configMAX_API_CALL_INTERRUPT_PRIORITY == 0
	#error configMAX_API_CALL_INTERRUPT_PRIORITY must not be set to 0
#endif

#if configMAX_API_CALL_INTERRUPT_PRIORITY > configUNIQUE_INTERRUPT_PRIORITIES
	#error configMAX_API_CALL_INTERRUPT_PRIORITY must be less than or equal to configUNIQUE_INTERRUPT_PRIORITIES as the lower the numeric priority value the higher the logical interrupt priority
#endif

#if configUSE_PORT_OPTIMISED_TASK_SELECTION == 1
	/* Check the configuration. */
	#if( configMAX_PRIORITIES > 32 )
		#error configUSE_PORT_OPTIMISED_TASK_SELECTION can only be set to 1 when configMAX_PRIORITIES is less than or equal to 32.  It is very rare that a system requires more than 10 to 15 difference priorities as tasks that share a priority will time slice.
	#endif
#endif /* configUSE_PORT_OPTIMISED_TASK_SELECTION */

/* In case security extensions are implemented. */
#if configMAX_API_CALL_INTERRUPT_PRIORITY <= ( configUNIQUE_INTERRUPT_PRIORITIES / 2 )
	#error configMAX_API_CALL_INTERRUPT_PRIORITY must be greater than ( configUNIQUE_INTERRUPT_PRIORITIES / 2 )
#endif

/* Some vendor specific files default configCLEAR_TICK_INTERRUPT() in
portmacro.h. */
#ifndef configCLEAR_TICK_INTERRUPT
	#define configCLEAR_TICK_INTERRUPT()
#endif

/* A critical section is exited when the critical section nesting count reaches
this value. */
#define portNO_CRITICAL_NESTING			( ( size_t ) 0 )

/* In all GICs 255 can be written to the priority mask register to unmask all
(but the lowest) interrupt priority. */
#define portUNMASK_VALUE				( 0xFFUL )

/* Tasks are not created with a floating point context, but can be given a
floating point context after they have been created.  A variable is stored as
part of the tasks context that holds portNO_FLOATING_POINT_CONTEXT if the task
does not have an FPU context, or any other value if the task does have an FPU
context. */
#define portNO_FLOATING_POINT_CONTEXT	( ( StackType_t ) 0 )

/* Constants required to setup the initial task context. */
#define portSP_ELx						( ( StackType_t ) 0x01 )
#define portSP_EL0						( ( StackType_t ) 0x00 )

#if defined( GUEST )
	#define portEL1						( ( StackType_t ) 0x04 )
	#define portINITIAL_PSTATE				( portEL1 | portSP_EL0 )
#else
	#define portEL3						( ( StackType_t ) 0x0c )
	/* At the time of writing, the BSP only supports EL3. */
	#define portINITIAL_PSTATE			( portEL3 | portSP_EL0 )
#endif

/* Used by portASSERT_IF_INTERRUPT_PRIORITY_INVALID() when ensuring the binary
point is zero. */
#define portBINARY_POINT_BITS			( ( uint8_t ) 0x03 )

/* Masks all bits in the APSR other than the mode bits. */
#define portAPSR_MODE_BITS_MASK			( 0x0C )

/* The I bit in the DAIF bits. */
#define portDAIF_I						( 0x80 )

#if defined (GICv2)
/* Macro to unmask all interrupt priorities. */
#define portCLEAR_INTERRUPT_MASK_P()									\
{																	\
	portDISABLE_INTERRUPTS();										\
	portICCPMR_PRIORITY_MASK_REGISTER = portUNMASK_VALUE;			\
	__asm volatile (	"DSB SY		\n"								\
						"ISB SY		\n" );							\
	portENABLE_INTERRUPTS();										\
}
#else
#define portCLEAR_INTERRUPT_MASK_P()									\
{																	\
	portDISABLE_INTERRUPTS();										\
	mtcp(S3_0_C4_C6_0, portUNMASK_VALUE);							\
	__asm volatile (	"DSB SY		\n"								\
						"ISB SY		\n" );							\
	portENABLE_INTERRUPTS();										\
}
#endif

/* Hardware specifics used when sanity checking the configuration. */
#define portINTERRUPT_PRIORITY_REGISTER_OFFSET		0x400UL
#define portMAX_8_BIT_VALUE							( ( uint8_t ) 0xff )
#define portBIT_0_SET								( ( uint8_t ) 0x01 )


/*-----------------------------------------------------------*/

/*
 * Starts the first task executing.  This function is necessarily written in
 * assembly code so is implemented in portASM.s.
 */
extern void vPortRestoreTaskContext( void );

/*-----------------------------------------------------------*/

/* A variable is used to keep track of the critical section nesting.  This
variable has to be stored as part of the task context and must be initialised to
a non zero value to ensure interrupts don't inadvertently become unmasked before
the scheduler starts.  As it is stored as part of the task context it will
automatically be set to 0 when the first task is started. */
volatile uint64_t ullCriticalNesting = 9999ULL;

/*
 * The instance of the interrupt controller used by this port.  This is required
 * by the Xilinx library API functions.
 */
extern XScuGic xInterruptController;

/* Saved as part of the task context.  If ullPortTaskHasFPUContext is non-zero
then floating point context must be saved and restored for the task. */
uint64_t ullPortTaskHasFPUContext = pdFALSE;

/* Set to 1 to pend a context switch from an ISR. */
uint64_t ullPortYieldRequired[configNUMBER_OF_CORES] = { pdFALSE };

/* Counts the interrupt nesting depth.  A context switch is only performed if
if the nesting depth is 0. */
uint64_t ullPortInterruptNesting[configNUMBER_OF_CORES] = { 0 };

/* Used for check IN ISR */
volatile uint64_t uxCoreInIsr[configNUMBER_OF_CORES] = {0};

/*
 * Global counter used for calculation of run time statistics of tasks.
 * Defined only when the relevant option is turned on
 */
#if (configGENERATE_RUN_TIME_STATS==1)
volatile uint32_t ulHighFrequencyTimerTicks;
#endif

/* The space on the stack required to hold the FPU registers.  This is 32 128-bit
 * registers */
#define portFPU_REGISTER_WORDS	( 2 * 32 )

#if defined(GICv2)
/* Used in the ASM code. */
__attribute__(( used )) const uint64_t ullICCEOIR = portICCEOIR_END_OF_INTERRUPT_REGISTER_ADDRESS;
__attribute__(( used )) const uint64_t ullICCIAR = portICCIAR_INTERRUPT_ACKNOWLEDGE_REGISTER_ADDRESS;
__attribute__(( used )) const uint64_t ullICCPMR = portICCPMR_PRIORITY_MASK_REGISTER_ADDRESS;
#endif
__attribute__(( used )) const uint64_t ullMaxAPIPriorityMask = ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );

/*-----------------------------------------------------------*/
/*
 * Initialise the interrupt controller instance.
 */
static int32_t prvInitialiseInterruptController( void );

/* Ensure the interrupt controller instance variable is initialised before it is
 * used, and that the initialisation only happens once.
 */
static int32_t prvEnsureInterruptControllerIsInitialised( void );

int32_t lInterruptControllerInitialised[configNUMBER_OF_CORES] = {0};

/*
 * See header file for description.
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
	/* Setup the initial stack of the task.  The stack is set exactly as
	expected by the portRESTORE_CONTEXT() macro. */

	/* First all the general purpose registers. */
	pxTopOfStack--;
	*pxTopOfStack = 0x0101010101010101ULL;	/* R1 */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pvParameters; /* R0 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0303030303030303ULL;	/* R3 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0202020202020202ULL;	/* R2 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0505050505050505ULL;	/* R5 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0404040404040404ULL;	/* R4 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0707070707070707ULL;	/* R7 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0606060606060606ULL;	/* R6 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0909090909090909ULL;	/* R9 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0808080808080808ULL;	/* R8 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1111111111111111ULL;	/* R11 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1010101010101010ULL;	/* R10 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1313131313131313ULL;	/* R13 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1212121212121212ULL;	/* R12 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1515151515151515ULL;	/* R15 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1414141414141414ULL;	/* R14 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1717171717171717ULL;	/* R17 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1616161616161616ULL;	/* R16 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1919191919191919ULL;	/* R19 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1818181818181818ULL;	/* R18 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2121212121212121ULL;	/* R21 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2020202020202020ULL;	/* R20 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2323232323232323ULL;	/* R23 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2222222222222222ULL;	/* R22 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2525252525252525ULL;	/* R25 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2424242424242424ULL;	/* R24 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2727272727272727ULL;	/* R27 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2626262626262626ULL;	/* R26 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2929292929292929ULL;	/* R29 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2828282828282828ULL;	/* R28 */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) 0x00;	/* XZR - has no effect, used so there are an even number of registers. */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) 0x00;	/* R30 - procedure call link register. */
	pxTopOfStack--;

	*pxTopOfStack = portINITIAL_PSTATE;
	pxTopOfStack--;

	*pxTopOfStack = ( StackType_t ) pxCode; /* Exception return address. */

	#if( configUSE_TASK_FPU_SUPPORT == 1 )
	{
        /* The task will start without a floating point context.  A task that uses
        the floating point hardware must call vPortTaskUsesFPU() before executing
        any floating point instructions. */
        /* The task will start with a critical nesting count of 0 as interrupts are
        enabled. */
        pxTopOfStack--
        *pxTopOfStack = portNO_CRITICAL_NESTING;
        pxTopOfStack--;
        *pxTopOfStack = portNO_FLOATING_POINT_CONTEXT;
	}
	#elif( configUSE_TASK_FPU_SUPPORT == 2 )
	{
        /* The task will start with a floating point context.  Leave enough
            * space for the registers - and ensure they are initialised to 0. */
        pxTopOfStack -= portFPU_REGISTER_WORDS;
        BaseType_t i = 0;
        for (i = 0; i < portFPU_REGISTER_WORDS; ++i)
        {
            pxTopOfStack[i] = 0ULL;
        }
        pxTopOfStack--;
        *pxTopOfStack = portNO_CRITICAL_NESTING;
        pxTopOfStack--;
        *pxTopOfStack = pdTRUE;
        ullPortTaskHasFPUContext = pdTRUE;
	}
	#else
	{
		#error Invalid configUSE_TASK_FPU_SUPPORT setting - configUSE_TASK_FPU_SUPPORT must be set to 1, 2, or left undefined.
	}
	#endif

	return pxTopOfStack;
}
/*-----------------------------------------------------------*/

BaseType_t xPortInstallInterruptHandler( uint8_t ucInterruptID, XInterruptHandler pxHandler, void *pvCallBackRef )
{
int32_t lReturn;

	/* An API function is provided to install an interrupt handler */
	lReturn = prvEnsureInterruptControllerIsInitialised();
	if( lReturn == pdPASS )
	{
		lReturn = XScuGic_Connect( &xInterruptController, ucInterruptID, pxHandler, pvCallBackRef );
	}
	if( lReturn == XST_SUCCESS )
	{
		lReturn = pdPASS;
	}
	configASSERT( lReturn == pdPASS );

	return lReturn;
}
/*-----------------------------------------------------------*/

static int32_t prvEnsureInterruptControllerIsInitialised( void )
{
int32_t lReturn;

	/* Ensure the interrupt controller instance variable is initialised before
	it is used, and that the initialisation only happens once. */
	uint32_t ulCoreID = portGET_CORE_ID();
	if( lInterruptControllerInitialised[ulCoreID] != pdTRUE )
	{
		lReturn = prvInitialiseInterruptController();

		if( lReturn == pdPASS )
		{
			lInterruptControllerInitialised[ulCoreID] = pdTRUE;
		}
	}
	else
	{
		lReturn = pdPASS;
	}

	return lReturn;
}
/*-----------------------------------------------------------*/
uint32_t TSCIsrCnt[4] = {0, 0, 0, 0};
void vTaskSwitchContextISR(void* arg)
{
    portEND_SWITCHING_ISR(pdTRUE);
    TSCIsrCnt[portGET_CORE_ID()]++;
}

uint32_t IntCoreCnt[4] = {0, 0, 0, 0};

void vInterruptCore(uint32_t ulInterruptID, uint32_t ulCoreID)
{
    configASSERT(ulCoreID < configNUMBER_OF_CORES);
    configASSERT(ulInterruptID < 16);
    uint32_t ulRegVal = 0;
    uint32_t ulCoreMask = (1 << ulCoreID);
    ulRegVal |= ulInterruptID & 0xFu;
    ulRegVal |= (ulCoreMask << 16U);
    *((volatile uint32_t*)portICD_SGIR_ADDRESS) = ulRegVal;
    IntCoreCnt[ulCoreID] ++;
}

static int32_t prvInitialiseInterruptController( void )
{
BaseType_t xulStatus;
XScuGic_Config *pxGICConfig;

    BaseType_t xCoreID = portGET_CORE_ID();
    if (xCoreID == 0)
    {
        pxGICConfig = XScuGic_LookupConfig( configINTERRUPT_CONTROLLER_DEVICE_ID );
        xulStatus = XScuGic_CfgInitialize( &xInterruptController, pxGICConfig, pxGICConfig->CpuBaseAddress );
        /* Connect the interrupt controller interrupt handler to the hardware
        interrupt handling logic in the ARM processor. */
        Xil_ExceptionRegisterHandler( XIL_EXCEPTION_ID_IRQ_INT,
                                    ( Xil_ExceptionHandler ) XScuGic_InterruptHandler,
                                    &xInterruptController);
        XScuGic_SetPPI_SGI_Priority(&xInterruptController, portYIELD_CORE_INT_ID, portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT);
        XScuGic_Connect(&xInterruptController, portYIELD_CORE_INT_ID, vTaskSwitchContextISR, NULL);
    }
    else
    {
        // Secondary COREs only handle the portYIELD_CORE interrrupt, any other interrupts should be handled by CORE 0 
        extern s32 XScuGic_CpuIfInitialize(XScuGic *InstancePtr);
        xulStatus = XScuGic_CpuIfInitialize(&xInterruptController);
        XScuGic_SetPPI_SGI_Priority(&xInterruptController, portYIELD_CORE_INT_ID, portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT);
        XScuGic_Connect(&xInterruptController, portYIELD_CORE_INT_ID, vTaskSwitchContextISR, NULL);
    }

	/* For FreeRTOS SMP port, Exception will be enable at the end of vPortRestoreTaskContext */
	// Xil_ExceptionEnable();

	if( xulStatus == XST_SUCCESS )
	{
		xulStatus = pdPASS;
	}
	else
	{
		xulStatus = pdFAIL;
	}
	configASSERT( xulStatus == pdPASS );

	return xulStatus;
}
/*-----------------------------------------------------------*/

void vPortEnableInterrupt( uint8_t ucInterruptID )
{
int32_t lReturn;

	/* An API function is provided to enable an interrupt in the interrupt
	controller. */
	lReturn = prvEnsureInterruptControllerIsInitialised();
	if( lReturn == pdPASS )
	{
		XScuGic_Enable( &xInterruptController, ucInterruptID );
	}
	configASSERT( lReturn );
}
/*-----------------------------------------------------------*/

void vPortDisableInterrupt( uint8_t ucInterruptID )
{
int32_t lReturn;

	/* An API function is provided to disable an interrupt in the interrupt
	controller. */
	lReturn = prvEnsureInterruptControllerIsInitialised();
	if( lReturn == pdPASS )
	{
		XScuGic_Disable( &xInterruptController, ucInterruptID );
	}
	configASSERT( lReturn );
}
/*-----------------------------------------------------------*/

/* flag to control tick ISR handling, this is made true just before schedular start */

volatile uint64_t uxPortSchedularRunning = pdFALSE;

BaseType_t xPortStartScheduler( void )
{
uint32_t ulAPSR;
#if defined(GICv3)
uint32_t ulBpr;
#endif

	prvEnsureInterruptControllerIsInitialised();

	#if( configASSERT_DEFINED == 1 )
	{
		if (portGET_CORE_ID() == 0)
		{
			volatile uint32_t ulOriginalPriority;
	#if defined(GICv2)
			volatile uint8_t * const pucFirstUserPriorityRegister = ( volatile uint8_t * const ) ( configINTERRUPT_CONTROLLER_BASE_ADDRESS + portINTERRUPT_PRIORITY_REGISTER_OFFSET );
	#else
			volatile uint8_t * const pucFirstUserPriorityRegister = ( volatile uint8_t * const ) ( configINTERRUPT_CONTROLLER_BASE_ADDRESS + 0x420 );
	#endif
			volatile uint8_t ucMaxPriorityValue;

			/* Determine how many priority bits are implemented in the GIC.

			Save the interrupt priority value that is about to be clobbered. */
			ulOriginalPriority = *pucFirstUserPriorityRegister;

			/* Determine the number of priority bits available.  First write to
			all possible bits. */
			*pucFirstUserPriorityRegister = portMAX_8_BIT_VALUE;

			/* Read the value back to see how many bits stuck. */
			ucMaxPriorityValue = *pucFirstUserPriorityRegister;

			/* Shift to the least significant bits. */
			while( ( ucMaxPriorityValue & portBIT_0_SET ) != portBIT_0_SET )
			{
				ucMaxPriorityValue >>= ( uint8_t ) 0x01;
			}

			/* Sanity check configUNIQUE_INTERRUPT_PRIORITIES matches the read
			value. */

			configASSERT( ucMaxPriorityValue >= portLOWEST_INTERRUPT_PRIORITY );


			/* Restore the clobbered interrupt priority register to its original
			value. */
			*pucFirstUserPriorityRegister = ulOriginalPriority;
		}
	}
	#endif /* conifgASSERT_DEFINED */

	/* At the time of writing, the BSP only supports EL3. */
	__asm volatile ( "MRS %0, CurrentEL" : "=r" ( ulAPSR ) );
	ulAPSR &= portAPSR_MODE_BITS_MASK;

#if defined( GUEST )
	#warning Building for execution as a guest under XEN. THIS IS NOT A FULLY TESTED PATH.
	configASSERT( ulAPSR == portEL1 );
	if( ulAPSR == portEL1 )
#else
	configASSERT( ulAPSR == portEL3 );
	if( ulAPSR == portEL3 )
#endif
	{
		/* Only continue if the binary point value is set to its lowest possible
		setting.  See the comments in vPortValidateInterruptPriority() below for
		more information. */
#if defined(GICv2)
		configASSERT( ( portICCBPR_BINARY_POINT_REGISTER & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE );

		if( ( portICCBPR_BINARY_POINT_REGISTER & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE )
#else
		ulBpr = mfcp(S3_0_C12_C8_3);
		configASSERT( ( ulBpr & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE );

		if( ( ulBpr & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE )
#endif
		{
			/* Interrupts are turned off in the CPU itself to ensure a tick does
			not execute	while the scheduler is being started.  Interrupts are
			automatically turned back on in the CPU when the first task starts
			executing. */
			portDISABLE_INTERRUPTS();

            if (portGET_CORE_ID() == 0)
            {
            	/* Start the timer that generates the tick ISR. */
                configSETUP_TICK_INTERRUPT();
                uxPortSchedularRunning = pdTRUE;
            }

			/* Start the first task executing. */
			vPortRestoreTaskContext();
		}
	}

	return 0;
}
/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
	/* Not implemented in ports where there is nothing to return to.
	Artificially force an assert. */
	configASSERT( 0 );
}

/*-----------------------------------------------------------*/

void FreeRTOS_Tick_Handler( void )
{
#if defined(GICv3)
	unsigned int ulRpr;
#endif

	/*
	 * The Xilinx implementation of generating run time task stats uses the same timer used for generating
	 * FreeRTOS ticks. In case user decides to generate run time stats the tick handler is called more
	 * frequently (10 times faster). The timer/tick handler uses logic to handle the same. It handles
	 * the FreeRTOS tick once per 10 interrupts.
	 * For handling generation of run time stats, it increments a pre-defined counter every time the
	 * interrupt handler executes.
	 */
#if (configGENERATE_RUN_TIME_STATS == 1)
	ulHighFrequencyTimerTicks++;
	if (!(ulHighFrequencyTimerTicks % 10))
#endif
	{

		/* Must be the lowest possible priority. */
#if !defined( QEMU )
		{
#if defined(GICv2)
			configASSERT( portICCRPR_RUNNING_PRIORITY_REGISTER == ( uint32_t ) ( portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) );
#else
			ulRpr = mfcp(S3_0_C12_C11_3);
			configASSERT( ulRpr == ( uint32_t ) ( portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) );
#endif
		}
#endif

	/* Interrupts should not be enabled before this point. */
#if( configASSERT_DEFINED == 1 )
		{
			uint32_t ulMaskBits;

			__asm volatile( "mrs %0, daif" : "=r"( ulMaskBits ) :: "memory" );
			configASSERT( ( ulMaskBits & portDAIF_I ) != 0 );
		}
#endif /* configASSERT_DEFINED */
	/* Set interrupt mask before altering scheduler structures.   The tick
	handler runs at the lowest priority, so interrupts cannot already be masked,
	so there is no need to save and restore the current mask value.  It is
	necessary to turn off interrupts in the CPU itself while the ICCPMR is being
	updated. */
#if defined(GICv2)
		portICCPMR_PRIORITY_MASK_REGISTER = ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );
#else
		mtcp(S3_0_C4_C6_0, ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ));
#endif
		__asm volatile (	"dsb sy		\n"
							"isb sy		\n" ::: "memory" );

		/* Ok to enable interrupts after the interrupt source has been cleared. */
		configCLEAR_TICK_INTERRUPT();
		portENABLE_INTERRUPTS();

        UBaseType_t x = portENTER_CRITICAL_FROM_ISR();
		/* Increment the RTOS tick. */
		if( xTaskIncrementTick() != pdFALSE )
		{
			ullPortYieldRequired[portGET_CORE_ID()] = pdTRUE;
		}
        portEXIT_CRITICAL_FROM_ISR(x);
	}

	/* Ensure all interrupt priorities are active again. */
	portCLEAR_INTERRUPT_MASK_P();
}
/*-----------------------------------------------------------*/
#if( configUSE_TASK_FPU_SUPPORT != 2 )
void vPortTaskUsesFPU( void )
{
	/* A task is registering the fact that it needs an FPU context.  Set the
	FPU flag (which is saved as part of the task context). */
	ullPortTaskHasFPUContext = pdTRUE;

	/* Consider initialising the FPSR here - but probably not necessary in
	AArch64. */
}
#endif /* configUSE_TASK_FPU_SUPPORT */
/*-----------------------------------------------------------*/

void vPortClearInterruptMask( UBaseType_t uxNewMaskValue )
{
	if( uxNewMaskValue == pdFALSE )
	{
		portCLEAR_INTERRUPT_MASK_P();
	}
}
/*-----------------------------------------------------------*/

UBaseType_t uxPortSetInterruptMask( void )
{
uint32_t ulReturn;
uint32_t mask;

	if (lInterruptControllerInitialised[portGET_CORE_ID()] == pdTRUE) {
		/* Interrupt in the CPU must be turned off while the ICCPMR is being
		updated. */
		portDISABLE_INTERRUPTS();
#if defined(GICv2)
		mask = portICCPMR_PRIORITY_MASK_REGISTER;
#else
		mask = mfcp(S3_0_C4_C6_0);
#endif
		if( mask == ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) )
		{
			/* Interrupts were already masked. */
			ulReturn = pdTRUE;
		}
		else
		{
			ulReturn = pdFALSE;
#if defined(GICv2)
			portICCPMR_PRIORITY_MASK_REGISTER = ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );
#else
			mtcp(S3_0_C4_C6_0, ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ));
#endif
			__asm volatile (	"dsb sy		\n"
								"isb sy		\n" ::: "memory" );
		}
		portENABLE_INTERRUPTS();
	} else {
		ulReturn = pdTRUE;
	}
	return ulReturn;
}
/*-----------------------------------------------------------*/

#if( configASSERT_DEFINED == 1 )

	void vPortValidateInterruptPriority( void )
	{
#if defined(GICv3)
		unsigned int ulRpr;
		unsigned int ulBpr;
#endif
		/* The following assertion will fail if a service routine (ISR) for
		an interrupt that has been assigned a priority above
		configMAX_SYSCALL_INTERRUPT_PRIORITY calls an ISR safe FreeRTOS API
		function.  ISR safe FreeRTOS API functions must *only* be called
		from interrupts that have been assigned a priority at or below
		configMAX_SYSCALL_INTERRUPT_PRIORITY.

		Numerically low interrupt priority numbers represent logically high
		interrupt priorities, therefore the priority of the interrupt must
		be set to a value equal to or numerically *higher* than
		configMAX_SYSCALL_INTERRUPT_PRIORITY.

		FreeRTOS maintains separate thread and ISR API functions to ensure
		interrupt entry is as fast and simple as possible. */
#if defined(GICv2)
		configASSERT( portICCRPR_RUNNING_PRIORITY_REGISTER >= ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) );
#else
		ulRpr = mfcp(S3_0_C12_C11_3);
		configASSERT( ulRpr >= ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) );
#endif
		/* Priority grouping:  The interrupt controller (GIC) allows the bits
		that define each interrupt's priority to be split between bits that
		define the interrupt's pre-emption priority bits and bits that define
		the interrupt's sub-priority.  For simplicity all bits must be defined
		to be pre-emption priority bits.  The following assertion will fail if
		this is not the case (if some bits represent a sub-priority).

		The priority grouping is configured by the GIC's binary point register
		(ICCBPR).  Writing 0 to ICCBPR will ensure it is set to its lowest
		possible value (which may be above 0). */
#if defined(GICv2)
		configASSERT( ( portICCBPR_BINARY_POINT_REGISTER & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE );
#else
		ulBpr = mfcp(S3_0_C12_C8_3);
		configASSERT( ( ulBpr & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE );
#endif
	}

#endif /* configASSERT_DEFINED */
/*-----------------------------------------------------------*/

void vIRQHandler( uint32_t ulICCIAR )
{
    BaseType_t xCoreId = portGET_CORE_ID();
    uxCoreInIsr[xCoreId]++;

    extern void vApplicationIRQHandler( uint32_t ulICCIAR );
    vApplicationIRQHandler(ulICCIAR);

    uxCoreInIsr[xCoreId]--;
}

#define portRTOS_LOCK_COUNT     (2u)
#define portMAX_CORE_COUNT      configNUMBER_OF_CORES

/* Which core owns the lock */
volatile uint64_t ucOwnedByCore[ portMAX_CORE_COUNT ];
/* Lock count a core owns */
volatile uint64_t ucRecursionCountByLock[ portRTOS_LOCK_COUNT ];
/* Index 0 is used for ISR lock and Index 1 is used for task lock */
uint32_t ulGateWord[ portRTOS_LOCK_COUNT ];


static inline void vSpinUnlock(uint32_t* ulLock)
{
    __asm volatile(
        "dmb sy\n"
        "mov w1, #0\n"
        "str w1, [%x0]\n"
        "dsb sy\n"
        "sev\n"
        :
        :"r" (ulLock)
        : "memory", "w1"
    );
}

static inline int32_t lSpinTrylock(uint32_t* ulLock)
{
    register int32_t lRet;

    __asm volatile(
        "1:\n"
        "ldxr w1, [%x1]\n"
        "cmp w1, #1\n"
        "beq 2f\n"
        "mov w2, #1\n"
        "stxr w1, w2, [%x1]\n"
        "cmp w1, #0\n"
        "bne 1b\n"
        "2:\n"
        "mov %w0, w1\n"
        :"=r" (lRet)
        :"r" (ulLock)
        : "memory", "w1", "w2"
    );

    return lRet;
}

/* Read 64b value shared between cores */
static inline uint64_t uxGet64(volatile uint64_t* x)
{
    __asm("dsb sy");
    return *x;
}

/* Write 64b value shared between cores */
static inline void vSet64(volatile uint64_t* x, uint64_t value)
{
    *x = value;
    __asm("dsb sy");
}

void vPortRecursiveLock(BaseType_t xCoreID, uint32_t ulLockNum, BaseType_t uxAcquire)
{
    uint32_t ulLockBit = 1u << ulLockNum;

    /* Lock acquire */
    if (uxAcquire)
    {
        /* Check if spinlock is available */
        /* If spinlock is not available check if the core owns the lock */
        /* If the core owns the lock wait increment the lock count by the core */
        /* If core does not own the lock wait for the spinlock */
        if( lSpinTrylock(&ulGateWord[ulLockNum]) != 0)
        {
            /* Check if the core owns the spinlock */
            if( uxGet64(&ucOwnedByCore[xCoreID]) & ulLockBit )
            {
                configASSERT( uxGet64(&ucRecursionCountByLock[ulLockNum]) != 255u);
                vSet64(&ucRecursionCountByLock[ulLockNum], (uxGet64(&ucRecursionCountByLock[ulLockNum])+1));
                return;
            }

            /* Preload the gate word into the cache */
            uint32_t dummy = ulGateWord[ulLockNum];
            dummy++;

            while (lSpinTrylock(&ulGateWord[ulLockNum]) != 0)
                __asm volatile ("wfe");
        }

         /* Add barrier to ensure lock is taken before we proceed */
        __asm__ __volatile__ ( "dmb sy" ::: "memory" );

        /* Assert the lock count is 0 when the spinlock is free and is acquired */
        configASSERT(uxGet64(&ucRecursionCountByLock[ulLockNum]) == 0);

        /* Set lock count as 1 */
        vSet64(&ucRecursionCountByLock[ulLockNum], 1);
        /* Set ucOwnedByCore */
        vSet64(&ucOwnedByCore[xCoreID], (uxGet64(&ucOwnedByCore[xCoreID]) | ulLockBit));
    }
    /* Lock release */
    else
    {
        /* Assert the lock is not free already */
        configASSERT( (uxGet64(&ucOwnedByCore[xCoreID]) & ulLockBit) != 0 );
        configASSERT( uxGet64(&ucRecursionCountByLock[ulLockNum]) != 0 );

        /* Reduce ucRecursionCountByLock by 1 */
        vSet64(&ucRecursionCountByLock[ulLockNum], (uxGet64(&ucRecursionCountByLock[ulLockNum]) - 1) );

        if( !uxGet64(&ucRecursionCountByLock[ulLockNum]) )
        {
            vSet64(&ucOwnedByCore[xCoreID], (uxGet64(&ucOwnedByCore[xCoreID]) & ~ulLockBit));
            vSpinUnlock(&ulGateWord[ulLockNum]);
            __asm volatile("sev");
            /* Add barrier to ensure lock is taken before we proceed */
            __asm__ __volatile__ (  "dmb sy" ::: "memory" );
        }
    }
}


#if( configGENERATE_RUN_TIME_STATS == 1 )
/*
 * For Xilinx implementation this is a dummy function that does a redundant operation
 * of zeroing out the global counter.
 * It is called by FreeRTOS kernel.
 */
void xCONFIGURE_TIMER_FOR_RUN_TIME_STATS (void)
{
	ulHighFrequencyTimerTicks = 0;
}
/*
 * For Xilinx implementation this function returns the global counter used for
 * run time task stats calculation.
 * It is called by FreeRTOS kernel task handling logic.
 */
uint32_t xGET_RUN_TIME_COUNTER_VALUE (void)
{
	return ulHighFrequencyTimerTicks;
}
#endif
