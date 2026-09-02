#include "menu.h" 
#include "MyRTC.h"


struct option_class option_list[];

void Set_CursorStyle(void)
{
	if(Store_Data[1] == 0x0001)
	{
		Store_Data[1] = 0x0002;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
		//CurStyle = mouse;
		option_list[2].Name = "光标风格[鼠标]";
	}
	else if(Store_Data[1] == 0x0002)
	{
		Store_Data[1] = 0x0003;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
		//CurStyle = frame;
		option_list[2].Name = "光标风格[矩形]";
	}
	else if(Store_Data[1] == 0x0003)
	{
		Store_Data[1] = 0x0004;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
		//CurStyle = reverse;
		option_list[2].Name = "光标风格[爱心]";
	}
	else if(Store_Data[1] == 0x0004)
	{
		Store_Data[1] = 0x0001;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
		//CurStyle = aixin;
		option_list[2].Name = "光标风格[反相]";
	}
}

void Set_animation_speed(void)
{
	if(Store_Data[2] == 0x0001)
	{
		Store_Data[2] = 0x0002;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
		Speed_Factor = 16;
		Roll_Speed = 4;
		option_list[3].Name = "动画速度[慢]";
	}
	else if(Store_Data[2] == 0x0002)
	{
		Store_Data[2] = 0x0003;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
		Speed_Factor = 1;
		Roll_Speed = 16;
		option_list[3].Name = "动画速度[关]";
	}
	else if(Store_Data[2] == 0x0003)
	{
		Store_Data[2] = 0x0001;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
		Speed_Factor = 8;
		Roll_Speed = 8;
		option_list[3].Name = "动画速度[快]";
	}
}

void Set_Buzzer(void)
{
	if(Store_Data[3] == 0x0001)
	{
		Store_Data[3] = 0x0002;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失	
		Buzzer_flag = 0;
		option_list[1].Name = "静音[开]";
	}
	else if(Store_Data[3] == 0x0002)
	{
		Store_Data[3] = 0x0001;							//利用flash记录光标状态
		Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失		
		Buzzer_flag = 1;
		option_list[1].Name = "静音[关]";
	}
}

/**
  * 函    数：修改停机（休眠）模式下时间函数
  * 参    数：无
  * 返 回 值：无
  * 说    明：调用此函数后，将在设置里更改时间
*/

void SetTime_Tool(void)
{
	uint8_t MoveFlag = 0;
	uint8_t SetTime_Key_Num;
	
	OLED_Clear();
	OLED_ShowString(22, 1, "XXXX-XX-XX",OLED_8X16);
	OLED_ShowString(38, 20, "XX:XX:XX",OLED_8X16);
	while(1)
	{
		SetTime_Key_Num = Get_LeftKeyNum_click();
		if(SetTime_Key_Num == 1)
		{
			MoveFlag++;
			MoveFlag %= 6;
		}
		if(SetTime_Key_Num == 2)
		{
			MoveFlag--;
			MoveFlag %= 6;
		}
		
		MyRTC_ReadTime();
	
		OLED_ShowNum(22,1,MyRTC_Time[0],4,OLED_8X16);
		OLED_ShowNum(62,1,MyRTC_Time[1],2,OLED_8X16);
		OLED_ShowNum(86,1,MyRTC_Time[2],2,OLED_8X16);
		OLED_ShowNum(38,20,MyRTC_Time[3],2,OLED_8X16);
		OLED_ShowNum(62,20,MyRTC_Time[4],2,OLED_8X16);
		OLED_ShowNum(86,20,MyRTC_Time[5],2,OLED_8X16);
		
		if(MoveFlag == 0)		{OLED_ReverseArea(22, 1, 16,15);}
		OLED_ReverseArea(38 + 24*(MoveFlag%3), 1 + (MoveFlag/3)*20, 16,15);
		SetTime_Key_Num = Get_RollKeyNum();
		if(SetTime_Key_Num == 3)
		{
			MyRTC_Time[MoveFlag]++;
			MyRTC_SetTime();
		}
		else if(SetTime_Key_Num == 4)
		{
			MyRTC_Time[MoveFlag]--;
			MyRTC_SetTime();
		}
		
		OLED_Update();
		if(Key_Back_Get() || Key_Enter_Get())
		{return;}
	}
}

void SetTime_Length(void)
{
	uint8_t SetTime_Key_Num;
	OLED_ReverseArea(0,48,116,16);
	OLED_Printf(72,48,OLED_8X16,"%4d",Time_Length);
	OLED_ReverseArea(73,48,32,16);
	OLED_Update();
	while(1)
	{
		SetTime_Key_Num = Get_RollKeyNum();
		if(SetTime_Key_Num == 3)
		{
			if(Time_Length < 9999)	Time_Length++;
			OLED_Printf(72,48,OLED_8X16,"%4d",Time_Length);
			OLED_ReverseArea(73,48,32,16);
			OLED_UpdateArea(73,48,32,16);
		}
		if(SetTime_Key_Num == 4)
		{
			if(Time_Length>0)	Time_Length--;			
			OLED_Printf(72,48,OLED_8X16,"%4d",Time_Length);
			OLED_ReverseArea(73,48,32,16);
			OLED_UpdateArea(73,48,32,16);
		}
		if(Key_Back_Get())
		{
			return;
		}
	}
	return;
}

void Setting_menu(void)
{
	run_menu(option_list);
}
struct option_class option_list[] = {
	{"退出"},
	{"静音[关]", Set_Buzzer},
	{"光标风格[反相]", Set_CursorStyle},
	{"动画速度[快]", Set_animation_speed},	
	{"时间设置", SetTime_Tool},
	{"刷新时间[    ]", SetTime_Length},
	{".."}
};
