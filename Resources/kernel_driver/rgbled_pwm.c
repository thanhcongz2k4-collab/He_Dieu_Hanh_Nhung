#include "rgbled_pwm.h"

// Độ phân giải mặc định của PWM
#define PERIOD  1000
#define PRESCALER 720

#define RGB_PORT    GPIOA

#define RED_PIN     GPIO_Pin_0
#define GREEN_PIN   GPIO_Pin_1
#define BLUE_PIN    GPIO_Pin_2


void RGBLed_PWM_Open(void)
{
  GPIO_InitTypeDef  				GPIO_InitStruct;
	TIM_TimeBaseInitTypeDef   TIM_InitStruct;
	TIM_OCInitTypeDef   			TIM_OC_InitStruct;
	
	// Bật clock GPIOA, AFIO và TIM2
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
	
	// Cấu hình chân PWM: AF push-pull
	GPIO_InitStruct.GPIO_Pin = RED_PIN | GREEN_PIN | BLUE_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_Init(RGB_PORT, &GPIO_InitStruct);
	
	// Time base cho TIM2
	TIM_InitStruct.TIM_Period = PERIOD-1;
	TIM_InitStruct.TIM_Prescaler = PRESCALER-1;
	TIM_InitStruct.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_InitStruct.TIM_ClockDivision = 0;
	TIM_InitStruct.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &TIM_InitStruct);
	
	// Cấu hình PWM mode 1 cho 3 kênh; duty ban đầu = 0
	TIM_OC_InitStruct.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OC_InitStruct.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OC_InitStruct.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OC_InitStruct.TIM_OutputNState = TIM_OutputNState_Disable;
	TIM_OC_InitStruct.TIM_Pulse = 0;
	TIM_OC1Init(TIM2, &TIM_OC_InitStruct);
	TIM_OC2Init(TIM2, &TIM_OC_InitStruct);
	TIM_OC3Init(TIM2, &TIM_OC_InitStruct);
	
	// Bật timer và output PWM
	TIM_Cmd(TIM2, ENABLE);
	TIM_CtrlPWMOutputs(TIM2, ENABLE);
}

void RGBLed_PWM_SetDuty(RGB_Led rgb_led)
{
	// Clamp giá trị duty để không vượt quá PERIOD
	rgb_led.red_value 	= (rgb_led.red_value > PERIOD) 		? 	PERIOD : rgb_led.red_value;
	rgb_led.green_value = (rgb_led.green_value > PERIOD) 	? 	PERIOD : rgb_led.green_value;
	rgb_led.blue_value 	= (rgb_led.blue_value > PERIOD) 	? 	PERIOD : rgb_led.blue_value;
	
	TIM2->CCR1 = rgb_led.blue_value;   // Kênh 1 . BLUE
	TIM2->CCR2 = rgb_led.green_value;  // Kênh 2 . GREEN
	TIM2->CCR3 = rgb_led.red_value;    // Kênh 3 . RED
}
