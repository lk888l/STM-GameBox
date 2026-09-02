#include "stm32f10x.h"                  // Device header
#include <stdlib.h>
#include "OLED.H"
#include "Key.h"
#include "DELAY.H"
#include "menu.h"

//1、使用OLED显示、加载图片、链表结构的实现、变量的定义
//2、生成我方飞机、我方飞机的移动
//3、子弹的生成、发射、释放

//枚举定义，定义一些常用参数
enum My
{
	BULLET_NUM = 3,	//玩家子弹数量
	ENMY_NUM = 1			//敌机数量
	
};

//定义结构体：飞机的位置，速度，存活状态
struct Plane
{
	int16_t x;
	uint16_t y;
	uint16_t live;		//是否存活
	uint16_t speed;	//速度
	uint16_t width;	//宽度
	uint16_t height;	//高度
	uint16_t hp;			//血量
	uint16_t type;		//敌机的类型 big small
}player,bull[BULLET_NUM],enmy[ENMY_NUM];

//变量定义
uint16_t bull_len = 0;
uint16_t score = 0;
uint16_t place = 1;


//初始化敌机血量
void enmyhp(int i)
{
	if(Rnum()%10 == 0)	//0~9
	{
		enmy[i].type = 2;
		enmy[i].hp = 5;
		enmy[i].width = 24;
		enmy[i].height = 20;
	}
	else
	{
		enmy[i].type = 1;
		enmy[i].hp = 1;
		enmy[i].width = 15;
		enmy[i].height = 14;
	}
}
//游戏初始化函数
void AirPlay_Init()
{
	player.x = 0;
	player.y = 20;
	player.live = 1;
	player.speed = 1;
	
	//刷新飞机在屏幕出现的bug
	OLED_ShowImage(player.x,player.y,18,16,Img_MyAir);
	OLED_ClearArea(player.x-1,player.y-1,20,1);
	OLED_ClearArea(player.x-1,player.y-1,1,18);
	OLED_ClearArea(player.x+18,player.y-1,1,18);
	OLED_Update();
	player.x++;
	OLED_ShowImage(player.x,player.y,18,16,Img_MyAir);
	OLED_ClearArea(player.x-1,player.y-1,20,1);
	OLED_ClearArea(player.x-1,player.y-1,1,18);
	OLED_ClearArea(player.x+18,player.y-1,1,18);
	OLED_Update();
	
	//初始化子弹
	for(int i;i< BULLET_NUM;i++)
	{
		bull[i].x = 0;
		bull[i].y = 0;
		bull[i].live = 0;
	}
	
	//初始化敌机
	for(int i = 0;i<ENMY_NUM;i++)
	{
		enmy[i].live = 0;
		enmyhp(i);
	}
	
	OLED_Update();
}

//生成子弹
void CreateBullet()
{
			if(bull[0].live == 0)
			{
				bull[0].x = player.x + 19;
				bull[0].y = player.y + 6;
				bull[0].live = 1;
				OLED_ShowImage(bull[0].x,bull[0].y,3,3,Img_Bullet);
			}
			for(int i=1;i<=BULLET_NUM;i++)
			{
				if(bull[i-1].live == 1 && bull[i].live == 0)
				{
					if(bull[i-1].x - player.x >= 30)
					{
						bull[i].x = player.x + 19;
						bull[i].y = player.y + 6;
						bull[i].live = 1;
						OLED_ShowImage(bull[i].x,bull[i].y,3,3,Img_Bullet);
					}
				}
				
			}
			
	}

	//生成敌机
	void CreateEnmy()
	{
		for(int i = 0;i<ENMY_NUM;i++)
		{
			if(enmy[i].live == 0)
			{
				enmy[i].live = 1;
				enmy[i].x = 130;
				enmy[i].y = Rnum()%60;
				if(enmy[i].y > 49)
				{
					enmy[i].y = enmy[i].y - 40;
				}
				break;
			}
		}
	}

