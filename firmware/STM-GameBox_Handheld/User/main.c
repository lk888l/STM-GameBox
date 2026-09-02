#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "Timer.h"
#include "Key.h"
#include "OLED.h"
#include "menu.h"
#include "MyRTC.h"
#include "AD.h"

int16_t MyRTC_Time[] = {2024,8,10,9,0,0};	//定义全局的时间数组，数组内容分别为年、月、日、时、分、秒
uint16_t Time_Length = 10;

int main(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);		//开启PWR的时钟，用于停止模式
	
	Timer_Init();
	MyRTC_Init();
	OLED_Init();
	Key_Init();
	AD_Init();					//用于生成随机种子
	Store_Init();				//参数存储模块初始化，在上电的时候将闪存的数据加载回Store_Data，实现掉电不丢失

	while(1)
	{
		main_menu();			//主功能函数
		MyRTC_ShowTime();		//休眠显示时间函数，默认10s更新一次
	}
}
