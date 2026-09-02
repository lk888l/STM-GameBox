#include "menu.h" 
#include "Game_Dion.h"
#include "MyRTC.h"
#include "AirPlay.h"
#include "Tetris.h"
#include "PingPong.h"
#include "Buzzer.h"

void tools_menu(void)
{
	struct option_class option_list[] = {
		{"退出"},
		{"倒计时", count_begin},				//RTC时钟寄存器
		{"秒表", stopwatch},					//RTC时钟寄存器
//		{"遥控", Remote_Control},				//小车串口遥控器
//		{"示波器", },							//示波器（PWMI输入）
		{".."}
	};
	
	run_menu(option_list);
}

void games_menu(void)
{
	struct option_class option_list[] = {
		{"<<<"},
		{"恐龙跳", Dion_Play},					//恐龙游戏
		{"贪吃蛇", Game_Snake_Init},			//贪吃蛇
		{"飞机大战", start},					//飞机大战
		{"俄罗斯方块",Tetris_start},			//俄罗斯方块
		{"双人乒乓游戏",PiarPingpong_Play},		//双人乒乓游戏
		{"电子琴", Buzzer_Begin},				//电子琴	
		{".."}
	};
	
	run_menu(option_list);
}

/**********************************************************/
