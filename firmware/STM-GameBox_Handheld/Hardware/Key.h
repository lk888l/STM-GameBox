#ifndef __KEY_H
#define __KEY_H

extern uint16_t Buzzer_flag;
extern uint16_t Timer_500ms;				//俄罗斯方块定时器中断功能是否开启

//各按键的IO端口
#define Up_Port 	GPIOB
#define Down_Port	GPIOB
#define Left_Port	GPIOB
#define Right_Port	GPIOB
#define Jump_Port	GPIOB
#define Func_Port	GPIOB
#define Enter_Port	GPIOB
#define Back_Port	GPIOB

//各按键的Pin口
#define Up_Pin		GPIO_Pin_7
#define Down_Pin	GPIO_Pin_5
#define Left_Pin	GPIO_Pin_6
#define Right_Pin	GPIO_Pin_4
#define Jump_Pin	GPIO_Pin_12
#define Func_Pin	GPIO_Pin_13
#define Enter_Pin	GPIO_Pin_14
#define Back_Pin	GPIO_Pin_15


void Key_Init(void);

uint8_t Get_LeftKeyNum(void);
uint8_t Get_RighKeyNum(void);
uint8_t Get_RollKeyNum(void);

uint8_t Get_LeftKeyNum_click(void);			//左右键click模式补丁
uint8_t Get_RollKeyNum_click(void);			//上下键click模式补丁
uint8_t Get_RighKeyNum_click(void);			//右边键click模式补丁
uint8_t Get_RollKeyNum_Tetris(void);		//俄罗斯方块补丁
uint8_t Get_DionKeyNum(void);				//恐龙游戏补丁
uint8_t Get_RollKeyNum_Car(void);			//遥控补丁
uint8_t Get_LeftKeyNum_Car(void);			//遥控补丁
uint8_t Get_DoubleKeyNum_Car(void);			//遥控补丁

void Buzzer_up(void);						//蜂鸣器功能函数
	
int8_t Key_Enter_Get(void);
int8_t Key_Back_Get(void);
int8_t Key_Up_Get(void);
int8_t Key_Down_Get(void);
void Key_Reset_All(void);					//清除所有按键标志


#endif