//子弹移动
void Bullet_Move()
{
	for(int i=0;i<BULLET_NUM;i++)
	{
		if(bull[i].live)
		{
			bull[i].x += 1;
			OLED_ShowImage(bull[i].x,bull[i].y,3,3,Img_Bullet);
			OLED_ClearArea(bull[i].x-1,bull[i].y-1,1,3);
			if(bull[i].x >128)
			{		
				bull[i].live = 0;
				bull[i].x = 0;
				bull[i].y = 0;
			}
		}
	}
}

//敌机移动
void Enmy_Move()
{
	CreateEnmy();
	for(int i =0;i<ENMY_NUM;i++)
	{
		if(enmy[i].live == 1)
		{
			enmy[i].x -= place;
			OLED_ShowImage(enmy[i].x,enmy[i].y,15,14,Img_EnmyAir);
			//判断子弹和敌机的碰撞
			for(int j=0;j<BULLET_NUM;j++)
			{
				if(bull[j].live && (enmy[i].x + 2) <= bull[j].x && bull[j].x <= (enmy[i].x + 8) && (enmy[i].y+2) <= bull[j].y && bull[j].y <= (enmy[i].y+11) )
				{
					enmy[i].live = 0;
					OLED_ClearArea(enmy[i].x-5,enmy[i].y,20,17);
					score++;
					//enmy[i].x = 129;
					//enmy[i].y = 65;
					bull[j].live = 0;
					bull[j].x = 0;
					bull[j].y = 0;
				}
			}
			if(enmy[i].x == 0)
			{
				enmy[i].live = 0;
				//OLED_ClearArea(enmy[i].x,enmy[i].y,15,14);
				OLED_Clear();
				if(score>Store_Data[6])
				{
					Store_Data[6] = score;							//利用flash记录最高分数 
					Store_Save();									//将Store_Data的数据备份保存到闪存，实现掉电不丢失
					
				}
				while(1)
				{
					
					//退出界面显示
					OLED_Clear();
					OLED_ShowImage(0,0,60,55,bmp_dog);		
					OLED_Printf(61, 0, OLED_8X16, "请按双键");
					OLED_Printf(61, 16, OLED_8X16, "退出");
					OLED_Printf(61, 32, OLED_8X16, "得分");
					OLED_Printf(61, 48, OLED_8X16, "最高");					
					OLED_ShowNum(97,32,score,4,OLED_6X8);
					OLED_ShowNum(97,52,Store_Data[6],4,OLED_6X8);
					OLED_Update();
					
					if(Key_Enter_Get()) {break;}		//退出游戏
					if(Key_Back_Get()) {break;}		//退出游戏
				}
			}
		}
	}
}

//角色移动，获取按键信息上下左右
void AirPlay_Move()
{
	//扫描获取按键
	if(GPIO_ReadInputDataBit(Up_Port,Up_Pin) == 0)
	{
		if(player.y>1)
		{
			player.y = player.y-player.speed;
		}
	}
	if(GPIO_ReadInputDataBit(Down_Port,Down_Pin) == 0)
	{
		if(player.y<49)
		{
			player.y = player.y+player.speed;
		}
	}
	if(GPIO_ReadInputDataBit(Jump_Port,Jump_Pin) == 0)
	{
		//首先创建一个子弹
		CreateBullet();
	}
}


//游戏环节，所有功能的实现都从这个函数开始
void start()
{
	score = 0;
	OLED_Clear();
	AirPlay_Init();
	while(1)
	{
		//显示分数
		OLED_ShowNum(102,56,score,4,OLED_6X8);
		
		if(score > 30)
		{
			place = 2;
		}
		
		AirPlay_Move();
		Enmy_Move();
		OLED_ShowImage(player.x,player.y,18,16,Img_MyAir);
		OLED_ClearArea(player.x-1,player.y-1,20,1);
		OLED_ClearArea(player.x-1,player.y-1,1,18);
		OLED_ClearArea(player.x+18,player.y-1,1,18);
		Bullet_Move();
		OLED_Update();
		if(Key_Enter_Get() || Key_Back_Get()) {return;}		//退出游戏
	}
}

