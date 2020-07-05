#include <stddef.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "hal/hal.h"

#include "main.h"
#include "shell.h"

#define LOAD_COUNT_DELAY		((TickType_t) 100)

application_t			ap;
pmc_t 				pm	LD_CCMRAM;
tel_t				ti;

void xvprintf(io_ops_t *_io, const char *fmt, va_list ap);

void log_TRACE(const char *fmt, ...)
{
	va_list		ap;
	io_ops_t	ops = {

		.getc = NULL,
		.putc = &log_putc
	};

	xprintf(&ops, "[%i.%i] ", log.boot_COUNT, xTaskGetTickCount());

        va_start(ap, fmt);
	xvprintf(&ops, fmt, ap);
        va_end(ap);
}

void vAssertCalled(const char *file, int line)
{
	taskDISABLE_INTERRUPTS();
	log_TRACE("FreeRTOS: Assert %s:%i" EOL, file, line);

	hal_system_reset();
}

void vApplicationMallocFailedHook()
{
	taskDISABLE_INTERRUPTS();
	log_TRACE("FreeRTOS: Heap Allocation Failed" EOL);

	hal_system_reset();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	taskDISABLE_INTERRUPTS();
	log_TRACE("FreeRTOS: Stack Overflow in %8x task" EOL, (u32_t) xTask);

	hal_system_reset();
}

void task_TERM(void *pData)
{
	TickType_t		xWake;
	float			i_temp_PCB, i_temp_EXT;

	GPIO_set_mode_ANALOG(GPIO_ADC_PCB_NTC);
	GPIO_set_mode_ANALOG(GPIO_ADC_EXT_NTC);

	xWake = xTaskGetTickCount();

	i_temp_PCB = PM_MAX_F;
	i_temp_EXT = PM_MAX_F;

	do {
		/* 10 Hz.
		 * */
		vTaskDelayUntil(&xWake, (TickType_t) 100);

		ap.temp_PCB = ntc_temperature(&ap.ntc_PCB, ADC_get_VALUE(GPIO_ADC_PCB_NTC));
		ap.temp_EXT = ntc_temperature(&ap.ntc_EXT, ADC_get_VALUE(GPIO_ADC_EXT_NTC));
		ap.temp_INT = ADC_get_VALUE(GPIO_ADC_INTERNAL_TEMP);

		if (pm.lu_mode != PM_LU_DISABLED) {

			/* Derate current if PCB is overheat.
			 * */
			if (ap.temp_PCB > ap.heat_PCB) {

				i_temp_PCB = ap.heat_PCB_derated_1;
			}
			else if (ap.temp_PCB < (ap.heat_PCB - ap.heat_recovery_gap)) {

				i_temp_PCB = PM_MAX_F;
			}

			/* Derate current if EXT is overheat.
			 * */
			if (ap.temp_EXT > ap.heat_EXT) {

				i_temp_EXT = ap.heat_EXT_derated_1;
			}
			else if (ap.temp_EXT < (ap.heat_EXT - ap.heat_recovery_gap)) {

				i_temp_EXT = PM_MAX_F;
			}

			pm.i_derated_1 = (i_temp_PCB < i_temp_EXT) ? i_temp_PCB : i_temp_EXT;

			/* Enable FAN if PCB is overheat.
			 * */
			if (ap.temp_PCB > ap.heat_PCB_FAN) {

				GPIO_set_LOW(GPIO_FAN);
			}
			else if (ap.temp_PCB < (ap.heat_PCB_FAN - ap.heat_recovery_gap)) {

				GPIO_set_HIGH(GPIO_FAN);
			}
		}
	}
	while (1);
}

void task_ERROR(void *pData)
{
	TickType_t	xWake;
	int		LED = 0;

	xWake = xTaskGetTickCount();

	do {
		if (LED > 0) {

			/* NOTE: The number of LED flashes (~2 Hz) corresponds
			 * to the fail reason code.
			 * */

			if (LED & 1) {

				GPIO_set_HIGH(GPIO_LED);
			}
			else {
				GPIO_set_LOW(GPIO_LED);
			}

			vTaskDelayUntil(&xWake, (TickType_t) 200);

			LED += -1;
		}
		else {
			GPIO_set_LOW(GPIO_LED);

			vTaskDelayUntil(&xWake, (TickType_t) 1000);

			LED = (int) (2 * pm.fail_reason - 1);
		}
	}
	while (1);
}

