#include "stm32f10x.h"                  			// Device header
#include "Delay.h"
#include "Key.h"

uint8_t Key_Enter = 0;								//确认键
uint8_t Key_Back = 0;								//返回键
uint8_t Key_Up = 0;									//上
uint8_t Key_Down = 0;								//下
uint8_t LeftKey_flag = 0;							//按键状态（用于click模式下,左右按键）
uint8_t RollKey_flag = 0;							//按键状态（用于click模式下,上下按键）
uint8_t RighKey_flag = 0;							//按键状态（用于click模式下,右边按键）
uint16_t Buzzer_flag;								//蜂鸣器是否静音
uint16_t Timer_500ms = 0;							//俄罗斯方块定时器中断开启


void Key_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
	
	//pB  GPIO  输入
	GPIO_InitTypeDef GPIO_InitStructure_B;
	GPIO_InitStructure_B.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure_B.GPIO_Pin = Up_Pin | Down_Pin | Left_Pin | Right_Pin | Jump_Pin | Func_Pin | Enter_Pin |Back_Pin;
	GPIO_InitStructure_B.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure_B);
	
	//pB  GPIO  输出（推挽输出）(LED灯)
	GPIO_InitTypeDef GPIO_InitStructure_B_up;
	GPIO_InitStructure_B_up.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure_B_up.GPIO_Pin = GPIO_Pin_0 |GPIO_Pin_1 | GPIO_Pin_10 | GPIO_Pin_11;
	GPIO_InitStructure_B_up.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure_B_up);
	
	
	//pA  GPIO  输出（推挽输出）(蜂鸣器)
	GPIO_InitTypeDef GPIO_InitStructure_A_up;
	GPIO_InitStructure_A_up.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure_A_up.GPIO_Pin = GPIO_Pin_12;
	GPIO_InitStructure_A_up.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure_A_up);
	
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource14);
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource15);
//	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource13);
//	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource15);
	
	EXTI_InitTypeDef EXTI_InitStructure;
	EXTI_InitStructure.EXTI_Line = EXTI_Line14 | EXTI_Line15;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;		//Rising_Falling;
	EXTI_Init(&EXTI_InitStructure);
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	
	//GPIO_B(12,15)外部中断的定义
	NVIC_InitTypeDef NVIC_InitStructure_B;
	NVIC_InitStructure_B.NVIC_IRQChannel = EXTI15_10_IRQn;
	NVIC_InitStructure_B.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure_B.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure_B.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure_B);
	
	//GPIO_A(8,9)外部中断的定义
//	NVIC_InitTypeDef NVIC_InitStructure_A;
//	NVIC_InitStructure_A.NVIC_IRQChannel = EXTI9_5_IRQn;
//	NVIC_InitStructure_A.NVIC_IRQChannelCmd = ENABLE;
//	NVIC_InitStructure_A.NVIC_IRQChannelPreemptionPriority = 1;
//	NVIC_InitStructure_A.NVIC_IRQChannelSubPriority = 1;
//	NVIC_Init(&NVIC_InitStructure_A);
	
}

//左边左右方向按键
uint8_t Get_LeftKeyNum(void)
{
	uint8_t Left_KeyNum = 0;
	uint32_t Temp = 720000;	//临时用的计时变量
		
	//右方向键
	if (GPIO_ReadInputDataBit(Right_Port, Right_Pin) == 0)			//读输入寄存器的状态，如果为0，则代表按键1按下
	{							
		Delay_ms(5);												//延时消抖
		while (GPIO_ReadInputDataBit(Right_Port, Right_Pin) == 0)	//等待按键松手，或长按连续返回值
		{
			Temp--;
			if(Temp == 0)
			{
				return 1;
			}
		}		
		Left_KeyNum = 1;											//置键码为1
	}
	
	//左方向键
	if (GPIO_ReadInputDataBit(Left_Port, Left_Pin) == 0)			//读输入寄存器的状态，如果为0，则代表按键1按下
	{					
		Delay_ms(5);												//延时消抖
		while (GPIO_ReadInputDataBit(Left_Port, Left_Pin) == 0)		//等待按键松手，或长按连续返回值
		{
			Temp--;
			if(Temp == 0)
			{
				return 2;
			}
		}
		Left_KeyNum = 2;											//置键码为2
	}
	
	return Left_KeyNum;
}


