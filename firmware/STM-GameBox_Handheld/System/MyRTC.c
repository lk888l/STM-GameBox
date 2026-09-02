#include "stm32f10x.h"                  // Device header
#include <time.h>
#include "OLED.h"
#include "Key.h"
#include "Delay.h"
#include "Game_Dion.h"
#include "MyRTC.h"


//闹钟中断标志
uint8_t Alarm_Flag = 0;
uint8_t Alarm_Start = 0;

//倒计时
uint32_t Alarm_CNT,Alarm_Time,Alarm_Time_Rest;				//闹钟相关变量，单位都是秒
uint8_t Flag_Count, complete_flag = 0;						//是否在计时标志，0为不在计时
uint8_t KeyNumber;											//按键键码值

//秒表
uint32_t time_cnt, time_flag, Time_Alarm = 5;				//定义秒计数器数据类型
uint8_t stopwatch_flag;										//秒表标志位



void MyRTC_SetTime(void);				//函数声明

/**
  * 函    数：RTC初始化（内部低速晶振）
  * 参    数：无
  * 返 回 值：无
  */
void MyRTC_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE);
	
	PWR_BackupAccessCmd(ENABLE);
	
	if (BKP_ReadBackupRegister(BKP_DR1) != 0xA5A5)
	{
		RCC_LSICmd(ENABLE);
		while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) != SET);
		
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
		RCC_RTCCLKCmd(ENABLE);
		
		RTC_WaitForSynchro();
		RTC_WaitForLastTask();
		
		RTC_SetPrescaler(40000 - 1);
		RTC_WaitForLastTask();
		
		MyRTC_SetTime();
		
		BKP_WriteBackupRegister(BKP_DR1, 0xA5A5);
	}
	else
	{
		RCC_LSICmd(ENABLE);				//即使不是第一次配置，也需要再次开启LSI时钟
		while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) != SET);
		
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
		RCC_RTCCLKCmd(ENABLE);
		
		RTC_WaitForSynchro();
		RTC_WaitForLastTask();
	}
	RTC_NVIC_Config();
}

/** 
  * RTC闹钟中断设置
  * 包括两个：RTC中断和RTC闹钟中断
  * RTC中断优先级：2，1
  * 闹钟中断优先级：1，2
*/
/****************************************************************************************************************************************/
void RTC_NVIC_Config(void)
{ 
/*
	闹钟中断的优先级必须比秒中断高
	闹钟中断和秒中断几乎同时到来 秒中断的处理函数 是RTC_IRQHandler()
	如果进入这个函数 那么要想从RTC_IRQHandler()退出  则必须清除所有中断标志
    (包括闹钟中断)， 这样 闹钟中断标志被清除 则RTCAlarm_IRQHandler()函数肯定是进不去了
	如果不清楚闹钟中断标志 那么程序会死在RTC_IRQHandler()里边
	综上所述 那种中断必须能打断秒中断的执行 这样程序才能执行到RTCAlarm_IRQHandler()里边
*/
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = RTC_IRQn;  //RTC全局中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; 
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1; 
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;  //使能该通道中断
	NVIC_Init(&NVIC_InitStructure);  

	NVIC_InitStructure.NVIC_IRQChannel = RTCAlarm_IRQn;  //闹钟中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; //比RTC全局中断的优先级高
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2; 
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 
	NVIC_Init(&NVIC_InitStructure);
	
	EXTI_InitTypeDef EXTI_InitStructure;
	EXTI_ClearITPendingBit(EXTI_Line17);
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Line = EXTI_Line17;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
}


/**
  * 函    数：RTC设置时间
  * 参    数：无
  * 返 回 值：无
  * 说    明：调用此函数后，全局数组里时间值将刷新到RTC硬件电路
  */
