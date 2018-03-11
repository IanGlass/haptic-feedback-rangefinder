/**************************************************************************//**
 * @file
 * @brief Empty Project
 * @author Energy Micro AS
 * @version 3.20.2
 ******************************************************************************
 * @section License
 * <b>(C) Copyright 2014 Silicon Labs, http://www.silabs.com</b>
 *******************************************************************************
 *
 * This file is licensed under the Silicon Labs Software License Agreement. See 
 * "http://developer.silabs.com/legal/version/v11/Silicon_Labs_Software_License_Agreement.txt"  
 * for details. Before using this software for any purpose, you must agree to the 
 * terms of that agreement.
 *
 ******************************************************************************/

#ifndef TRIG_PIN
#define TRIG_PIN     3
#endif
#ifndef TRIG_PORT
#define TRIG_PORT    gpioPortB
#endif
#ifndef ECHO_PIN
#define ECHO_PIN     4
#endif
#ifndef ECHO_PORT
#define ECHO_PORT    gpioPortB
#endif
#ifndef PWM_PIN
#define PWM_PIN     4
#endif
#ifndef PWM_PORT
#define PWM_PORT    gpioPortF
#endif
#ifndef BUTTON_PIN
#define BUTTON_PIN     13
#endif
#ifndef BUTTON_PORT
#define BUTTON_PORT    gpioPortC
#endif
#ifndef CHARGE_PIN
#define CHARGE_PIN     0
#endif
#ifndef CHARGE_PORT
#define CHARGE_PORT    gpioPortA
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "em_device.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_gpio.h"
#include "em_int.h"
#include "em_timer.h"
#include "em_prs.h"
#include "em_msc.h"
//
//
//****Notes for optimization****
//Spend time optimizing PWM freq for motor. Use seperate timer if max ticks are high to reduce cpu usage
//
//

//Optimal without signal cross-over
#define TRIGGER_CALL_FREQ 25
//Set to 40us high time
#define TRIGGER_HIGH_FREQ 25000
//PWM CALL frequency (100 times PWM frequency). Should be no greater than time to acquire 5 samples
#define PWM_FREQ 100000 //25000
//Systick call frequency
#define SYS_FREQ 10000
//Frequency for range indicator (set to 2 Hz atm)
#define RANGE_FREQ 200

volatile uint32_t Time;
volatile uint32_t PWMTime;
volatile uint32_t Debounce = 0;
volatile uint32_t Button_Enable = 1;
volatile uint32_t Sensor_On = 1;
volatile uint32_t Charging = 0;
int Distance[5];
int SampleCount = 0;
int PWM = 0;
//Range in cm, init
uint32_t Range = 100;
int Prev_State = 0;
int Indicator_Counter = 0;
int Indicator_Counter_Max = 0;
int Charge_Counter = 0;
int Charge_Indicated = 0;

uint32_t clock;

/**************************************************************************//**
 * @brief SysTick_Handler
 * Interrupt Service Routine for system tick counter, 10us calls
 *****************************************************************************/
void SysTick_Handler(void)
{
    Time++;
}

//PWM output ISR and debounce and range switch vibration control
void TIMER0_IRQHandler(void) {
	TIMER_IntClear(TIMER0, TIMER_IF_OF);
	PWMTime++;
	//Turn PWM off if timer < PWM variable
	if (PWMTime >= PWM) {
		GPIO_PinOutClear(PWM_PORT, PWM_PIN);
	}
	else {
		GPIO_PinOutSet(PWM_PORT, PWM_PIN);
	}
	if (PWMTime >= 100) {
		PWMTime = 0;
	}
	Debounce++;
	if (Debounce > 400) {
		Button_Enable = 1;
	}
	if (!Sensor_On) {
		//Set motor on time to 1/2 total time
		PWM = 50;
		//Change pwm timer freq to 5Hz
		TIMER_TopSet(TIMER0, CMU_ClockFreqGet(cmuClock_HFPER)/RANGE_FREQ);
		Indicator_Counter++;
		//If finished indicating range, turn everything back on
		if (Indicator_Counter/100 >= Indicator_Counter_Max) {
			//Reset PWM to normal frequency
			TIMER_TopSet(TIMER0, CMU_ClockFreqGet(cmuClock_HFPER)/PWM_FREQ);
			//Turn trigger timer back on
			TIMER_IntEnable(TIMER1, TIMER_IF_OF);
			Sensor_On = 1;
		}
	}
	if ((Charging) && (!Charge_Indicated)) {
		PWM = 50;
		//Change pwm timer freq to 5Hz
		TIMER_TopSet(TIMER0, CMU_ClockFreqGet(cmuClock_HFPER)/RANGE_FREQ);
		Charge_Counter++;
		if (Charge_Counter >= 100) {
			//Reset PWM to normal frequency
			TIMER_TopSet(TIMER0, CMU_ClockFreqGet(cmuClock_HFPER)/PWM_FREQ);
			Charge_Counter = 0;
			Charge_Indicated = 1;
			PWM = 0;
		}
	}
}

