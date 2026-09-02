#include "stm32f10x.h"                  // Device header
#include <stdlib.h>
#include "Delay.h"
#include "OLED.h"
#include "Key.h"
#include "Myflash.h"

//鍒嗘暟
uint8_t Grade_Count = 0;
uint16_t Grade = 0;
uint16_t Grade_end;
uint16_t DionGrade_max;
//Cloud
const uint8_t Cloud_Length = 27;
int8_t Cloud_Positon_1 = 100;
int8_t Cloud_Positon_2 = 0;
//Dino
uint8_t Height = 0;
uint8_t Dino_Flag = 0;
uint8_t Dino_Jump_Key = 0;
uint8_t Dino_Jump_Flag = 0;
uint8_t Dino_Jump_Flag_Flag = 0;
uint8_t Dino_Count = 0;
uint8_t Jump_FinishFlag = 0;
//仙人掌
uint16_t Cactus_CreatTime = 3000;
uint16_t Cactus_CreatTime_Multiplier = 1000;
uint8_t Cactus_CreatNumber = 0;
const int8_t Cactus_Length1 = 8;
int8_t Cactus_Position1 = 127;
uint8_t Cactus_Flag1 = 1;
const int8_t Cactus_Length2 = 16;
int8_t Cactus_Position2 = 127;
uint8_t Cactus_Flag2 = 1;
const int8_t Cactus_Length3 = 16;
int8_t Cactus_Position3 = 127;
uint8_t Cactus_Flag3 = 1;
uint16_t Cactus_Count = 0;

//Key
uint8_t dino_Key_Slow = 0;
uint8_t dino_Key_Value = 0;
uint8_t dino_Key_Old = 0;
uint8_t dino_Key_Down = 0;
uint8_t dino_Key_Test = 0;

//Ground
uint8_t OLED_Slow = 0;
//uint16_t Ground_Move_Number = 0;
uint8_t Speed = 3;
uint32_t pos = 0;





void Key_Proc(void)
{
	if(dino_Key_Slow) return;
	dino_Key_Slow = 1;
	
	dino_Key_Value = Get_DionKeyNum();
	dino_Key_Down = dino_Key_Value & (dino_Key_Value ^ dino_Key_Old);
	dino_Key_Old = dino_Key_Value;
	
	if(dino_Key_Down == 12 && Dino_Jump_Key == 0)
	{
		Dino_Jump_Key = 1;
	}
}

