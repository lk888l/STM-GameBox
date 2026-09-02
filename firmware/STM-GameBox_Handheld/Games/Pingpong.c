#include "stm32f10x.h"                  // Device header
#include <stdlib.h>
#include "OLED.h"
#include "Key.h"
#include "Timer.h"
#include "AD.h"
#include "Game_Dion.h"
#include "PingPong.h"

//小球相关变量
int8_t Ball_X = 49;
int8_t Ball_Y = 7;
int8_t Ball_Vx = 1;
int8_t Ball_Vy = 1;

//球拍相关变量
int8_t Left_Bat = 30;
int8_t Right_Bat = 30;
int8_t Left_BatLength = 8;
int8_t Right_BatLength = 8;

//道具变量
uint8_t Piar_PropNum;
uint8_t Piar_ResetVy = 0;
uint8_t Piar_AddBat = 0;
func_arr Piar_PropFunc[3] = {Piar_prop_1,Piar_Prop_2,Piar_Prop_3};

//双人球拍的移动
void Piar_BatMotion()
{
//	OLED_DrawLine(8,17,8,34);				//默认长度为：17
	OLED_ClearArea(8,Left_Bat - Left_BatLength-1,1,18);
	OLED_ClearArea(120,Right_Bat - Right_BatLength-1,1,18);
	//左边
	if(GPIO_ReadInputDataBit(Up_Port, Up_Pin) == 0)
	{
		Left_Bat--;
		if(Left_Bat <= Left_BatLength+2)		Left_Bat = Left_BatLength+2;
	}
	else if(GPIO_ReadInputDataBit(Down_Port, Down_Pin) == 0)
	{
		Left_Bat++;
		if(Left_Bat >= 62-Left_BatLength)			Left_Bat = 62-Left_BatLength;
	}
	OLED_DrawLine(8,Left_Bat - Left_BatLength,8,Left_Bat + Left_BatLength);
	
	//右边
	if(GPIO_ReadInputDataBit(Jump_Port, Jump_Pin) == 0)
	{
		Right_Bat--;
		if(Right_Bat <= Right_BatLength+2)	Right_Bat = Right_BatLength+2;
	}
	if(GPIO_ReadInputDataBit(Func_Port, Func_Pin) == 0)
	{
		Right_Bat++;
		if(Right_Bat >= 62-Right_BatLength)			Right_Bat = 62-Right_BatLength;
	}
	OLED_DrawLine(120,Right_Bat - Right_BatLength,120,Right_Bat + Right_BatLength);
}

//小球的移动
void Piar_BallMotion(void)
{
	OLED_ClearArea(Ball_X-1,Ball_Y-1,3,3);
	Ball_X += Ball_Vx;
	Ball_Y += Ball_Vy;
	
	OLED_ShowImage(60,15,7,7,Image_Piarprop);
	OLED_ShowImage(60,45,7,7,Image_Piarprop);
	
	//判断道具
	Piar_JudgeProp();
	
	//判断小球
	if(Ball_X >= 117)
	{
		if(Ball_Y <=Right_Bat+Right_BatLength && Ball_Y >= Right_Bat-Right_BatLength)
		{			
			Ball_Vx = -1;
			Ball_Y += rand()%3-1;
		}
		else
		{
			Ball_Vx = -1;
			Right_BatLength--;
		}		
		if(Piar_ResetVy)
		{
			Ball_Vy = 1;
			Piar_ResetVy = 0;
		}
		
	}
	else if(Ball_X <= 11)
	{
		if(Ball_Y <=Left_Bat+Left_BatLength && Ball_Y >= Left_Bat-Left_BatLength)
		{
			Ball_Vx = 1;
			Ball_Y += rand()%3-1;
		}
		else 
		{
			Ball_Vx = 1;
			Left_BatLength--;
		}
		if(Piar_ResetVy)
		{
			Ball_Vy = 1;
			Piar_ResetVy = 0;
		}
		
	}
	
	if(Ball_Y >= 59 || Ball_Y <= 5)
	{
		Ball_Vy = -Ball_Vy;
	}
	OLED_DrawCircle(Ball_X,Ball_Y,1,OLED_FILLED);
}