void MyRTC_SetTime(void)
{
	time_t time_cnt;		//定义秒计数器数据类型
	struct tm time_date;	//定义日期时间数据类型
	
	time_date.tm_year = MyRTC_Time[0] - 1900;		//将数组的时间赋值给日期时间结构体
	time_date.tm_mon = MyRTC_Time[1] - 1;
	time_date.tm_mday = MyRTC_Time[2];
	time_date.tm_hour = MyRTC_Time[3];
	time_date.tm_min = MyRTC_Time[4];
	time_date.tm_sec = MyRTC_Time[5];
	
	time_cnt = mktime(&time_date) - 8 * 60 * 60;	//调用mktime函数，将日期时间转换为秒计数器格式
													//- 8 * 60 * 60为东八区的时区调整
	
	RTC_SetCounter(time_cnt);						//将秒计数器写入到RTC的CNT中
	RTC_WaitForLastTask();							//等待上一次操作完成
}

/**
  * 函    数：RTC读取时间
  * 参    数：无
  * 返 回 值：无
  * 说    明：调用此函数后，RTC硬件电路里时间值将刷新到全局数组
  */
void MyRTC_ReadTime(void)
{
	time_t time_cnt;		//定义秒计数器数据类型
	struct tm time_date;	//定义日期时间数据类型
	
	time_cnt = RTC_GetCounter() + 8 * 60 * 60;		//读取RTC的CNT，获取当前的秒计数器
													//+ 8 * 60 * 60为东八区的时区调整
	
	time_date = *localtime(&time_cnt);				//使用localtime函数，将秒计数器转换为日期时间格式
	
	MyRTC_Time[0] = time_date.tm_year + 1900;		//将日期时间结构体赋值给数组的时间
	MyRTC_Time[1] = time_date.tm_mon + 1;
	MyRTC_Time[2] = time_date.tm_mday;
	MyRTC_Time[3] = time_date.tm_hour;
	MyRTC_Time[4] = time_date.tm_min;
	MyRTC_Time[5] = time_date.tm_sec;
}

/**
  * 函    数：停机（休眠）模式下显示时间函数
  * 参    数：无
  * 返 回 值：无
  * 说    明：改变量Alarm = RTC_GetCounter() + "数字" 中的数字可改休眠显示时间的刷新时长（建议10s或者30s或者60s）
*/

void MyRTC_ShowTime(void)
{
	/*显示静态字符串*/
	OLED_Clear();
	OLED_ShowString(0, 56, "XXXX-XX-XX",OLED_6X8);
	OLED_ShowString(1, 18, "    XX:XX:XX",OLED_8X16);
	while(1)
	{		
		/*设定闹钟*/	
		uint32_t Alarm = RTC_GetCounter() + Time_Length;				//闹钟为唤醒后当前时间的后10s
		RTC_SetAlarm(Alarm-1);								//写入闹钟值到RTC的ALR寄存器
		
		/*显示相关参数*/
		MyRTC_ReadTime();
		
		OLED_ShowNum(0, 56, MyRTC_Time[0], 4,OLED_6X8);			//显示MyRTC_Time数组中的时间值，年
		OLED_ShowNum(30, 56, MyRTC_Time[1], 2,OLED_6X8);		//月
		OLED_ShowNum(49, 56, MyRTC_Time[2], 2,OLED_6X8);		//日
		OLED_ShowNum(33, 18, MyRTC_Time[3], 2,OLED_8X16);		//时
		OLED_ShowNum(57, 18, MyRTC_Time[4], 2,OLED_8X16);		//分
		OLED_ShowNum(81, 18, MyRTC_Time[5], 2,OLED_8X16);		//秒
		OLED_Update();
		
		PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFI);	//SIOP模式
		SystemInit();
		if(Key_Enter_Get() || Key_Back_Get())
		{
			return;
		}
	}
}


//倒计时功能
/******************************************************************************************************************************************/