void LED_Proc(void)
{
	if(OLED_Slow) return;
	OLED_Slow = 1;
	//显示云
		//cloud_pos-=speed;
		//if(cloud_pos < 1) cloud_pos = 63;

		//显示分数
		OLED_ShowNum(97,0,Grade,5,OLED_6X8);
		OLED_UpdateArea(97,0,30,8);
		//显地面
		OLED_ShowImage(0,54,128,8,&groud[pos%500]);
		OLED_UpdateArea(0,54,128,8);
		//OLED_Update();
		pos+=Speed;
		//显示在该偏移量下的地面

		
		if(pos>500)	pos = 0;
		//云1
		OLED_ClearPicture(Cloud_Positon_1 + 1, 3, Cloud_Positon_1 + Cloud_Length - 1, 3);					//消影
		Cloud_Positon_1 -= (Speed - 1);																														//改变云朵1的位置
		if(Cloud_Positon_1 < -27)																																	//如果超出屏幕左侧，则回到屏幕右侧
			Cloud_Positon_1 = 127;																																	//
		OLED_ShowCloud(Cloud_Positon_1, 3, Cloud_Positon_1 + Cloud_Length - 1, 3);								//显示云朵1

		//云2
		OLED_ClearPicture(Cloud_Positon_2 + 1, 2, Cloud_Positon_2 + Cloud_Length - 1, 2);					//消影
		Cloud_Positon_2 -= (Speed - 1);																														//改变云朵2的位置
		if(Cloud_Positon_2 < -27)																																	//如果超出屏幕左侧，则回到屏幕右侧
			Cloud_Positon_2 = 127;																																	//
		OLED_ShowCloud(Cloud_Positon_2, 2, Cloud_Positon_2 + Cloud_Length - 1, 2);								//显示云朵2
		//小恐龙
		if(Dino_Jump_Key == 0)
		{
			OELD_ShowDino(0, 5, 15, 6, Dino_Flag);																									//显示恐龙奔跑的画面
		}
		else
		{
			if(Jump_FinishFlag == 1)
				Jump_FinishFlag = 2;
			OELD_ShowDino_Jump(0, 2, 15, 6, Dino_Jump_Flag);																				//显示恐龙跳起的画面
			if(Jump_FinishFlag == 2 && Dino_Jump_Flag == 0)
			{
				Jump_FinishFlag = 0;
				Dino_Jump_Key = 0;
			}
		}
		
			//仙人掌1
		if(Cactus_Flag1 == 0)																																			//生产仙人掌1的标志,若Cactus_Flag1 = 0,则生成仙人掌1
		{
			OLED_ClearPicture(Cactus_Position1 + 3, 5, Cactus_Position1 + Cactus_Length1 - 1, 6);		//消影
			Cactus_Position1 -= Speed;																															//向左移动
			if(Cactus_Position1 < -8)																																//如果超出屏幕左侧,则回到屏幕右侧,且静止
			{																																												//不动，不从屏幕右侧出现
				Cactus_Flag1 = 1;																																			//
				Cactus_Position1 = 127;																																//
			}																																												//
			OLED_ShowCactus1(Cactus_Position1, 5, Cactus_Position1 + Cactus_Length1 - 1, 6);				//显示仙人掌1
		
		}
	
		//仙人掌2
		if(Cactus_Flag2 == 0)																																			//生产仙人掌2的标志,若Cactus_Flag2 = 0,则生成仙人掌2
		{
			OLED_ClearPicture(Cactus_Position2 + 8, 5, Cactus_Position2 + Cactus_Length2 - 1, 6);		//消影
			Cactus_Position2 -= Speed;																															//向左移动
			if(Cactus_Position2 < -16)																															//如果超出屏幕左侧,则回到屏幕右侧,且静止
			{																																												//不动，不从屏幕右侧出现
				Cactus_Flag2 = 1;																																			//
				Cactus_Position2 = 127;																																//
			}																																												//
			OLED_ShowCactus2(Cactus_Position2, 5, Cactus_Position2 + Cactus_Length2 - 1, 6);				//显示仙人掌2
		}
	
		//仙人掌3
		if(Cactus_Flag3 == 0)																																			//生产仙人掌3的标志,若Cactus_Flag3 = 0,则生成仙人掌3
		{
			OLED_ClearPicture(Cactus_Position3 + 8, 6, Cactus_Position3 + Cactus_Length3 - 1, 6);		//消影
			Cactus_Position3 -= Speed;																															//向左移动
			if(Cactus_Position3 < -16)																															//如果超出屏幕左侧,则回到屏幕右侧,且静止
			{																																												//不动，不从屏幕右侧出现
				Cactus_Flag3 = 1;																																			//
				Cactus_Position3 = 127;																																//
			}																																												//
			OLED_ShowCactus3(Cactus_Position3, 6, Cactus_Position3 + Cactus_Length3 - 1, 6);				//显示仙人掌3
		}
}

