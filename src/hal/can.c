#include <stddef.h>

#include "hal.h"
#include "cmsis/stm32xx.h"

void irq_CAN1_TX() { }

static void
irq_CAN1_RX(int mb)
{
	uint32_t			xLO, xHI;

	hal.CAN_msg.ID = (uint16_t) (CAN1->sFIFOMailBox[mb].RIR >> 21);
	hal.CAN_msg.len = (uint16_t) (CAN1->sFIFOMailBox[mb].RDTR & 0xFU);

	xLO = CAN1->sFIFOMailBox[mb].RDLR;
	xHI = CAN1->sFIFOMailBox[mb].RDHR;

	hal.CAN_msg.payload[0] = (uint8_t) (xLO & 0xFFU);
	hal.CAN_msg.payload[1] = (uint8_t) ((xLO >> 8) & 0xFFU);
	hal.CAN_msg.payload[2] = (uint8_t) ((xLO >> 16) & 0xFFU);
	hal.CAN_msg.payload[3] = (uint8_t) ((xLO >> 24) & 0xFFU);
	hal.CAN_msg.payload[4] = (uint8_t) (xHI & 0xFFU);
	hal.CAN_msg.payload[5] = (uint8_t) ((xHI >> 8) & 0xFFU);
	hal.CAN_msg.payload[6] = (uint8_t) ((xHI >> 16) & 0xFFU);
	hal.CAN_msg.payload[7] = (uint8_t) ((xHI >> 24) & 0xFFU);
}

void irq_CAN1_RX0()
{
	irq_CAN1_RX(0);

	CAN1->RF0R |= CAN_RF0R_RFOM0;

	CAN_IRQ();
}

void irq_CAN1_RX1()
{
	irq_CAN1_RX(1);

	CAN1->RF1R |= CAN_RF1R_RFOM1;

	CAN_IRQ();
}

void irq_CAN1_SCE() { }

void CAN_startup()
{
	/* Enable CAN1 clock.
	 * */
	RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;

	/* Enable CAN1 pins.
	 * */
	GPIO_set_mode_FUNCTION(GPIO_CAN_RX);
	GPIO_set_mode_FUNCTION(GPIO_CAN_TX);
	GPIO_set_mode_SPEED_HIGH(GPIO_CAN_RX);
	GPIO_set_mode_SPEED_HIGH(GPIO_CAN_TX);

	/* Force a master RESET.
	 * */
	CAN1->MCR = CAN_MCR_RESET;

	/* Enable IRQs.
	 * */
	NVIC_SetPriority(CAN1_RX0_IRQn, 7);
	NVIC_SetPriority(CAN1_RX1_IRQn, 7);
	NVIC_EnableIRQ(CAN1_RX0_IRQn);
	NVIC_EnableIRQ(CAN1_RX1_IRQn);

	/* Configure CAN1.
	 * */
	CAN_configure();
}

static int
CAN_wait_for_MSR(uint32_t xBITS, uint32_t xVAL)
{
	uint32_t		xMSR;
	int		wait_N = 0;

	do {
		xMSR = CAN1->MSR & xBITS;

		if (xMSR == xVAL) {

			return 1;
		}

		__NOP();

		wait_N++;
	}
	while (wait_N < 70000U);

	return 0;
}

void CAN_configure()
{
	/* No mode SLEEP.
	 * */
	CAN1->MCR &= ~CAN_MCR_SLEEP;

	if (CAN_wait_for_MSR(CAN_MSR_SLAK, 0) == 0) {

		log_TRACE("CAN from SLEEP failed" EOL);
	}

	/* Go to mode INIT.
	 * */
	CAN1->MCR |= CAN_MCR_INRQ;

	if (CAN_wait_for_MSR(CAN_MSR_INAK, CAN_MSR_INAK) == 0) {

		log_TRACE("CAN to INIT failed" EOL);
	}

	CAN1->MCR |= CAN_MCR_ABOM | CAN_MCR_AWUM | CAN_MCR_TXFP;
	CAN1->IER = CAN_IER_FMPIE0 | CAN_IER_FMPIE1;

	/* Bit timing (1 Mbit/s).
	 * */
	CAN1->BTR = (5U << 20) | (6U << 16) | (2U);

	CAN1->FMR |= CAN_FMR_FINIT;

	/* Enable all 28 filters to CAN1.
	 * */
	MODIFY_REG(CAN1->FMR, 0x3F00U, 28U << 8);

	CAN1->FMR &= ~CAN_FMR_FINIT;

	/* Go to mode NORMAL.
	 * */
	CAN1->MCR &= ~CAN_MCR_INRQ;

	if (CAN_wait_for_MSR(CAN_MSR_INAK, CAN_MSR_INAK) == 0) {

		log_TRACE("CAN to NORMAL failed" EOL);
	}
}

void CAN_filter_ID(int fs, int mb, int ID, int mID)
{
	uint32_t		BFS = (1U << fs);

	CAN1->FMR |= CAN_FMR_FINIT;
	CAN1->FA1R &= ~BFS;

	if (ID != 0) {

		CAN1->FM1R &= ~BFS;
		CAN1->FS1R |= BFS;

		CAN1->FFA1R &= ~BFS;
		CAN1->FFA1R |= (mb == 1) ? BFS : 0U;

		CAN1->sFilterRegister[fs].FR1 = (ID << 21);
		CAN1->sFilterRegister[fs].FR2 = (mID << 21) + 6U;

		CAN1->FA1R |= BFS;
	}

	CAN1->FMR &= ~CAN_FMR_FINIT;
}

int CAN_send_msg(const CAN_msg_t *msg)
{
	uint32_t		xTSR;
	int		mb, irq;

	irq = hal_lock_irq();

	xTSR = CAN1->TSR;

	if (xTSR & CAN_TSR_TME_Msk) {

		mb = (xTSR & CAN_TSR_CODE_Msk) >> CAN_TSR_CODE_Pos;
	}
	else {
		hal_unlock_irq(irq);

		return CAN_TX_FAILED;
	}

	CAN1->sTxMailBox[mb].TIR = (uint32_t) msg->ID << 21;
	CAN1->sTxMailBox[mb].TDTR = (uint32_t) msg->len;

	CAN1->sTxMailBox[mb].TDLR =
		  ((uint32_t) msg->payload[0])
		| ((uint32_t) msg->payload[1] << 8)
		| ((uint32_t) msg->payload[2] << 16)
		| ((uint32_t) msg->payload[3] << 24);

	CAN1->sTxMailBox[mb].TDHR =
		  ((uint32_t) msg->payload[4])
		| ((uint32_t) msg->payload[5] << 8)
		| ((uint32_t) msg->payload[6] << 16)
		| ((uint32_t) msg->payload[7] << 24);

	CAN1->sTxMailBox[mb].TIR |= CAN_TI0R_TXRQ;

	hal_unlock_irq(irq);

	return CAN_TX_OK;
}