//右边功能按键（左返回5，右返回6）
uint8_t Get_RighKeyNum(void)
{
	//uint8_t Righ_KeyNum = 0;									//返回值
	uint32_t Temp = 720000;										//临时用的计时变量
	
	//左上角键	（返回值为5）
	if (GPIO_ReadInputDataBit(Jump_Port, Jump_Pin) == 0)		//读输入寄存器的状态，如果为0，则代表按键1按下
	{					
		Delay_ms(5);											//延时消抖
		while (GPIO_ReadInputDataBit(Jump_Port, Jump_Pin) == 0)	//等待按键松手，或长按连续返回值
		{
			Temp--;
			if(Temp == 0)			
				return 5;			
		}
		return 5;												//置键码为5
	}
	
	//右上角键	返回值为（返回值为6）
	if (GPIO_ReadInputDataBit(Func_Port, Func_Pin) == 0)		//读输入寄存器的状态，如果为0，则代表按键1按下
	{					
		Delay_ms(5);											//延时消抖
		while (GPIO_ReadInputDataBit(Func_Port, Func_Pin) == 0)	//等待按键松手，或长按连续返回值
		{
			Temp--;
			if(Temp == 0)			
				return 6;			
		}
		return 6;												//置键码为6
	}
	return 0;
}

//左边上下滚动按键
uint8_t Get_RollKeyNum(void)
{
	uint8_t Roll_KeyNum = 0;
	uint32_t Temp = 720000;										//临时用的计时变量
		
	//上方向键
	if (GPIO_ReadInputDataBit(Up_Port, Up_Pin) == 0)			//读PB12输入寄存器的状态，如果为0，则代表按键1按下
	{
		Delay_ms(5);
		while (GPIO_ReadInputDataBit(Up_Port, Up_Pin) == 0)	//等待按键松手，或长按连续返回值
		{
			Temp--;
			if(Temp == 0)			
				return 3;	
		}
		Delay_ms(3);											//延时消抖
		Roll_KeyNum = 3;										//置键码为3
	}
	
	//下方向键
	if (GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0)			//读PB14输入寄存器的状态，如果为0，则代表按键1按下
	{					
		Delay_ms(5);											//延时消抖
		while (GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0)	//等待按键松手，或长按连续返回值
		{
			Temp--;
			if(Temp == 0)			
				return 4;	
		}
		Delay_ms(3);											//延时消抖
		Roll_KeyNum = 4;										//置键码为4
	}
	
	return Roll_KeyNum;
}

//click（单击）补丁函数
/*******************************************************************************************/
//左右键
uint8_t Get_LeftKeyNum_click(void)
{
	//uint8_t Left_KeyNum = 0;
		
	//右方向键
	if (GPIO_ReadInputDataBit(Right_Port, Right_Pin) == 0 && LeftKey_flag == 0)			//读PB15输入寄存器的状态，如果为0，则代表按键1按下
	{
		LeftKey_flag = 1;
		return 1;		
	}
	
	//左方向键
	if (GPIO_ReadInputDataBit(Left_Port, Left_Pin) == 0 && LeftKey_flag == 0)			//读PB13输入寄存器的状态，如果为0，则代表按键1按下
	{
		LeftKey_flag = 1;
		return 2;		
	}
	
	if((GPIO_ReadInputDataBit(Left_Port, Left_Pin) == 1 && GPIO_ReadInputDataBit(Right_Port, Right_Pin) == 1)&& LeftKey_flag == 1)
	{
		LeftKey_flag = 0;
	}
	
	return 0;
}

//上下键
uint8_t Get_RollKeyNum_click(void)
{	
	//上方向键
	if (GPIO_ReadInputDataBit(Up_Port, Up_Pin) == 0 && RollKey_flag == 0)			//读PB12输入寄存器的状态，如果为0，则代表按键1按下
	{
		RollKey_flag = 1;
		return 3;		
	}
	
	//下方向键
	if (GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0 && RollKey_flag == 0)			//读PB14输入寄存器的状态，如果为0，则代表按键1按下
	{
		RollKey_flag = 1;
		return 4;		
	}
	
	if((GPIO_ReadInputDataBit(Up_Port, Up_Pin) == 1 && GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 1)&& RollKey_flag == 1)
	{
		RollKey_flag = 0;
	}
	return 0;
}

//右边功能键
uint8_t Get_RighKeyNum_click()
{
		
	//左上角功能键
	if (GPIO_ReadInputDataBit(Jump_Port, Jump_Pin) == 0 && RighKey_flag == 0)			//读PB12输入寄存器的状态，如果为0，则代表按键1按下
	{
		RighKey_flag = 1;
		return 5;		
	}
	
	//右上角功能键
	if (GPIO_ReadInputDataBit(Func_Port, Func_Pin) == 0 && RighKey_flag == 0)			//读PB14输入寄存器的状态，如果为0，则代表按键1按下
	{
		RighKey_flag = 1;
		return 6;		
	}
	
	if((GPIO_ReadInputDataBit(Jump_Port, Jump_Pin) == 1 && GPIO_ReadInputDataBit(Func_Port, Func_Pin) == 1)&& RighKey_flag == 1)
	{
		RighKey_flag = 0;
	}
	return 0;
}