void count_begin(void)
{
	int16_t Alarm_SetTime[] = {0,0,0};	
	uint8_t Alarm_MoveFlag = 2;
	OLED_Clear();
	OLED_ShowString(1, 6, "倒计时:  :  :  ",OLED_8X16);
	OLED_Update();
	while(1)
	{
		if(Flag_Count)														//正在计时，则显示闹钟响起剩余时间
		{
			Alarm_Time_Rest = Alarm_CNT-RTC_GetCounter()+1;					//计算闹钟响起剩余时间
			if(Alarm_Time_Rest > Alarm_Time)								//防止溢出错误
				Alarm_Time_Rest = 0;
			
			OLED_ShowNum(56,6,Alarm_Time_Rest/3600,2,OLED_8X16);			//显示剩余小时
			OLED_ShowNum(80,6,(Alarm_Time_Rest%3600)/60,2,OLED_8X16);		//显示剩余分钟
			OLED_ShowNum(104,6,(Alarm_Time_Rest%3600)%60,2,OLED_8X16);		//显示剩余秒
			
			if(Alarm_Flag)													//闹钟时间到，检查标志位为1
			{
				Alarm_Flag = 0;
				Flag_Count = 0;Alarm_Time = 0;								//重置相关参数	
				Alarm_SetTime[0] = 0,Alarm_SetTime[1] = 0,Alarm_SetTime[2] = 0;
				complete_flag = 1;
				OLED_ShowString(30,30,"Time Out",OLED_8X16);
				OLED_ShowNum(104,6,0,1,OLED_8X16);
				OLED_Update();
				TimerLength =500;											//定时器中断刷新时长
				Timer_500ms = 1;											//定时器中断500ms刷新一次	
				while(complete_flag)
				{
					if(RefreshFlag == 1)
					{
						GPIO_SetBits(GPIOB,GPIO_Pin_0);							//打开红色灯
						Buzzer_up();											//打开蜂鸣器
						RefreshFlag = 0;
					}
					GPIO_ResetBits(GPIOB,GPIO_Pin_0);
					KeyNumber = Get_LeftKeyNum();
					KeyNumber += Get_RighKeyNum();
					KeyNumber += Get_RollKeyNum();
					if(KeyNumber != 0)
					{
						complete_flag = 0;
						OLED_ClearArea(30,30,95,17);
						Timer_500ms = 0;
						
						break;						
					}
					if(Key_Enter_Get() || Key_Back_Get())
					{
						complete_flag = 0;
						OLED_ClearArea(30,30,95,17);
						Timer_500ms = 0;
						break;
					}
				}
				
			}
			else															//闹钟时间未到
			{
				OLED_ShowString(30,30,"Counting",OLED_8X16);				//显示正在计时
			}
		}
		else																//不在计时，则显示需要设定的闹钟时间
		{
			OLED_ShowNum(56,6,Alarm_SetTime[0],2,OLED_8X16);
			OLED_ShowNum(80,6,Alarm_SetTime[1],2,OLED_8X16);
			OLED_ShowNum(104,6,Alarm_SetTime[2],2,OLED_8X16);
			KeyNumber = Get_RollKeyNum();
			if(KeyNumber == 3)
			{
				Alarm_SetTime[Alarm_MoveFlag]++;
				if(Alarm_SetTime[Alarm_MoveFlag] > 60)
					Alarm_SetTime[Alarm_MoveFlag] = 0;
				else if(Alarm_SetTime[Alarm_MoveFlag] < 0)
					Alarm_SetTime[Alarm_MoveFlag] = 60;
			}
			if(KeyNumber == 4)
			{
				Alarm_SetTime[Alarm_MoveFlag]--;
				if(Alarm_SetTime[Alarm_MoveFlag] > 60)
					Alarm_SetTime[Alarm_MoveFlag] = 0;
				else if(Alarm_SetTime[Alarm_MoveFlag] < 0)
					Alarm_SetTime[Alarm_MoveFlag] = 60;
			}
			
			KeyNumber = Get_LeftKeyNum_click();
			if(KeyNumber == 1)
			{
				Alarm_MoveFlag++;
				if(Alarm_MoveFlag > 2)
					Alarm_MoveFlag = 0;
			}
			else if(KeyNumber == 2)
			{
				Alarm_MoveFlag--;
				if(Alarm_MoveFlag < 0)
					Alarm_MoveFlag = 2;
			}
			OLED_ReverseArea(56 + Alarm_MoveFlag*24,6,16,15);
			
			//右上角清空
			KeyNumber = Get_RighKeyNum();
			if(KeyNumber == 6)
			{
				RTC_ClearFlag(RTC_FLAG_ALR);									//清除标志位
				//重置相关参数
				Alarm_SetTime[0] = 0,Alarm_SetTime[1] = 0,Alarm_SetTime[2] = 0,complete_flag = 0;
				Flag_Count = 0,Alarm_Time = 0;
			}	
			
			//确定键开始
			if(Key_Enter_Get())
			{
				Alarm_Time = Alarm_SetTime[0]*3600 + Alarm_SetTime[1]*60 + Alarm_SetTime[2];							//单位是秒
				if(Alarm_Time > 0)
				{
					Alarm_Start = 1;
					Alarm_CNT = RTC_GetCounter()+Alarm_Time-1;					//设定闹钟值，需要-1
					RTC_SetAlarm(Alarm_CNT);									//写入闹钟值到RTC的ALR寄存器
					Flag_Count = 1;
				}
				else
				{
					OLED_ShowString(30,32,"Error !!!",OLED_8X16);
					OLED_UpdateArea(30,32,72,16);
					Delay_ms(1000);
					OLED_ClearArea(30,32,72,16);
				}
			}
			
		}
		OLED_Update();
		
		if(Key_Back_Get()) 													//退出功能
		{
			RTC_ClearFlag(RTC_FLAG_ALR);									//清除标志位
			//重置相关参数
			Alarm_SetTime[0] = 0,Alarm_SetTime[1] = 0,Alarm_SetTime[2] = 0,complete_flag = 0;						
			Flag_Count = 0,Alarm_Time = 0;				
			Alarm_Start = 0;
			return;
		}
			
	}
}