float ADC_get_analog_ANG()
{
	float			analog;

	analog = ADC_get_VALUE(GPIO_ADC_ANALOG_ANG)
		* hal.ADC_reference_voltage / ap.analog_voltage_ratio;

	return analog;
}

float ADC_get_analog_BRK()
{
	float			analog;

	analog = ADC_get_VALUE(GPIO_ADC_ANALOG_BRK)
		* hal.ADC_reference_voltage / ap.analog_voltage_ratio;

	return analog;
}

void task_ANALOG(void *pData)
{
	TickType_t		xWake, xTime;
	float			analog, control, range, scaled;
	float			brake, brake_scaled;
	int			revol;

	GPIO_set_mode_ANALOG(GPIO_ADC_ANALOG_ANG);
	GPIO_set_mode_ANALOG(GPIO_ADC_ANALOG_BRK);

	xWake = xTaskGetTickCount();

	revol = pm.im_revol_total;
	xTime = (TickType_t) 0;

	do {
		/* 100 Hz.
		 * */
		vTaskDelayUntil(&xWake, (TickType_t) 10);

		if (ap.analog_ENABLED == PM_ENABLED) {

			analog = ADC_get_analog_ANG();
			brake = ADC_get_analog_BRK();

			if (		analog < ap.analog_voltage_lost[0]
					|| analog > ap.analog_voltage_lost[1]) {

				/* Loss of ANALOG signal.
				 * */

				if (ap.analog_locked == 1) {

					pm.fsm_req = PM_STATE_LU_SHUTDOWN;
					ap.analog_locked = 0;
				}
			}

			if (analog < ap.analog_voltage_ANG[1]) {

				range = ap.analog_voltage_ANG[0] - ap.analog_voltage_ANG[1];
				scaled = (ap.analog_voltage_ANG[1] - analog) / range;
			}
			else {
				range = ap.analog_voltage_ANG[2] - ap.analog_voltage_ANG[1];
				scaled = (analog - ap.analog_voltage_ANG[1]) / range;
			}

			scaled = (scaled < - 1.f) ? - 1.f :
				(scaled > 1.f) ? 1.f : scaled;

			if (		brake < ap.analog_voltage_lost[0]
					|| brake > ap.analog_voltage_lost[1]) {

				/* Loss of BRAKE signal.
				 * */

				brake_scaled = - 2.f;
			}
			else {
				if (brake < ap.analog_voltage_BRK[1]) {

					range = ap.analog_voltage_BRK[0] - ap.analog_voltage_BRK[1];
					brake_scaled = (ap.analog_voltage_BRK[1] - brake) / range;
				}
				else {
					range = ap.analog_voltage_BRK[2] - ap.analog_voltage_BRK[1];
					brake_scaled = (brake - ap.analog_voltage_BRK[1]) / range;
				}
			}

			if (brake_scaled > - 1.f && brake_scaled < 1.f) {

				/* BRAKE has a higher priority.
				 * */

				if (brake_scaled < 0.f) {

					range = ap.analog_control_BRK[1] - ap.analog_control_BRK[0];
					control = ap.analog_control_BRK[1] + range * brake_scaled;
				}
				else {
					range = ap.analog_control_BRK[2] - ap.analog_control_BRK[1];
					control = ap.analog_control_BRK[1] + range * brake_scaled;
				}
			}
			else {
				if (scaled < 0.f) {

					range = ap.analog_control_ANG[1] - ap.analog_control_ANG[0];
					control = ap.analog_control_ANG[1] + range * scaled;
				}
				else {
					range = ap.analog_control_ANG[2] - ap.analog_control_ANG[1];
					control = ap.analog_control_ANG[1] + range * scaled;
				}
			}

			reg_SET_F(ap.analog_reg_ID, control);

			if (pm.lu_mode == PM_LU_DISABLED) {

				if (		control > ap.analog_startup_range[0]
						&& control < ap.analog_startup_range[1]) {

					pm.fsm_req = PM_STATE_LU_STARTUP;
					ap.analog_locked = 1;
				}
			}
			else {
				/* Idle timeout.
				 * */

				if (ap.analog_locked == 1 && pm.im_revol_total == revol) {

					if (xTime > (TickType_t) (ap.analog_timeout * 1000.f)) {

						pm.fsm_req = PM_STATE_LU_SHUTDOWN;
						ap.analog_locked = 0;

						xTime = (TickType_t) 0;
					}
					else {
						xTime += (TickType_t) 10;
					}
				}
				else {
					revol = pm.im_revol_total;
					xTime = (TickType_t) 0;
				}
			}
		}
		else {
			/* Relax while analog is disabled.
			 * */
			vTaskDelayUntil(&xWake, (TickType_t) 100);
		}
	}
	while (1);
}