//俄罗斯方块（上下键）补丁函数
/***********************************************************************************************/
uint8_t Get_RollKeyNum_Tetris(void)
{
	uint32_t Temp = 300000;										//临时用的计时变量
	//下方向键
	if (GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0)			//读PB14输入寄存器的状态，如果为0，则代表按键1按下
	{					
		Delay_ms(5);											//延时消抖
		while (GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0)	//等待按键松手，或长按连续返回值
		{
			Temp -= 2;
			if(Temp == 0)			
				return 4;	
		}
		return 4;												//置键码为4
	}
	return 0;
}

//恐龙游戏补丁
/**********************************************************************************************************************/
uint8_t Get_DionKeyNum()
{
	//左上角键	（返回值为12）
	if(GPIO_ReadInputDataBit(Jump_Port, Jump_Pin) == 0) 
	{
		return 12;
	}
	return 0;
}


//遥控小车补丁函数
/************************************************************************************************************************/
//前后（上下）键
uint8_t Get_RollKeyNum_Car()
{
		
	//上方向键
	if (GPIO_ReadInputDataBit(Up_Port, Up_Pin) == 0)			//读PB12输入寄存器的状态，如果为0，则代表按键1按下
	{
		return 3;												//置键码为3
	}
	
	//下方向键
	if (GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0)			//读PB14输入寄存器的状态，如果为0，则代表按键1按下
	{					
		return 4;												//置键码为4
	}	
	return 0;
}
//左右键
uint8_t Get_LeftKeyNum_Car()
{
		
	//右方向键
	if (GPIO_ReadInputDataBit(Right_Port, Right_Pin) == 0)			//读PB15输入寄存器的状态，如果为0，则代表按键1按下
	{
		return 1;												//置键码为3
	}
	
	//左方向键
	if (GPIO_ReadInputDataBit(Left_Port, Left_Pin) == 0)			//读PB13输入寄存器的状态，如果为0，则代表按键1按下
	{					
		return 2;												//置键码为4
	}	
	return 0;
}
//双键函数
uint8_t Get_DoubleKeyNum_Car()
{
	//上键和右键
	if ((GPIO_ReadInputDataBit(Up_Port, Up_Pin) == 0) && (GPIO_ReadInputDataBit(Right_Port, Right_Pin) == 0))
	{
		return 10;												//置键码为3
	}
	//上键和左键
	else if ((GPIO_ReadInputDataBit(Up_Port, Up_Pin) == 0) && (GPIO_ReadInputDataBit(Left_Port, Left_Pin) == 0))
	{
		return 11;												//置键码为3
	}
	//下键和右键
	else if((GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0) && (GPIO_ReadInputDataBit(Right_Port, Right_Pin) == 0))
	{
		return 12;
	}
	//下键和左键
	else if((GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0) && (GPIO_ReadInputDataBit(Left_Port, Left_Pin) == 0))
	{
		return 13;
	}
	return 0;
}


//蜂鸣器功能函数
/**********************************************************************************************/
void Buzzer_up()
{
	if(Buzzer_flag == 0)	
	{
		Delay_ms(158);
		return;
	}
	for(u16 i=0;i<200;i++)
	{
		GPIO_WriteBit(GPIOA,GPIO_Pin_12,(BitAction)(1)); //蜂鸣器接口输出1		
		Delay_us(590);
		GPIO_WriteBit(GPIOA,GPIO_Pin_12,(BitAction)(0)); //蜂鸣器接口输出0
		Delay_us(200);
	}
}




//中断功能函数
/**********************************************************************************************/
int8_t Key_Enter_Get(void)	//确认键
{
	if(Key_Enter)
	{
		Key_Enter = 0;
		return 7;
	}
	return 0;
}

int8_t Key_Back_Get(void)	//返回键
{
	if(Key_Back)
	{
		Key_Back = 0;
		return 8;
	}
	return 0;
}

void Key_Reset_All(void)	//清除所有按键标志位
{
	Key_Enter = 0;
	Key_Back = 0;
}

void EXTI15_10_IRQHandler(void)
{
	if (EXTI_GetITStatus(EXTI_Line14) == SET) //确认键  PB14
	{
	/*如果出现数据乱跳的现象，可再次判断引脚电平，以避免抖动*/
		if (GPIO_ReadInputDataBit(Enter_Port, Enter_Pin) == 1)
		{
			Key_Enter += 1;
		}
		
		EXTI_ClearITPendingBit(EXTI_Line14);
	}
	
	if (EXTI_GetITStatus(EXTI_Line15) == SET) //返回键  pB15
	{
	/*如果出现数据乱跳的现象，可再次判断引脚电平，以避免抖动*/	
		if (GPIO_ReadInputDataBit(Back_Port, Back_Pin) == 1)
		{
			Key_Back += 1;
		}
		
		EXTI_ClearITPendingBit(EXTI_Line15);
	}
}
