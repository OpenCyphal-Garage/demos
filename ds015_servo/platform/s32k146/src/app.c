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

#include "uavcan/node/Heartbeat_1_0.h"
#include "canard.h"
#include "canfd.h"
#include "o1heap.h"
#include "LPIT.h"
#include "SCG.h"
#include "S32K146_bitfields.h"
#include "s32_core_cm4.h"

#define FRAME_UNLOAD_PERIOD_MILI (500u)
#define FRAME_UNLOAD_IRQ_PRIO 	 (2u)
#define FLEXCAN_RX_IRQ_PRIO   	 (1u)

// Linker file symbols for o1heap allcator
void* __HeapBasee = (void*)0x200000a0;
size_t HEAP_SIZEe = 0x8000;

// allocator and instance declaration for wrappers
O1HeapInstance* my_allocator;
CanardInstance ins;

// Wrappers for using o1heap allocator with libcanard
static void* memAllocate(CanardInstance* const ins, const size_t amount);
static void memFree(CanardInstance* const ins, void* const pointer);

// Application-specific function prototypes
void FlexCAN0_reception_callback(void);
void abort(void);
void process_canard_TX_queue();
void UCANS32K146_PIN_MUX();
void greenLED_init(void);
void greenLED_toggle(void);

static uint8_t my_message_transfer_id = 0;

// Uptime counter variable for heartbeat message
uint32_t test_uptimeSec = 0;

// buffer for serialization of a heartbeat message
size_t hbeat_ser_buf_size = uavcan_node_Heartbeat_1_0_EXTENT_BYTES_;
uint8_t hbeat_ser_buf[uavcan_node_Heartbeat_1_0_EXTENT_BYTES_];

int main(void) {

	// Initialization of o1heap allocator for libcanard
	my_allocator = o1heapInit(__HeapBasee, HEAP_SIZEe, NULL, NULL);

	// Initialization of a canard instance with the previous allocator
	CanardInstance ins = canardInit(&memAllocate, &memFree);
	ins.mtu_bytes = CANARD_MTU_CAN_FD;
	ins.node_id = 96;

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

	// Setup IRQ for processing the TX queue
	LPIT0_Ch2_IRQ_Config(FRAME_UNLOAD_PERIOD_MILI, FRAME_UNLOAD_IRQ_PRIO, process_canard_TX_queue);

	for(;;)
	{
		// Create a heartbeat message
		uavcan_node_Heartbeat_1_0 test_heartbeat = {
			.uptime = test_uptimeSec,
			.health = { uavcan_node_Health_1_0_NOMINAL },
			.mode =  { uavcan_node_Mode_1_0_OPERATIONAL }
		};


		// Serialize the previous heartbeat message
		int8_t result1 = uavcan_node_Heartbeat_1_0_serialize_(&test_heartbeat, hbeat_ser_buf, &hbeat_ser_buf_size);

		if(result1 < 0) { abort(); }

		// Create a transfer for the heartbeat message
		const CanardTransfer transfer = {
				.timestamp_usec = 0,
				.priority = CanardPriorityNominal,
				.transfer_kind = CanardTransferKindMessage,
				.port_id = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_,
				.remote_node_id = CANARD_NODE_ID_UNSET,
				.transfer_id = my_message_transfer_id,
				.payload_size = hbeat_ser_buf_size,
				.payload = hbeat_ser_buf,
		};

		// Increment the transfer_id variable
		++my_message_transfer_id;

		// Push the transfer to the tx queue
		int32_t result2 = canardTxPush(&ins, &transfer);

		if(result2 < 0) { abort(); }

		// Block for a second for generating the next transfer and increase uptime
		uint64_t initial_time = LPIT0_GetTimestamp();
		uint64_t delta = 0;
		const uint64_t cycles_second = 80e6;

		while(delta < cycles_second)
		{
			uint64_t cur_time = LPIT0_GetTimestamp();
			delta = cur_time - initial_time;
		}

		test_uptimeSec++;

		// Toggle LED at same frequency of transmission
		greenLED_toggle();
	}

	return 0;
}


static void* memAllocate(CanardInstance* const ins, const size_t amount)
{
    (void) ins;
    return o1heapAllocate(my_allocator, amount);
}

static void memFree(CanardInstance* const ins, void* const pointer)
{
    (void) ins;
    o1heapFree(my_allocator, pointer);
}

void FlexCAN0_reception_callback(void)
{
	// Nothing to do in the transmission node
}

void abort(void)
{
	while(1){}
}

void process_canard_TX_queue(void)
{
	/* Look at top of the TX queue of individual CANFD frames */
	for(const CanardFrame* txf = NULL; (txf = canardTxPeek(&ins)) != NULL;)
	{
		/* Ensure TX deadline has not expired */
		if(txf->timestamp_usec > LPIT0_GetTimestamp())
		{
			/* Instantiate a fdframe for the media layer */
			fdframe_t txframe;
			txframe.PAYLOAD = txf->payload;
			txframe.PAYLOAD_SIZE_BYTES = txf->payload_size;
			txframe.EXTENDED_ID = txf->extended_can_id;

			/* Send the individual frame and break if no message buffers are available */
			if(!FlexCAN0_Send(&txframe))
			{
				break;
			}
		}

		/* Remove the frame from the queue after a successful transmission */
		canardTxPop(&ins);
		/* Deallocation of the memory utilized by that frame popped */
		ins.memory_free(&ins, (CanardFrame*)txf);

	}
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
    PTE->GPIOE_PDDR |= 1 << 11; 				  /* Set direction as output */
    PTE->GPIOE_PCOR |= 1 << 11; 				  /* Set the pin LOW */

    PORTE->PORTE_PCR10_b.MUX = PORTE_PCR10_MUX_001; /* Same as above */
    PTE->GPIOE_PDDR |= 1 << 10;
    PTE->GPIOE_PCOR |= 1 << 10;
}

void greenLED_init(void)
{
	PCC->PCC_PORTD_b.CGC = PCC_PCC_PORTD_CGC_1; 	/* Enable clock for PORTD */
	PORTD->PORTD_PCR16_b.MUX = PORTE_PCR16_MUX_001; /* Port D16: MUX = GPIO */
    PTD->GPIOD_PDDR |= 1<<16;                       /* Port D16: Data direction = output  */

}

void greenLED_toggle(void)
{
	PTD->GPIOD_PTOR |= 1<<16;
}