//判断道具
void Piar_JudgeProp(void)
{
	if(Ball_Vx > 0)
	{
		//功能道具
		if(Ball_X >=61 && Ball_X<= 65)
		{
			if((Ball_Y<=22 && Ball_Y>=15) || (Ball_Y<=52&&Ball_Y>=45))
			{
				srand(AD_GetValue(1));
				Piar_PropNum = rand()%3;
				Piar_PropFunc[Piar_PropNum]();
				return;
			}
		}
		//回血道具
		if(Piar_AddBat && (Ball_X>=61&&Ball_X<=66) && (Ball_Y>=31&&Ball_Y<=36))
		{
			Piar_AddBat = 0;
			if(Left_BatLength < 8)	Left_BatLength++;
			OLED_ClearArea(61,31,5,5);
			RefreshFlag = 0;
		}
	}
	
	else if(Ball_Vx < 0)
	{
		//功能道具
		if(Ball_X >=65 && Ball_X<= 70)
		{
			if((Ball_Y<=22 && Ball_Y>=15) || (Ball_Y<=52&&Ball_Y>=45))
			{
				srand(AD_GetValue(1));
				Piar_PropNum = rand()%3;
				Piar_PropFunc[Piar_PropNum]();
				return;
			}
		}
		//回血道具
		if(Piar_AddBat && (Ball_X>=61&&Ball_X<=66) && (Ball_Y>=31&&Ball_Y<=36))                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                
		{
			Piar_AddBat = 0;
			if(Right_BatLength < 8)	Right_BatLength++;
			OLED_ClearArea(61,31,5,5);
			RefreshFlag = 0;
		}
	}
}

//判断游戏结束
int8_t Piar_JudgeOver(void)
{
	if(Left_BatLength == 1)
	{
		return -1;
	}
	else if(Right_BatLength == 1)
	{
		return -2;
	}
}

//游戏入口
void PiarPingpong_Play(void)
{
	OLED_Clear();
	OLED_DrawLine(8,2,120,2);
	OLED_DrawLine(8,62,120,62);
	TimerLength = 3500;					//设置时长为2000ms
	Timer_500ms = 1;					//开启定时器刷新
	//相关变量初始化
	Left_Bat = 30,Right_Bat = 30,Left_BatLength = 8,Right_BatLength = 8;//球拍变量
	Ball_X = 49,Ball_Y = 7,Ball_Vx = 1,Ball_Vy = 1;						//小球相关变量
	Piar_ResetVy = 0,Piar_AddBat = 0;				//道具相关变量
	while(1)
	{
		if(Key_Back_Get())
		{Timer_500ms = 0;return;}
		Piar_BallMotion();
		Piar_BatMotion();
		
		//显示加血道具
		if(!(Piar_AddBat) && RefreshFlag ==1)
		{
			Piar_AddBat = 1;
			OLED_ShowImage(61,31,5,5,Image_PiarAddBat);
			RefreshFlag = 0; 
		}
		
		if(Piar_JudgeOver() == -1)
		{
			Timer_500ms = 0;		//取消刷新功能
			OLED_Clear();
			OLED_ShowImage(0,0,60,55,bmp_dog);
			OLED_Printf(61, 0, OLED_8X16, "游戏结束");
			OLED_Printf(61,16,OLED_8X16,"右方获胜");
			OLED_Printf(77,40,OLED_8X16,"请返回");
			OLED_Update();
			while(1)
			{
				if(Key_Back_Get())
				{
					break;
				}
			}
			return;
		}
		else if(Piar_JudgeOver() == -2)
		{
			Timer_500ms = 0;		//取消刷新功能
			OLED_Clear();
			OLED_ShowImage(0,0,60,55,bmp_dog);
			OLED_Printf(61, 0, OLED_8X16, "游戏结束");
			OLED_Printf(61,16,OLED_8X16,"左方获胜");
			OLED_Printf(77,40,OLED_8X16,"请返回");
			OLED_Update();
			while(1)
			{
				if(Key_Back_Get())
				{
					break;
				}
			}
			return;
		}
		OLED_Update();
	}			
}


void Piar_prop_1(void)
{
	Ball_Vx = 2;
	Ball_Vy = 0;
	Piar_ResetVy = 1;
}

void Piar_Prop_2(void)
{
	Ball_Vy = -Ball_Vy;
}

void Piar_Prop_3(void)
{
	Ball_Vx = -Ball_Vx;
}