void ap_pushb_startup(char *s);

void task_INIT(void *pData)
{
	int			rc_flash;

	GPIO_set_mode_OUTPUT(GPIO_BOOST_12V);
	GPIO_set_HIGH(GPIO_BOOST_12V);

	GPIO_set_mode_OPEN_DRAIN(GPIO_FAN);
	GPIO_set_HIGH(GPIO_FAN);
	GPIO_set_mode_OUTPUT(GPIO_FAN);

	GPIO_set_mode_OUTPUT(GPIO_LED);
	GPIO_set_HIGH(GPIO_LED);

	ap.lc_flag = 1;
	ap.lc_tick = 0;

	vTaskDelay(LOAD_COUNT_DELAY);

	ap.lc_flag = 0;
	ap.lc_idle = ap.lc_tick;

	ap.io_USART.getc = &USART_getc;
	ap.io_USART.putc = &USART_putc;
	iodef = &ap.io_USART;

	if (log_bootup() != 0) {

		/* Slow down the startup to indicate a problem.
		 * */
		vTaskDelay((TickType_t) 1000);
	}

	rc_flash = flash_block_load();

	if (rc_flash == 0) {

		/* Resistor values in the voltage measurement circuits.
		 * */
		const float	vm_R1 = 470000.f;
		const float	vm_R2 = 27000.f;
		const float	vm_R3 = 470000.f;
		const float	vm_D = (vm_R1 * vm_R2 + vm_R2 * vm_R3 + vm_R1 * vm_R3);
		const float	ag_R1 = 10000.f;
		const float	ag_R2 = 10000.f;

		/* Default.
		 * */
		hal.USART_baud_rate = 57600;
		hal.PWM_frequency = 30000.f;
		hal.PWM_deadtime = 190.f;
		hal.ADC_reference_voltage = 3.3f;

#ifdef _HW_REV2

		hal.PWM_deadtime = 90.f;
		hal.ADC_shunt_resistance = 0.5E-3f;
		hal.ADC_amplifier_gain = 60.f;
		hal.ADC_voltage_ratio = vm_R2 / (vm_R1 + vm_R2);
		hal.ADC_terminal_ratio = vm_R2 / (vm_R1 + vm_R2);
		hal.ADC_terminal_bias = 0.f;

#endif /* _HW_REV2 */

#ifdef _HW_REV4B

		hal.ADC_shunt_resistance = 0.5E-3f;
		hal.ADC_amplifier_gain = 60.f;
		hal.ADC_voltage_ratio = vm_R2 / (vm_R1 + vm_R2);
		hal.ADC_terminal_ratio = vm_R2 / (vm_R1 + vm_R2);
		hal.ADC_terminal_bias = 0.f;

#endif /* _HW_REV4B */

#ifdef _HW_REV4C

		hal.ADC_shunt_resistance = 0.5E-3f;
		hal.ADC_amplifier_gain = 20.f;
		hal.ADC_voltage_ratio = vm_R2 / (vm_R1 + vm_R2);
		hal.ADC_terminal_ratio = vm_R2 * vm_R3 / vm_D;
		hal.ADC_terminal_bias = vm_R1 * vm_R2 * hal.ADC_reference_voltage / vm_D;

#endif /* _HW_REV4C */

		hal.TIM_mode = TIM_DISABLED;

		hal.PPM_mode = PPM_DISABLED;
		hal.PPM_timebase = 2000000UL;

		ap.ppm_reg_ID = ID_PM_S_SETPOINT_PC;
		ap.ppm_pulse_range[0] = 1000.f;
		ap.ppm_pulse_range[1] = 1500.f;
		ap.ppm_pulse_range[2] = 2000.f;
		ap.ppm_pulse_lost[0] = 800.f;
		ap.ppm_pulse_lost[1] = 2200.f;
		ap.ppm_control_range[0] = 0.f;
		ap.ppm_control_range[1] = 50.f;
		ap.ppm_control_range[2] = 100.f;
		ap.ppm_startup_range[0] = 0.f;
		ap.ppm_startup_range[1] = 5.f;

		ap.step_reg_ID = ID_PM_X_SETPOINT_F_MM;
		ap.step_const_ld_EP = 0.f;

		ap.analog_ENABLED = PM_DISABLED;
		ap.analog_reg_ID = ID_PM_I_SETPOINT_Q_PC;
		ap.analog_voltage_ratio = ag_R2 / (ag_R1 + ag_R2);
		ap.analog_timeout = 5.f;
		ap.analog_voltage_ANG[0] = 1.0f;
		ap.analog_voltage_ANG[1] = 2.5f;
		ap.analog_voltage_ANG[2] = 4.0f;
		ap.analog_voltage_BRK[0] = 1.0f;
		ap.analog_voltage_BRK[1] = 4.0f;
		ap.analog_voltage_BRK[2] = 4.1f;
		ap.analog_voltage_lost[0] = 0.2f;
		ap.analog_voltage_lost[1] = 4.8f;
		ap.analog_control_ANG[0] = 0.f;
		ap.analog_control_ANG[1] = 50.f;
		ap.analog_control_ANG[2] = 100.f;
		ap.analog_control_BRK[0] = 0.f;
		ap.analog_control_BRK[1] = - 100.f;
		ap.analog_control_BRK[2] = - 100.f;
		ap.analog_startup_range[0] = 0.f;
		ap.analog_startup_range[1] = 20.f;

		ap.ntc_PCB.r_balance = 10000.f;
		ap.ntc_PCB.r_ntc_0 = 10000.f;
		ap.ntc_PCB.ta_0 = 25.f;
		ap.ntc_PCB.betta = 3435.f;

		ap.ntc_EXT.r_balance = 10000.f;
		ap.ntc_EXT.r_ntc_0 = 10000.f;
		ap.ntc_EXT.ta_0 = 25.f;
		ap.ntc_EXT.betta = 3380.f;

		ap.heat_PCB = 90.f;
		ap.heat_PCB_derated_1 = 20.f;
		ap.heat_EXT = 90.f;
		ap.heat_EXT_derated_1 = 20.f;
		ap.heat_PCB_FAN = 60.f;
		ap.heat_recovery_gap = 5.f;

		ap.hx711_gain[0] = 0.f;
		ap.hx711_gain[1] = 4.545E-6f;

		ap.servo_span_mm[0] = - 25.f;
		ap.servo_span_mm[1] = 25.f;
		ap.servo_uniform_mmps = 20.f;
		ap.servo_mice_role = 0;

		ap.FT_grab_hz = 200;
	}

	USART_startup();
	ADC_startup();
	PWM_startup();

	pm.freq_hz = hal.PWM_frequency;
	pm.dT = 1.f / pm.freq_hz;
	pm.dc_resolution = hal.PWM_resolution;
	pm.proc_set_DC = &PWM_set_DC;
	pm.proc_set_Z = &PWM_set_Z;

	if (rc_flash == 0) {

		/* Default.
		 * */
		pm_default(&pm);
		tel_reg_default(&ti);

		reg_SET_F(ID_PM_FAULT_CURRENT_HALT, 0.f);
	}

	if (hal.PPM_mode != PPM_DISABLED) {

		PPM_startup();
	}

	TIM_startup();
	WD_startup();

	ADC_irq_unlock();
	GPIO_set_LOW(GPIO_LED);

	pm.fsm_req = PM_STATE_ZERO_DRIFT;

	xTaskCreate(task_TERM, "TERM", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
	xTaskCreate(task_ERROR, "ERROR", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate(task_ANALOG, "ANALOG", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
	xTaskCreate(task_SH, "SH", 400, NULL, 1, NULL);

	ap_pushb_startup(EOL);

	vTaskDelete(NULL);
}

static void
input_PULSE_WIDTH()
{
	float		pulse, control, range, scaled;

	if (hal.PPM_signal_caught != 0) {

		pulse = PPM_get_PULSE();

		if (pulse != ap.ppm_pulse_cached) {

			ap.ppm_pulse_cached = pulse;

			if (pulse < ap.ppm_pulse_lost[0] || pulse > ap.ppm_pulse_lost[1]) {

				/* Loss of signal.
				 * */

				if (ap.ppm_locked == 1) {

					pm.fsm_req = PM_STATE_LU_SHUTDOWN;
					ap.ppm_locked = 0;
				}
			}

			if (pulse < ap.ppm_pulse_range[1]) {

				range = ap.ppm_pulse_range[0] - ap.ppm_pulse_range[1];
				scaled = (ap.ppm_pulse_range[1] - pulse) / range;
			}
			else {
				range = ap.ppm_pulse_range[2] - ap.ppm_pulse_range[1];
				scaled = (pulse - ap.ppm_pulse_range[1]) / range;
			}

			scaled = (scaled < - 1.f) ? - 1.f :
				(scaled > 1.f) ? 1.f : scaled;

			if (scaled < 0.f) {

				range = ap.ppm_control_range[1] - ap.ppm_control_range[0];
				control = ap.ppm_control_range[1] + range * scaled;
			}
			else {
				range = ap.ppm_control_range[2] - ap.ppm_control_range[1];
				control = ap.ppm_control_range[1] + range * scaled;
			}

			reg_SET_F(ap.ppm_reg_ID, control);

			if (pm.lu_mode == PM_LU_DISABLED) {

				if (		control > ap.ppm_startup_range[0]
						&& control < ap.ppm_startup_range[1]) {

					pm.fsm_req = PM_STATE_LU_STARTUP;
					ap.ppm_locked = 1;
				}
			}
		}
	}
	else {
		if (ap.ppm_locked == 1) {

			pm.fsm_req = PM_STATE_LU_SHUTDOWN;
			ap.ppm_locked = 0;
		}
	}
}

static void
input_STEP_DIR()
{
	int		EP, relEP;
	float		xSP, wSP;

	EP = PPM_get_STEP_DIR();

	relEP = (short int) (EP - ap.step_baseEP);
	ap.step_baseEP = EP;

	if (		pm.lu_mode == PM_LU_DISABLED
			&& ap.step_locked == 0) {

		pm.fsm_req = PM_STATE_LU_STARTUP;
		ap.ppm_locked = 1;
	}

	if (relEP != 0) {

		ap.step_accuEP += relEP;
		xSP = ap.step_accuEP * ap.step_const_ld_EP;

		reg_SET_F(ap.step_reg_ID, xSP);
	}

	if (ap.step_reg_ID == ID_PM_X_SETPOINT_F_MM) {

		/* FIXME */

		wSP = relEP * pm.freq_hz * ap.step_const_ld_EP;

		reg_SET_F(ID_PM_X_SETPOINT_WS_MMPS, wSP);
	}
}

static void
input_CONTROL_QENC()
{
	/* TODO */
}

void ADC_IRQ()
{
	pmfb_t		fb;

	fb.current_A = hal.ADC_current_A;
	fb.current_B = hal.ADC_current_B;
	fb.voltage_U = hal.ADC_voltage_U;

	fb.voltage_A = hal.ADC_voltage_A;
	fb.voltage_B = hal.ADC_voltage_B;
	fb.voltage_C = hal.ADC_voltage_C;

	if (hal.TIM_mode == TIM_DRIVE_HALL) {

		fb.pulse_HS = GPIO_get_HALL();
		fb.pulse_EP = 0;
	}
	else if (hal.TIM_mode == TIM_DRIVE_QENC) {

		fb.pulse_HS = 0;
		fb.pulse_EP = TIM_get_EP();
	}
	else {
		fb.pulse_HS = 0;
		fb.pulse_EP = 0;
	}

	if (hal.PPM_mode == PPM_PULSE_WIDTH) {

		input_PULSE_WIDTH();
	}
	else if (hal.PPM_mode == PPM_STEP_DIR) {

		input_STEP_DIR();
	}
	else if (hal.PPM_mode == PPM_CONTROL_QENC) {

		input_CONTROL_QENC();
	}

	pm_feedback(&pm, &fb);
	tel_reg_grab(&ti);

	WD_kick();
}

void app_MAIN()
{
	xTaskCreate(task_INIT, "INIT", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
	vTaskStartScheduler();
}

SH_DEF(rtos_uptime)
{
	TickType_t	xTick;
	int		Day, Hour, Min, Sec;

	xTick = xTaskGetTickCount();

	Sec = xTick / configTICK_RATE_HZ;
	Day = Sec / 86400;
	Sec -= Day * 86400;
	Hour = Sec / 3600;
	Sec -= Hour * 3600;
	Min = Sec / 60;
	Sec -= Min * 60;

	printf("[%i] %id %ih %im %is" EOL, log.boot_COUNT, Day, Hour, Min, Sec);
}

void vApplicationIdleHook()
{
	if (ap.lc_flag != 0) {

		ap.lc_tick++;
		hal_fence();
	}
	else {

		hal_sleep();
	}
}

SH_DEF(rtos_cpu_usage)
{
	float		pc;

	ap.lc_flag = 1;
	ap.lc_tick = 0;

	vTaskDelay(LOAD_COUNT_DELAY);

	ap.lc_flag = 0;

	pc = 100.f * (float) (ap.lc_idle - ap.lc_tick)
		/ (float) ap.lc_idle;

	printf("%1f (%%)" EOL, &pc);
}

SH_DEF(rtos_task_list)
{
	TaskStatus_t		*pLIST;
	int			xSIZE, xState, N;

	xSIZE = uxTaskGetNumberOfTasks();
	pLIST = pvPortMalloc(xSIZE * sizeof(TaskStatus_t));

	if (pLIST != NULL) {

		xSIZE = uxTaskGetSystemState(pLIST, xSIZE, NULL);

		printf("TCB ID Name Stat Prio Stack Free" EOL);

		for (N = 0; N < xSIZE; ++N) {

			switch (pLIST[N].eCurrentState) {

				case eRunning:
					xState = 'R';
					break;

				case eReady:
					xState = 'E';
					break;

				case eBlocked:
					xState = 'B';
					break;

				case eSuspended:
					xState = 'S';
					break;

				case eDeleted:
					xState = 'D';
					break;

				case eInvalid:
				default:
					xState = 'N';
					break;
			}

			printf("%8x %i %s %c %i %8x %i" EOL,
					(u32_t) pLIST[N].xHandle,
					(int) pLIST[N].xTaskNumber,
					(const char *) pLIST[N].pcTaskName,
					(int) xState,
					(int) pLIST[N].uxCurrentPriority,
					(u32_t) pLIST[N].pxStackBase,
					(int) pLIST[N].usStackHighWaterMark);
		}

		vPortFree(pLIST);
	}
}

SH_DEF(rtos_task_kill)
{
	TaskHandle_t		xHandle;

	xHandle = xTaskGetHandle(s);

	if (xHandle != NULL) {

		vTaskDelete(xHandle);
	}
}

SH_DEF(rtos_freeheap)
{
	printf("FreeHeap %i" EOL, xPortGetFreeHeapSize());
	printf("MinimumEver %i" EOL, xPortGetMinimumEverFreeHeapSize());
}

SH_DEF(rtos_log_flush)
{
	if (log.textbuf[0] != 0) {

		puts(log.textbuf);
		puts(EOL);
	}
}

SH_DEF(rtos_log_cleanup)
{
	if (log.textbuf[0] != 0) {

		memset(log.textbuf, 0, sizeof(log.textbuf));

		log.tail = 0;
	}
}

SH_DEF(rtos_reboot)
{
	if (pm.lu_mode != PM_LU_DISABLED) {

		printf("Unable when PM is running" EOL);
		return ;
	}

	GPIO_set_LOW(GPIO_BOOST_12V);
	vTaskDelay((TickType_t) 10);

	hal_system_reset();
}

SH_DEF(rtos_bootload)
{
	if (pm.lu_mode != PM_LU_DISABLED) {

		printf("Unable when PM is running" EOL);
		return ;
	}

	GPIO_set_LOW(GPIO_BOOST_12V);
	vTaskDelay((TickType_t) 10);

	hal_bootload_jump();
}

