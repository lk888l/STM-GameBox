#ifndef __MYRTC_H
#define __MYRTC_H

extern int16_t MyRTC_Time[];	//定义全局的时间数组，数组内容分别为年、月、日、时、分、秒
extern uint16_t Time_Length;

void MyRTC_Init(void);
void RTC_NVIC_Config(void);
void MyRTC_SetTime(void);
void MyRTC_ReadTime(void);
void MyRTC_ShowTime(void);

//倒计时功能
void count_begin(void);
//秒表功能
void stopwatch(void);

#endif
