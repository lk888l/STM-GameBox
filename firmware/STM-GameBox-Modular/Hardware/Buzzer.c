#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Key.h"

#define GPIO_Buzzer GPIO_Pin_12
//用于获取按键
uint8_t BUzzer_KeyNum = 0;
int8_t BUzzer_Enter_KeyNum = 0;
int8_t BUzzer_Back_KeyNum = 0;

//uint32_t play_hz[] = {0, 293, 330, 350, 392, 440, 494, 523, 587}; 
uint32_t play_hz[] = {0, 261, 293, 330, 350, 392, 440, 494, 523}; 
                   
void Buzzer_Up(uint16_t tone)
{
	uint16_t hz;
	uint16_t i, j;
//	tone = play_tone[sta]; 		//由当前播放的位置获取声调
	hz = play_hz[tone]; 		//由声调获取频率

	//通过控制下面的i < hz 右侧的值来控制单个音节的播放时间
	for(i = 0; i < hz / 4; i++) // 循环体内运行一次的周期=1s÷频率，所以整个循环体就是1s，就是一个音节1s
	{
		GPIO_WriteBit(GPIOA,GPIO_Buzzer,(BitAction)(1)); //蜂鸣器接口输出0
		Delay_us(500000 / hz);
		GPIO_WriteBit(GPIOA,GPIO_Buzzer,(BitAction)(0)); //蜂鸣器接口输出1	
		Delay_us(500000 / hz);	
	}
}
void Buzzer_Begin()
{
	//提示界面
	OLED_Clear();
	OLED_ShowImage(0,0,60,55,bmp_dog);	
	OLED_Printf(61, 0, OLED_8X16, "进入后按");
	OLED_Printf(61, 16, OLED_8X16, "双键返回");
	OLED_Printf(61,32,OLED_8X16,"当前: ");
	OLED_Printf(61,48,OLED_8X16,"确认进入");
	OLED_Update();
	while(1)
	{
		if(Key_Enter_Get())
		{	break;}
		if(Key_Back_Get()) 
		{	return;}										//退出游戏
	}
	OLED_Clear();	
	/**静态显示按键位置**/
	OLED_ShowNum(10,1,1,1,OLED_8X16);
	OLED_ShowNum(1,23,2,1,OLED_8X16);
	OLED_ShowNum(20,23,3,1,OLED_8X16);
	OLED_ShowNum(10,40,4,1,OLED_8X16);
	OLED_ShowNum(100,6,5,1,OLED_8X16);
	OLED_ShowNum(120,6,6,1,OLED_8X16);
	OLED_ShowNum(100,38,7,1,OLED_8X16);
	OLED_ShowNum(120,38,8,1,OLED_8X16);
	OLED_Printf(60, 56, OLED_6X8, "C");						//显示当前声调
	OLED_Update();	
	while(1)
	{	
		
		BUzzer_KeyNum = Get_LeftKeyNum_click();
		if(BUzzer_KeyNum == 2)
		{
			Buzzer_Up(2);
		}
		else if(BUzzer_KeyNum == 1)
		{
			Buzzer_Up(3);
		}
		BUzzer_KeyNum = Get_RollKeyNum_click();
		if(BUzzer_KeyNum == 3)
		{
			Buzzer_Up(1);
		}
		else if(BUzzer_KeyNum == 4)
		{
			Buzzer_Up(4);
		}
		BUzzer_KeyNum = Get_RighKeyNum_click();
		if(BUzzer_KeyNum == 5)
		{
			Buzzer_Up(5);
		}
		else if(BUzzer_KeyNum == 6)
		{
			Buzzer_Up(6);
		}
		
		BUzzer_Enter_KeyNum = Key_Enter_Get();
		if(BUzzer_Enter_KeyNum)
		{
			Buzzer_Up(7);
		}
		BUzzer_Back_KeyNum = Key_Back_Get();
		if(BUzzer_Back_KeyNum)
		{
			Buzzer_Up(8);
		}	
				
		if(BUzzer_Enter_KeyNum && BUzzer_Back_KeyNum) 
		{
			OLED_Clear();
			OLED_ShowImage(0,0,60,55,bmp_dog);	
			OLED_Printf(61, 0, OLED_8X16, "请按返回");
			OLED_Printf(61, 16, OLED_8X16, "返回");
			OLED_Printf(61,32,OLED_8X16,"或按确认");
			OLED_Printf(61,48,OLED_8X16,"继续");			
			OLED_Update();
			while(1)
			{
				if(Key_Enter_Get())
				{	break;	}
				if(Key_Back_Get()) 
				{	return;	}										//退出游戏
			}
		}
		BUzzer_Enter_KeyNum = 0;
		BUzzer_Back_KeyNum = 0;
	}
}