void Dion_Play(void)
{
	OLED_Clear();
	OLED_Update();
	
	//生成仙人掌的变量重置
	Cactus_Count = 0;
	Cactus_CreatTime = 3000;
	Cactus_CreatTime_Multiplier = 1000;
	Cactus_CreatNumber = 0;
	Cactus_Position1 = 127;
	Cactus_Flag1 = 1;
	Cactus_Position2 = 127;
	Cactus_Flag2 = 1;
	Cactus_Position3 = 127;
	Cactus_Flag3 = 1;
	
	//加速变量重置
	Grade_Count =0;
	Grade = 0;
	Speed = 3;
	
	while(1)
	{
		
		Key_Proc();
		LED_Proc();
		
		//Game Over
		if(Cactus_Position3 + Cactus_Length3 - 1 <= 26 && Cactus_Position3 + Cactus_Length3 - 1 >= 0 && Height <= 6 || 		//判定是否触碰
				Cactus_Position2 + Cactus_Length2 - 1 <= 26 && Cactus_Position2 + Cactus_Length2 - 1 >= 0 && Height <= 14 || 	//到仙人掌
				Cactus_Position1 + Cactus_Length1 - 1 <= 24 && Cactus_Position1 + Cactus_Length1 - 1 >= 0 && Height <= 14)		//
		{
			Grade_end = Grade;
			if(Grade_end>Store_Data[4])
			{
				Store_Data[4] = Grade_end;						//利用flash记录最高分数
				Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
			}
			while(1)																																								//游戏结束
			{																																												//
				Delay_ms(200);
				
				//退出界面显示
				OLED_Clear();
				OLED_ShowImage(0,0,60,55,bmp_dog);
				//请按双键
				OLED_Printf(61, 0, OLED_8X16, "游戏结束");
				OLED_Printf(61, 20, OLED_8X16, "得分: ");
				OLED_Printf(61, 40, OLED_8X16, "最高: ");
				OLED_ShowNum(95,26,Grade_end,5,OLED_6X8);
				OLED_ShowNum(95,46,Store_Data[4],5,OLED_6X8);
				OLED_Update();
				
				if(Key_Enter_Get()) {break;}		//退出游戏
				if(Key_Back_Get()) {break;}		//退出游戏				
			}		
			return;
		}

		if(Key_Enter_Get()) {return;}		//退出游戏
		if(Key_Back_Get()) {return;}		//退出游戏
	}
}

//定时器中断功能函数
/******************************************************************************************************************************/
uint16_t TimerCount = 0;					//计时500ms
uint16_t RefreshFlag = 0;					//俄罗斯方块是否刷新
uint16_t TimerLength = 500;					//时长（默认500）

void TIM2_IRQHandler(void)
{
	
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{		
		if(Timer_500ms == 1)
		{
			TimerCount++;
			if(TimerCount >= TimerLength)
			{
				TimerCount = 0;
				RefreshFlag = 1;				
			}		
		}
		
		if(++OLED_Slow == 35) 	OLED_Slow = 0;
		if(++dino_Key_Slow == 2) dino_Key_Slow = 0;
		
		//处理小恐龙的奔跑跳跃
		Dino_Count++;
		if(Dino_Count == 50)
		{
			Dino_Flag ^= 1;
			
			if(Dino_Jump_Key == 1)
			{
				if(Dino_Jump_Flag_Flag == 0 && Jump_FinishFlag == 0)
				{
					Dino_Jump_Flag ++;
					if(Dino_Jump_Flag == 8)
						Dino_Jump_Flag_Flag = 1;
				}
				else if(Dino_Jump_Flag_Flag == 1)
				{
					Dino_Jump_Flag --;
					if(Dino_Jump_Flag == 0)
					{
						Dino_Jump_Flag_Flag = 0;
						Jump_FinishFlag = 1;
					}
				}
			}
			
			switch(Dino_Jump_Flag)
			{
				case 0:Height = 0; break;
				case 1:Height = 6; break;
				case 2:Height = 10;break;
				case 3:Height = 15;break;
				case 4:Height = 18;break;
				case 5:Height = 21;break;
				case 6:Height = 23;break;
				case 7:Height = 25;break;
				case 8:Height = 25;break;
			}
			
			Dino_Count = 0;
		}
		
		//随机生成仙人掌
		Cactus_Count++;
		if(Cactus_Count >= Cactus_CreatTime)
		{
			
			Cactus_CreatTime = rand() % 3;
			Cactus_CreatTime += 1;
			Cactus_CreatTime *= Cactus_CreatTime_Multiplier;
			
			srand(rand());
			Cactus_CreatNumber = rand() % 3;
			switch(Cactus_CreatNumber)
			{
				case 0:
					Cactus_Flag1 = 0;
				break;
				case 1:
					Cactus_Flag2 = 0;
				break;
				case 2:
					Cactus_Flag3 = 0;
				break;
			}
			Cactus_Count= 0;
		}
		
		//加速
		Grade_Count++;
		if(Grade_Count == 200)
		{
			Grade ++;
			if(Grade ==  50)
				Speed ++;
			if(Grade == 100)
			{
				Speed ++;
				Cactus_CreatTime_Multiplier = 500;
			}
			if(Grade == 150)
			{
				Speed ++;
				Cactus_CreatTime_Multiplier = 800;
			}
			if(Grade == 200)
				Speed ++;
			Grade_Count = 0;
		}
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}
