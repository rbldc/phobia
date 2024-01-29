#define HW_MCU_STM32F405

/* Tested on FLIPSKY MINI FSESC 6.7 PRO
 * */

#define HW_HAVE_LOW_SIDE_SHUNT
#define HW_HAVE_DRV_ON_PCB
#define HW_HAVE_ANALOG_KNOB
#define HW_HAVE_BRAKE_KNOB
#define HW_HAVE_STEP_DIR_KNOB
#define HW_HAVE_NTC_ON_PCB
#define HW_HAVE_NTC_MACHINE
#define HW_HAVE_USB_CDC_ACM
#define HW_HAVE_NETWORK_EPCAN

#define HW_CLOCK_CRYSTAL_HZ		8000000U

#define HW_PWM_FREQUENCY_HZ		28571.f
#define HW_PWM_DEADTIME_NS		330.f		/* NVMFS5C612NL */

#define HW_PWM_MINIMAL_PULSE		0.4f
#define HW_PWM_CLEARANCE_ZONE		5.0f
#define HW_PWM_SKIP_ZONE		2.0f
#define HW_PWM_BOOTSTRAP_RETENTION	100.f		/* DRV8301 */

#define HW_DRV_ID_ON_PCB		BUS_ID_SPI3

#define HW_DRV_PARTNO			DRV_PART_DRV8301
#define HW_DRV_GATE_CURRENT		0
#define HW_DRV_OCP_LEVEL		22		/* 0.82 (V) */

#define HW_ADC_SAMPLING_SEQUENCE	ADC_SEQUENCE__ABC_UTT_TXX

#define HW_ADC_REFERENCE_VOLTAGE	3.3f
#define HW_ADC_SHUNT_RESISTANCE		0.0005f
#define HW_ADC_AMPLIFIER_GAIN		20.f		/* INA181A1 */

#define HW_ADC_VOLTAGE_R1		39000.f
#define HW_ADC_VOLTAGE_R2		2200.f
#define HW_ADC_VOLTAGE_R3		1000000000000.f		/* have no bias */

#define HW_ADC_KNOB_R1			0.f			/* have no */
#define HW_ADC_KNOB_R2			1000.f

#define HW_NTC_PCB_TYPE			NTC_VCC
#define HW_NTC_PCB_BALANCE		10000.f
#define HW_NTC_PCB_NTC0			10000.f
#define HW_NTC_PCB_TA0			25.f
#define HW_NTC_PCB_BETTA		3380.f			/* unknown part */

#define HW_NTC_EXT_BALANCE		10000.f

#define GPIO_ADC_CURRENT_A		XGPIO_DEF3('C', 0, 10)
#define GPIO_ADC_CURRENT_B		XGPIO_DEF3('C', 1, 11)
#define GPIO_ADC_CURRENT_C		XGPIO_DEF3('C', 2, 12)
#define GPIO_ADC_VOLTAGE_U		XGPIO_DEF3('C', 3, 13)
#define GPIO_ADC_VOLTAGE_A		XGPIO_DEF3('A', 0, 0)
#define GPIO_ADC_VOLTAGE_B		XGPIO_DEF3('A', 1, 1)
#define GPIO_ADC_VOLTAGE_C		XGPIO_DEF3('A', 2, 2)
#define GPIO_ADC_NTC_PCB		XGPIO_DEF3('A', 3, 3)
#define GPIO_ADC_NTC_EXT		XGPIO_DEF3('C', 4, 14)
#define GPIO_ADC_KNOB_ANG		XGPIO_DEF3('A', 5, 5)
#define GPIO_ADC_KNOB_BRK		XGPIO_DEF3('A', 6, 6)

#define GPIO_STEP			XGPIO_DEF2('A', 5)
#define GPIO_DIR			XGPIO_DEF2('A', 6)

#define GPIO_DRV_GATE_EN		XGPIO_DEF2('B', 5)
#define GPIO_DRV_FAULT			XGPIO_DEF2('B', 7)

#define GPIO_SPI3_NSS			XGPIO_DEF2('C', 9)
#define GPIO_SPI3_SCK			XGPIO_DEF4('C', 10, 0, 6)
#define GPIO_SPI3_MISO			XGPIO_DEF4('C', 11, 0, 6)
#define GPIO_SPI3_MOSI			XGPIO_DEF4('C', 12, 0, 6)

#define GPIO_USART3_TX			XGPIO_DEF4('B', 10, 0, 7)
#define GPIO_USART3_RX			XGPIO_DEF4('B', 11, 0, 7)

#define GPIO_OTG_FS_DM			XGPIO_DEF4('A', 11, 0, 10)
#define GPIO_OTG_FS_DP			XGPIO_DEF4('A', 12, 0, 10)

#define GPIO_CAN_RX			XGPIO_DEF4('B', 8, 0, 9)
#define GPIO_CAN_TX			XGPIO_DEF4('B', 9, 0, 9)

#define GPIO_LED_ALERT			XGPIO_DEF2('B', 1)
#define GPIO_LED_MODE			XGPIO_DEF2('B', 0)