//Trigger signal out
void TIMER1_IRQHandler(void) {
	//Reset overflow flag
	TIMER_IntClear(TIMER1, TIMER_IF_OF);
	//Turn trigger on if off before
	if (GPIO_PinInGet(TRIG_PORT, TRIG_PIN) == 0) {
		GPIO_PinOutSet(TRIG_PORT, TRIG_PIN);
		//Set overflow value to trigger timer in 10us
		TIMER_TopSet(TIMER1, CMU_ClockFreqGet(cmuClock_HFPER)/TRIGGER_HIGH_FREQ/16);
	}
	//Switch back to regular trigger timing
	else {
		GPIO_PinOutClear(TRIG_PORT, TRIG_PIN);
		//Set overflow value to call timer at 25Hz
		TIMER_TopSet(TIMER1, CMU_ClockFreqGet(cmuClock_HFPER)/TRIGGER_CALL_FREQ);
	}
}

void GPIO_EVEN_IRQHandler(void) {
	if (GPIO_PinInGet(CHARGE_PORT, CHARGE_PIN) == 1) {
		Charging = 1;
	}
	if (GPIO_PinInGet(CHARGE_PORT, CHARGE_PIN) == 0) {
		Charging = 0;
		//Reset indicated charge when unplugged
		Charge_Indicated = 0;
	}
	//If echo high start timer
	if (GPIO_PinInGet(ECHO_PORT, ECHO_PIN) == 1) {
		Time = 0;
	}
	//If echo low stop timer
	else if (GPIO_PinInGet(ECHO_PORT, ECHO_PIN) == 0) {
		//Distance in cm, definitely works!
		Distance[SampleCount] = Time*340/(1000000/SYS_FREQ)/2;
		SampleCount++;
	}
	if ((SampleCount >= 4) && (!Charging)) {
		SampleCount = 0;
		//Needs to scale 30-90% as motor has min starting voltage. PWM and distance inversely proportional
		PWM = 100 - ((Distance[0] + Distance[1] + Distance[2] + Distance[3])/4*100/Range);
		//Can go -Ve if Distance>range
		if (PWM < 0) {
			PWM = 0;
		}
	}
	GPIO_IntClear(1 << ECHO_PIN);
	GPIO_IntClear(1 << CHARGE_PIN);
}

void GPIO_ODD_IRQHandler(void) {
	//Button
	if ((GPIO_PinInGet(BUTTON_PORT, BUTTON_PIN) == 1) && Button_Enable && !Prev_State && !Charging){
		Debounce = 0;
		Button_Enable = 0;
		//Switch Range
		if (Range == 100) {
			Range = 200;
			Indicator_Counter_Max = 2;
		}
		else if (Range == 200) {
			Range = 400;
			Indicator_Counter_Max = 4;
		}
		else {
			Range = 100;
			Indicator_Counter_Max = 1;
		}
		//Turn off trigger timer
		TIMER_IntDisable(TIMER1, TIMER_IF_OF);
		Indicator_Counter = 0;
		Sensor_On = 0;
		Prev_State = 1;
	}
	//Make sure button has been released before allowing another push
	else if ((GPIO_PinInGet(BUTTON_PORT, BUTTON_PIN) == 0) && Prev_State) {
		Prev_State = 0;
	}
	GPIO_IntClear(1 << BUTTON_PIN);
}

/**************************************************************************//**
 * @brief  Main function
 *****************************************************************************/