//秒表函数
/*********************************************************************************************************************************************/
void stopwatch()
{
	OLED_Clear();
	OLED_ShowString(1, 6, "time :  :  :  ",OLED_8X16);
	stopwatch_flag = 0;
	while(1)
	{
		if(stopwatch_flag == 0 && Get_RollKeyNum() == 4)
		{
			time_flag = 0;									//重置相关参数
		}
//		time_cnt = RTC_GetCounter();						//读取RTC的CNT，获取当前的秒计数器
		if(Alarm_Flag == 1)
		{
			time_flag++;
			Alarm_Flag = 0;
			RTC_SetAlarm(RTC_GetCounter());									//写入闹钟值到RTC的ALR寄存器
		}
			
		OLED_ShowNum(48,6,time_flag/3600,2,OLED_8X16);		//显示剩余小时
		OLED_ShowNum(72,6,(time_flag%3600)/60,2,OLED_8X16);	//显示剩余分钟
		OLED_ShowNum(96,6,(time_flag%3600)%60,2,OLED_8X16);	//显示剩余秒				
		OLED_Update();
		//Hour,Min,Sec;
		
		if(Key_Enter_Get())
		{
			if(stopwatch_flag == 0)		
			{
				stopwatch_flag = 1;
				Alarm_Start = 1;
				RTC_SetAlarm(RTC_GetCounter());									//写入闹钟值到RTC的ALR寄存器
				OLED_ShowString(30,30,"Counting",OLED_8X16);				//显示正在计时
			}
			else if(stopwatch_flag == 1)		
			{
				stopwatch_flag = 0;
				Alarm_Start = 0;
				Alarm_Flag = 0;
				RTC_ClearFlag(RTC_FLAG_ALR);									//清除标志位
				OLED_ShowString(30,30,"        ",OLED_8X16);				//显示正在计时
			}
		}
		if(Key_Back_Get()) 									//退出功能
		{
			Alarm_Start = 0;
			time_flag = 0;									//重置相关参数
			return;
		}
		OLED_Update();
	}
	
}



//RTC和闹钟中断实施函数
/****************************************************************************************************************************************/

void RTC_IRQHandler(void)
{
	if (RTC_GetITStatus(RTC_IT_SEC) != RESET)
    {
		
	}
    
    RTC_ClearITPendingBit(RTC_IT_SEC);
	RTC_WaitForLastTask();

}

void RTCAlarm_IRQHandler(void)
{     
	//秒表或倒计时开始时开启
	if(Alarm_Start)
	{			
		if(Alarm_Flag == 0)
		{
			Alarm_Flag = 1;				
		}
	}
	if(RTC_GetITStatus(RTC_IT_ALR) != RESET)
	{
		
		
	}
	EXTI_ClearITPendingBit(EXTI_Line17);
	RTC_WaitForLastTask();
	RTC_ClearITPendingBit(RTC_IT_ALR);
	RTC_WaitForLastTask();
 }
