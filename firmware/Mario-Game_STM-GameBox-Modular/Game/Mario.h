#ifndef __MARIO_H
#define __MARIO_H

extern uint64_t Mario_MapDate[28][18];		//地图
extern uint8_t MapFlag_Page;			//地图的页数
extern uint8_t Round_flag;				//回合标志

void Init_MapDate(void);	//初始化地图
void Pipeline_1(void);		//下管道
void Mario_Play(void);		//游戏主函数

#endif