int main(void)
{
  /* Chip errata */
  CHIP_Init();

  CMU_ClockEnable(cmuClock_GPIO, true);

  /* Setup SysTick Timer for 100 usec interrupts  */
  if (SysTick_Config(CMU_ClockFreqGet(cmuClock_CORE)/SYS_FREQ)) while (1) ;

  clock = SystemCoreClockGet();

  //
  //Initialise echo pin, both edges
  GPIO_PinModeSet(ECHO_PORT, ECHO_PIN, gpioModeInput, 0);
  GPIO_IntConfig(ECHO_PORT, ECHO_PIN, true, true, true);
  //Initialise button pin, both edges
  GPIO_PinModeSet(BUTTON_PORT, BUTTON_PIN, gpioModeInputPull, 0);
  GPIO_IntConfig(BUTTON_PORT, BUTTON_PIN, true, true, true);
  //Initialise charge pin, both edges
  GPIO_PinModeSet(CHARGE_PORT, CHARGE_PIN, gpioModeInputPull, 0);
  GPIO_IntConfig(CHARGE_PORT, CHARGE_PIN, true, true, true);
  //Load to interrupt vector controller
  NVIC_EnableIRQ(GPIO_ODD_IRQn);
  NVIC_EnableIRQ(GPIO_EVEN_IRQn);
  //
  //

  //
  //Setup timer 1 on PC1 for sensor trigger signal
  //Enable clock for TIMER1 module
  CMU_ClockEnable(cmuClock_TIMER1, true);
  //Set CC1 location pin 1 (PC1) as output
  GPIO_PinModeSet(TRIG_PORT, TRIG_PIN, gpioModeWiredAndPullUp, 0);
  //Set overflow value
  TIMER_TopSet(TIMER1, CMU_ClockFreqGet(cmuClock_HFPER)/TRIGGER_CALL_FREQ);
  //Construct time initialization structure
  TIMER_Init_TypeDef timerInit1 =
  {		.enable = true,
		  .debugRun = true,
		  .prescale = timerPrescale16,
		  .clkSel = timerClkSelHFPerClk,
		  .fallAction = timerInputActionNone,
		  .riseAction = timerInputActionNone,
		  .mode = timerModeUp,
		  .dmaClrAct = false,
		  .quadModeX4 = false,
		  .oneShot = false,
		  .sync = false,
  };
  //Enable overflow interrupt
  TIMER_IntEnable(TIMER1, TIMER_IF_OF);
  //Enable TIMER1 interrupt vector in NVIC
  NVIC_EnableIRQ(TIMER1_IRQn);
  //Configure timer
  TIMER_Init(TIMER1, &timerInit1);
  //
  //

  //
  //Setup timer 0 on PD7 for sensor trigger signal
  //Enable clock for TIMER0 module
  CMU_ClockEnable(cmuClock_TIMER0, true);
  //Set CC1 location pin 7 (PD7) as output
  GPIO_PinModeSet(PWM_PORT, PWM_PIN, gpioModeWiredAndPullUp, 0);
  //Set overflow value
  TIMER_TopSet(TIMER0, CMU_ClockFreqGet(cmuClock_HFPER)/PWM_FREQ/1.6);
  //Construct time initialization structure
  TIMER_Init_TypeDef timerInit0 =
  {		.enable = true,
		  .debugRun = true,
		  .prescale = timerPrescale16,
		  .clkSel = timerClkSelHFPerClk,
		  .fallAction = timerInputActionNone,
		  .riseAction = timerInputActionNone,
		  .mode = timerModeUp,
		  .dmaClrAct = false,
		  .quadModeX4 = false,
		  .oneShot = false,
		  .sync = false,
  };
  //Enable overflow interrupt
  TIMER_IntEnable(TIMER0, TIMER_IF_OF);
  //Enable TIMER1 interrupt vector in NVIC
  NVIC_EnableIRQ(TIMER0_IRQn);
  //Configure timer
  TIMER_Init(TIMER0, &timerInit0);
  //
  //

  INT_Enable();

  //Check if charging on startup, ISR won't catch charging on startup
  if (GPIO_PinInGet(CHARGE_PORT, CHARGE_PIN) == 1	) {
	  Charging = 1;
  }

  /* Infinite loop */
  while (1) {

  }
}

