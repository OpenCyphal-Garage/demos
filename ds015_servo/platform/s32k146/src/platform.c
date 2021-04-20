/*
 * Copyright (c) 2020, NXP. All rights reserved.
 * Distributed under The MIT License.
 * Author: Abraham Rodriguez <abraham.rodriguez@nxp.com>
 *
 * Description:
 * Example of using libcanard with UAVCAN V1.0 in the S32K1 platform
 * please reference the S32K1 manual, focused for development with
 * the UCANS32K146 board.
 * The files that are particular to this demo application are in the \src folder rather
 * than in the \include, where are the general libraries and headers.
 *
 * Transmits an UAVCAN Heartbeat message between two UCANS32K146 boards.
 *
 */

#include "canfd.h"
#include "LPIT.h"
#include "SCG.h"
#include "S32K146_bitfields.h"
#include "s32_core_cm4.h"
#include "platform.h"

#define FRAME_UNLOAD_PERIOD_MILI (500u)
#define FRAME_UNLOAD_IRQ_PRIO      (2u)
#define FLEXCAN_RX_IRQ_PRIO        (1u)

// Linker file symbols for o1heap allcator
void* __HeapBasee = (void*)0x200000a0;
size_t HEAP_SIZEe = 0x8000;

// Application-specific function prototypes
void FlexCAN0_reception_callback(void);
void abort(void);
void UCANS32K146_PIN_MUX();
void greenLED_init(void);
void greenLED_toggle(void);

int init_platform(O1HeapInstance** out_allocator)
{
    int result = 0;

    // Initialization of o1heap allocator if requested.
    if (out_allocator)
    {
        O1HeapInstance* inst = o1heapInit(__HeapBasee, HEAP_SIZEe, NULL, NULL);
        if(inst == NULL)
        {
            result = 1;
        }
        *out_allocator = inst;
    }

    /* Configure clock source */
    SCG_SOSC_8MHz_Init();
    SCG_SPLL_160MHz_Init();
    SCG_Normal_RUN_Init();

    // Indicative board LED for successful transmission
    greenLED_init();
    greenLED_toggle();

    // 64-bit monotonic timer start
    LPIT0_Timestamping_Timer_Init();

    // Pin mux
    UCANS32K146_PIN_MUX();

    // Initialize FlexCAN0
    FlexCAN0_Init(CANFD_1MB_4MB_PLL, FLEXCAN_RX_IRQ_PRIO, FlexCAN0_reception_callback);

    return result;
}

void heartbeat(void)
{
    // Toggle LED at 1Hz
    greenLED_toggle();
}

void FlexCAN0_reception_callback(void)
{
    // Nothing to do in the transmission node
}

void abort(void)
{
    while(1){}
}

void UCANS32K146_PIN_MUX(void)
{
    /* Multiplex FlexCAN0 pins */
    PCC->PCC_PORTE_b.CGC = PCC_PCC_PORTE_CGC_1;   /* Clock gating to PORT E */
    PORTE->PORTE_PCR4_b.MUX = PORTE_PCR4_MUX_101; /* CAN0_RX at PORT E pin 4 */
    PORTE->PORTE_PCR5_b.MUX = PORTE_PCR5_MUX_101; /* CAN0_TX at PORT E pin 5 */

    PCC->PCC_PORTA_b.CGC = PCC_PCC_PORTA_CGC_1;   /* Clock gating to PORT A */
    PORTA->PORTA_PCR12_b.MUX = PORTA_PCR12_MUX_011; /* CAN1_RX at PORT A pin 12 */
    PORTA->PORTA_PCR13_b.MUX = PORTA_PCR13_MUX_011; /* CAN1_TX at PORT A pin 13 */

    /* Set to LOW the standby (STB) pin in both transceivers of the UCANS32K146 node board */
    PORTE->PORTE_PCR11_b.MUX = PORTE_PCR11_MUX_001; /* MUX to GPIO */
    PTE->GPIOE_PDDR |= 1 << 11;                   /* Set direction as output */
    PTE->GPIOE_PCOR |= 1 << 11;                   /* Set the pin LOW */

    PORTE->PORTE_PCR10_b.MUX = PORTE_PCR10_MUX_001; /* Same as above */
    PTE->GPIOE_PDDR |= 1 << 10;
    PTE->GPIOE_PCOR |= 1 << 10;
}

void greenLED_init(void)
{
    PCC->PCC_PORTD_b.CGC = PCC_PCC_PORTD_CGC_1;     /* Enable clock for PORTD */
    PORTD->PORTD_PCR16_b.MUX = PORTE_PCR16_MUX_001; /* Port D16: MUX = GPIO */
    PTD->GPIOD_PDDR |= 1<<16;                       /* Port D16: Data direction = output  */

}

void greenLED_toggle(void)
{
    PTD->GPIOD_PTOR |= 1<<16;
}
