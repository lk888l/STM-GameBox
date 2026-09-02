#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Delay.h"
#include "Key.h"
#include "Mario.h"

//初始化时
uint8_t Show_X;
uint8_t Show_Y;
//显示偏移量
uint8_t Show_Offset = 0;
uint8_t Dynamic=0;		//是否动态补偿
//得分
uint16_t Mario_Grade=0;
//回合
uint8_t Round_flag=1;

//马里奥总生命
uint8_t MarioLife_Flag = 4;

//地图相关变量
uint8_t Mario_Map[18][43] = {0};
uint8_t MapFlag_X = 0;
uint8_t MapFlag_Y = 0;
uint8_t MapFlag_Page = 0;			//地图的页数

/*    !地图具体!      */
//2：实心墙；3：道具墙；4：乌龟；5：下水道墙；6：普通墙；7：放大菇；8：胜利旗帜；9:敌人蘑菇

//跳跃相关变量（马里奥）
uint8_t Jump_HighLimit = 25;
uint8_t Jump_Plus = 0;

struct player {
    int8_t flag;  		//1、分辨这是什么：0马里奥、1放大蘑菇、2星星、3敌人蘑菇、4敌人乌龟、5龟壳
    int8_t life;		//2、生命
    int8_t if_Falling;  //3、是否处于空中
    int8_t Speed;   	//4、移动速度
    int8_t X;			//5、坐标X
    int8_t Y;			//6、坐标Y
    int8_t Height;		//7、图像高度
    int8_t Width;		//8、图像宽度
    const uint8_t *Image;
}
Mario = {0,		
         1,
         0,
         1,
         10,
         45,
         10,
         8,
         Mario_Man},
//敌人蘑菇
Enemy_Aaric[] ={
	{3, 0, 0, -1, 120, 51, 6, 6, Mario_EnemyAaric},
	{3, 0, 0, -1, 120, 51, 6, 6, Mario_EnemyAaric}},
//敌人乌龟
Enemy_Tortoise[] = {
	{4, 0, 0, -1, 119, 51, 8, 6, Mario_Tortoise},
	{4, 0, 0, -1, 119, 51, 8, 6, Mario_Tortoise},},
//放大蘑菇
Plus_Aaric = {1, 0, 0, 1, 0, 0, 5, 5, Mario_PlusAaric};

//道具（金币或者星星）
struct Property{
    int8_t life;		//1、生命
    int8_t if_Falling;  //2、是否处于空中
	int8_t Move_flag;	//3、地图移动的判断
	int8_t X;			//4、坐标X
    int8_t Y;			//5、坐标Y
	int8_t H_Limit;		//6、跳的高度限制
}

coin[5] = {
	{0,0,0,119,1,0},{0,0,0,119,1,0},{0,0,0,119,1,0},{0,0,0,119,1,0},{0,0,0,119,1,0}
};
		  
//初始化地图
void Init_MapDate(void)
{	
	uint8_t Flag_Page=0;
	Show_Offset = 0;		//重置显示偏移量
	for(Flag_Page=0;Flag_Page<2;Flag_Page++)
	{
		MapFlag_Y = 0;
		for(uint8_t i=0;i<18;i++)
		{
			MapFlag_X = Flag_Page * 18;
			for(uint8_t j=0;j<18;j++)
			{
				Mario_Map[MapFlag_Y][MapFlag_X] = Mario_MapDate[MapFlag_Page][i]%10;
				Mario_MapDate[MapFlag_Page][i] /= 10;
				MapFlag_X++;
			}
			MapFlag_Y++;
		}
		MapFlag_Page++;
	}
	//末尾补上
	MapFlag_Y = 0;
	for(uint8_t i=0;i<18;i++)
	{
		MapFlag_X = Flag_Page * 18;
		for(uint8_t j=0;j<7;j++)
		{
			Mario_Map[MapFlag_Y][MapFlag_X] = Mario_MapDate[MapFlag_Page][i]%10;
			Mario_MapDate[MapFlag_Page][i] /= 10;
			MapFlag_X++;			
		}
		MapFlag_Y++;
	}
//	MapFlag_Page++;
	
	//显示地图
	for(uint8_t i=0;i<18;i++)
	{
		for(uint8_t j=0;j<42;j++)
		{
			if(Mario_Map[i][j] == 2)
			{
				OLED_DrawRectangle(j*3,i*3+8,3,3,OLED_FILLED);
			}
			else if(Mario_Map[i][j] == 3 && Mario_Map[i][j-1] != 3)
			{
				OLED_DrawRectangle(j*3,i*3+8,6,3,OLED_UNFILLED);
			}
		}
	}
}

//生成自动物体
void Init_AutoObject(uint8_t flag)
{
	if(flag == 9)						//生成敌人蘑菇
	{
		if(Enemy_Aaric[0].life == 0)
		{Enemy_Aaric[0].life = 1,Enemy_Aaric[0].X=119,Enemy_Aaric[0].Speed=-1;}
		else if(Enemy_Aaric[1].life == 0)
		{Enemy_Aaric[1].life = 1,Enemy_Aaric[1].X=119,Enemy_Aaric[1].Speed=-1;}
	}
	else if(flag == 4)					//生成敌人乌龟
	{
		if(Enemy_Tortoise[0].life == 0)
			Enemy_Tortoise[0].X=119,Enemy_Tortoise[0].Image=Mario_Tortoise,
			Enemy_Tortoise[0].Width=8,Enemy_Tortoise[0].flag=4,Enemy_Tortoise[0].life = 1;			//置相关参数
		else if(Enemy_Tortoise[1].life == 0)
			Enemy_Tortoise[1].X=119,Enemy_Tortoise[1].Image=Mario_Tortoise,
			Enemy_Tortoise[1].Width=8,Enemy_Tortoise[1].flag=4,Enemy_Tortoise[1].life = 1;
	}
	else if(flag == 7)	
	{Plus_Aaric.life = 1;Plus_Aaric.Speed=1;}
	else if(flag == 3)					//生成金币
	{
		if(coin[0].life == 0)
		{
			//置相关参数
			coin[0].life = 1,coin[0].X = Mario.X+4,coin[0].Y = Mario.Y-5,
			coin[0].H_Limit = coin[0].Y-8,coin[0].if_Falling=1;
		}
		else if(coin[1].life == 0)
		{
			coin[1].life = 1,coin[1].X = Mario.X+4,coin[1].Y = Mario.Y-5,
			coin[1].H_Limit = coin[1].Y-8,coin[1].if_Falling=1;
		}
		else if(coin[2].life == 0)
		{
			coin[2].life = 1,coin[2].X = Mario.X+4,coin[2].Y = Mario.Y-5,
			coin[2].H_Limit = coin[2].Y-8,coin[2].if_Falling=1;
		}
		else if(coin[3].life == 0)
		{
			coin[3].life = 1,coin[3].X = Mario.X+4,coin[3].Y = Mario.Y-5,
			coin[3].H_Limit = coin[3].Y-8,coin[3].if_Falling=1;
		}
		else if(coin[4].life == 0)
		{
			coin[4].life = 1,coin[4].X = Mario.X+4,coin[4].Y = Mario.Y-5,
			coin[4].H_Limit = coin[4].Y-8,coin[4].if_Falling=1;
		}
	}
}


//自动物体是否撞墙
uint8_t Autocash(struct player* object)
{
	if(Mario_Map[((*object).Y-6)/3][((*object).X+(*object).Width+Show_Offset)/3] != 0 ||
	Mario_Map[((*object).Y+(*object).Height-10)/3][((*object).X+(*object).Width+Show_Offset)/3] != 0||
	Mario_Map[((*object).Y-6)/3][((*object).X-Show_Offset)/3] != 0 || 
	Mario_Map[((*object).Y+(*object).Height-10)/3][((*object).X-Show_Offset)/3] != 0)
	{
		return 1;
	}
}

//游戏获胜的踩旗子动画
void Game_Wing()
{
	Mario_Grade += (72-Mario.X)*50;
	while(Mario.Y<49)
	{
		OLED_ClearArea(Mario.X,Mario.Y,Mario.Width,3);
		Mario.Y++;
		OLED_ShowImage(Mario.X,Mario.Y,Mario.Width,Mario.Height,Mario.Image);
		OLED_DrawTriangle(Mario.X,Mario.Y+Mario.Height,Mario.X+Mario.Width,Mario.Y+Mario.Height,Mario.X,Mario.Y+5+Mario.Height,OLED_FILLED);
		OLED_Update();
		Delay_ms(26);
	}
	
	OLED_Clear();
	
	while(1)
	{
		OLED_Printf(0,0,OLED_8X16,"Game Over!!!");
		OLED_ShowNum(16,16,Mario_Grade,5,OLED_6X8);
		OLED_Update();
	}
}

//移动时更新地图缓存
void Gat_MapData(void)
{
	Show_Offset++;
	if(Show_Offset ==2)
	{	
		Show_Offset =0;
		for(uint8_t i=0;i<18;i++)
		{
			Mario_Map[i][0] = Mario_Map[i][1];
			OLED_ClearArea(3,i*3+8,3,4);
			for(uint8_t j=1;j<42;j++)
			{	//消除刷新前的内容
				if(Mario_Map[i][j] == 2 || Mario_Map[i][j] == 5)			//实心墙
				{OLED_ClearArea(j*3+1,i*3+8,3,4);}
				else if(Mario_Map[i][j] == 3)		//
				{OLED_ClearArea(j*3,i*3+8,6,4);}
				else if(Mario_Map[i][j] == 7)		//
				{OLED_ClearArea(j*3,i*3+8,6,4);}
//				else if(Mario_Map[i][j] == 10)		//
//				{OLED_ClearArea(j*3,i*3+8,6,4);OLED_ClearArea(j*3+2,i*3+2,4,4);}
				Mario_Map[i][j] = Mario_Map[i][j+1];
			}
		}
		if(Mario_MapDate[MapFlag_Page][1]%10 == 1)	MapFlag_Page++;
		for(uint8_t i=0;i<18;i++)
		{	//
			Mario_Map[i][42] = Mario_MapDate[MapFlag_Page][i]%10;
			Mario_MapDate[MapFlag_Page][i] /= 10;
			//是否生成自动物体
			if(Mario_Map[i][42] == 9)					//生成蘑菇并使地图缓存置零
			{Init_AutoObject(9),Mario_Map[i][42] = 0;}
			else if(Mario_Map[i][42] == 4)				//生成乌龟
			{Init_AutoObject(4),Mario_Map[i][42] = 0;}
			else if(Mario_Map[i][42] == 8)				//到达终点
			{
				Game_Wing();
			}
		}
		//补偿动态的物体
		if(Autocash(&Enemy_Aaric[0]))	Enemy_Aaric[0].X-=3,Enemy_Aaric[0].Speed = -Enemy_Aaric[0].Speed;
		if(Autocash(&Enemy_Aaric[1]))	Enemy_Aaric[1].X-=3,Enemy_Aaric[1].Speed = -Enemy_Aaric[1].Speed;
//		if(Autocash(&Plus_Aaric))		Plus_Aaric.X-=3;Plus_Aaric.Speed=-Plus_Aaric.Speed;
	}
	//移动静态的物体
	if(Enemy_Tortoise[0].flag==5&&Enemy_Tortoise[0].Speed==0)
	{
		OLED_ClearArea(Enemy_Tortoise[0].X+Enemy_Tortoise[0].Width,Enemy_Tortoise[0].Y,Enemy_Tortoise[0].Width+2,Enemy_Tortoise[0].Height);
		Enemy_Tortoise[0].X-=Show_Offset+1;
	}
	if(Enemy_Tortoise[1].flag==5&&Enemy_Tortoise[1].Speed==0)
	{
		OLED_ClearArea(Enemy_Tortoise[1].X+Enemy_Tortoise[1].Width,Enemy_Tortoise[1].Y,Enemy_Tortoise[1].Width+2,Enemy_Tortoise[1].Height);
		Enemy_Tortoise[1].X-=Show_Offset+1;
	}
}

//游戏时显示地图缓存中的数据
void Show_Map()
{
	for(uint8_t i=0;i<18;i++)
	{
		//最左边的一排
		if(Mario_Map[i][0] == 0)
		{OLED_ClearArea(0,i*3+7,3,4);}
		if(Mario_Map[i][1] == 2 || Mario_Map[i][1] == 5)		//刷新实心墙
		{
				OLED_ClearArea(3-Show_Offset+3,i*3+8,1,4);
				OLED_DrawRectangle(3-Show_Offset,i*3+8,3,4,OLED_FILLED);
		}
		else if(Mario_Map[i][1] == 6)	//刷新普通墙
		{
				OLED_ClearArea(3-Show_Offset+3,i*3+8,2,4);
				OLED_ShowImage(3-Show_Offset,i*3+8,3,4,Mario_Grand);
		}
		
		for(uint8_t j=2;j<42;j++)
		{
			if(Mario_Map[i][j] == 2 || Mario_Map[i][j] == 5)//刷新实心墙
			{
				OLED_ClearArea(j*3-Show_Offset+3,i*3+8,1,4);
				OLED_DrawRectangle(j*3-Show_Offset,i*3+8,3,4,OLED_FILLED);
				
			}
			else if(Mario_Map[i][j] == 6)		//刷新普通墙
			{
				OLED_ClearArea(j*3-Show_Offset+3,i*3+8,2,4);
				OLED_ShowImage(j*3-Show_Offset,i*3+8,3,4,Mario_Grand);
			}
			else if(Mario_Map[i][j] == 3 && Mario_Map[i][j+1] == 3)		//刷新道具墙
			{
				OLED_ClearArea(j*3-Show_Offset,i*3+8,9,4);
				OLED_DrawRectangle(j*3-Show_Offset,i*3+8,7,4,OLED_UNFILLED);
			}
			else if(Mario_Map[i][j] == 7 && Mario_Map[i][j+1] == 7)		//刷新放大菇墙
			{
				OLED_ClearArea(j*3-Show_Offset,i*3+8,9,4);
				OLED_DrawRectangle(j*3-Show_Offset,i*3+8,7,4,OLED_UNFILLED);
			}
//			else if(Mario_Map[i][j] == 10 && Mario_Map[i][j+1] == 10)	//刷新撞后的金币墙
//			{
//				OLED_ClearArea(j*3-Show_Offset+6,i*3+8,2,4);
//				OLED_ClearArea(j*3-Show_Offset+4,i*3+2,2,4);
//				OLED_DrawRectangle(j*3-Show_Offset,i*3+8,6,4,OLED_FILLED);
//				OLED_DrawCircle(j*3+2-Show_Offset,i*3+4,1,OLED_FILLED);
//			}
		}
	}
		
}

/*
	*函数功能：检查是否撞墙
	*返回值：0表示撞实心墙，1表示撞道具墙，2表示撞乌龟墙，3表示星星墙，4表示撞普通墙，5表示没有撞墙
*/
uint8_t Jump_Obstruct(struct player* object)
{
	//头上
	if(Mario_Map[((*object).Y-8)/3][((*object).X+8)/3]!=0 || Mario_Map[((*object).Y-8)/3][((*object).X+5)/3]!=0 || Mario_Map[((*object).Y-8)/3][((*object).X+2)/3]!=0 || Mario_Map[((*object).Y-8)/3][((*object).X)/3]!=0)
	{
		//是道具墙
		if(Mario_Map[((*object).Y-8)/3][((*object).X+7)/3]==3 || Mario_Map[((*object).Y-8)/3][((*object).X+5)/3]==3 || Mario_Map[((*object).Y-8)/3][((*object).X+2)/3]==3 || Mario_Map[((*object).Y-8)/3][((*object).X+1)/3]==3)
		{
			return 1;
		}
		//是放大菇墙
		else if(Mario_Map[((*object).Y-8)/3][((*object).X+7)/3]==7 || Mario_Map[((*object).Y-8)/3][((*object).X+5)/3]==7 || Mario_Map[((*object).Y-8)/3][((*object).X+2)/3]==7 || Mario_Map[((*object).Y-8)/3][((*object).X+1)/3]==7)
		{
			return 2;
		}
		//是星星墙
		else if(Mario_Map[((*object).Y-8)/3][((*object).X+8)/3]==5 || Mario_Map[((*object).Y-8)/3][((*object).X+5)/3]==5 || Mario_Map[((*object).Y-8)/3][((*object).X+2)/3]==5 || Mario_Map[((*object).Y-8)/3][((*object).X)/3]==5)
		{
			return 3;
		}
		//是普通墙
		else if(Mario_Map[((*object).Y-8)/3][((*object).X+8)/3]==6 || Mario_Map[((*object).Y-8)/3][((*object).X+5)/3]==6 || Mario_Map[((*object).Y-8)/3][((*object).X+2)/3]==6 || Mario_Map[((*object).Y-8)/3][((*object).X)/3]==6)
		{
			return 4;
		}
		//是实心墙
		return 0;
	}
	return 5;
}

/*
	*函数功能：检查是否撞墙
	*返回值：
*/
uint8_t Move_Obstruct(struct player* object)
{
	//左边
	if(Mario_Map[((*object).Y-7)/3][((*object).X-1)/3]!=0 || Mario_Map[((*object).Y-4)/3][((*object).X-1)/3]!=0 || Mario_Map[((*object).Y-1)/3][((*object).X-1)/3]!=0 || Mario_Map[((*object).Y+1)/3][((*object).X-1)/3]!=0)
	{
		return 2;
	}
	//右边
	if(Mario_Map[((*object).Y-7)/3][((*object).X+9)/3]!=0 || Mario_Map[((*object).Y-4)/3][((*object).X+9)/3]!=0 || Mario_Map[((*object).Y-1)/3][((*object).X+9)/3]!=0 || Mario_Map[((*object).Y+1)/3][((*object).X+9)/3]!=0)
	{
		return 1;
	}
}

//物体是否有碰撞
uint8_t Object_Crash(struct player* a,struct player* b)
{
//	 
	if((*b).X>(*a).X+(*a).Width || (*a).X>(*b).X+(*b).Width || (*b).Y>(*a).Y+(*a).Height || (*a).Y>(*b).Y+(*b).Height)
		return 0;
	else 
	{
		if((*a).flag == 0 && (*a).Y+(*a).Height==(*b).Y)
			return 2;
		return 1;
	}
}

//马里奥扣血时
void Game_Over()
{
	if(Mario.life == 2)			//大状态
	{
		OLED_ClearArea(Mario.X,Mario.Y,Mario.Width,Mario.Height);
		OLED_UpdateArea(Mario.X,Mario.Y,Mario.Width,Mario.Height);
		Delay_ms(300);
		OLED_ShowImage(Mario.X,Mario.Y,8,10,Mario_Man);
		OLED_UpdateArea(Mario.X,Mario.Y,8,10);
		Delay_ms(300);
		OLED_ClearArea(Mario.X,Mario.Y,Mario.Width,Mario.Height);
		OLED_UpdateArea(Mario.X,Mario.Y,Mario.Width,Mario.Height);
		Delay_ms(300);
		Mario.life = 1;
		Mario.Width=8,Mario.Height=10;
		Mario.Y-=4,Mario.Image = Mario_Man;
	}
	else if(Mario.life == 1)	//小状态
	{
		MarioLife_Flag--;
		OLED_Clear();
		OLED_Update();
		Delay_ms(500);
		OLED_Printf(40,26,OLED_8X16," X%d",MarioLife_Flag);
		OLED_Update();
		Delay_s(1);
		OLED_Clear();
	}
}

//人物的重力函数
void Man_Gravity(struct player* object)
{
	uint8_t tem = 0;
	//检查是否下落
	if((Mario_Map[((*object).Y+(*object).Height-8)/3][((*object).X+8)/3] == 0 && Mario_Map[((*object).Y+(*object).Height-8)/3][((*object).X)/3] == 0) && (*object).if_Falling == 0)
	{
		(*object).if_Falling = 2;
	}
	//跳起时
	if((*object).if_Falling == 1)
	{
		(*object).Y--;	
		tem = Jump_Obstruct(object);
		if(tem != 5)					//判断是否撞墙
		{
			if(tem == 1)				//撞到道具墙时
			{
				Mario_Grade+=20;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3] == 3)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3] = 2;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-1] == 3)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-1] = 2;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+1] == 3)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+1] = 2;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+2] == 3)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+2] = 2;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-2] == 3)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-2] = 2;
				//生成金币
				Init_AutoObject(3);
			}
			else if(tem == 4 && Mario.life == 2)	//变大时撞普通墙
			{
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3] == 6)	
					Mario_Map[((*object).Y-10)/3][((*object).X+4)/3] = 0,OLED_ClearArea(((*object).X+4)/3*3-Show_Offset,((*object).Y-10)/3*3+8,3,4);
																		 
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-1] == 6)	
					Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-1] = 0,OLED_ClearArea((((*object).X+4)/3-1)*3-Show_Offset,((*object).Y-10)/3*3+8,3,4);
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+1] == 6)	
					Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+1] = 0,OLED_ClearArea((((*object).X+4)/3+1)*3-Show_Offset,((*object).Y-10)/3*3+8,3,4);
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+2] == 6)	
					Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+2] = 0,OLED_ClearArea((((*object).X+4)/3+2)*3-Show_Offset,((*object).Y-10)/3*3+8,3,4);
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-2] == 6)	
					Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-2] = 0,OLED_ClearArea((((*object).X+4)/3-2)*3-Show_Offset,((*object).Y-10)/3*3+8,3,4);                                                                                      
			}
			else if(tem == 2)			//撞到放大蘑菇
			{
				Init_AutoObject(7);
				Plus_Aaric.X = (Mario.X-1)/3*3-2, Plus_Aaric.Y = Mario.Y - 10;	//置放大菇坐标
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3] == 7)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3] = 2;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-1] == 7)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-1] = 2;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+1] == 7)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+1] = 2;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+2] == 7)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3+2] = 2;
				if(Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-2] == 7)	Mario_Map[((*object).Y-10)/3][((*object).X+4)/3-2] = 2;
			}
			
			(*object).Y++;(*object).if_Falling = 2;return;		//置相关参数
		}
		if((*object).Y <= Jump_HighLimit)					
		{	
			if(GPIO_ReadInputDataBit(Jump_Port,Jump_Pin)==0 && Jump_Plus==0)	//到顶时判断是否继续上升
			{Jump_Plus = 1;Jump_HighLimit -= 8;}
			else (*object).if_Falling = 2;
		}
		OLED_ClearArea((*object).X,(*object).Y+(*object).Height,(*object).Width,1);		//消除向上时的残影
	}
	//下落时
	else if((*object).if_Falling == 2)
	{
		if(Mario_Map[((*object).Y+(*object).Height-8)/3][((*object).X+6)/3]!=0 || Mario_Map[((*object).Y+(*object).Height-8)/3][((*object).X+2)/3]!=0)
		{Mario.if_Falling = 0;return;}
		Mario.Y++;
		OLED_ClearArea(Mario.X,Mario.Y-1,8,1);
		if(Mario.Y>54)			//掉下悬崖时
		{Mario.life=1,Mario.X-=15,Mario.Y = 0,Mario.Height=10,Mario.Width=8,Mario.Image=Mario_Man;Game_Over();}	//置相关参数
	}
}

//其他物体的重力函数
void Object_Gravity(struct player* object)
{
	//检查是否下落
	if((Mario_Map[((*object).Y-2)/3][((*object).X+6)/3] == 0 && Mario_Map[((*object).Y-2)/3][((*object).X)/3] == 0) && (*object).if_Falling == 0)
	{
		(*object).if_Falling = 2;
	}
	//下落时
	else if((*object).if_Falling == 2)
	{
		if(Mario_Map[((*object).Y+(*object).Height-8)/3][((*object).X+6)/3]!=0 || Mario_Map[((*object).Y+(*object).Height-8)/3][((*object).X+2)/3]!=0)
		{(*object).if_Falling = 0;return;}
		(*object).Y++;
		OLED_ClearArea((*object).X,(*object).Y-1,8,1);
	}

}

//按键控制物体
void Man_Control()
{
	if(Get_RighKeyNum_click()==5 && Mario.if_Falling==0)		//跳跃
	{
		Jump_HighLimit = Mario.Y - 18;
		if(Jump_HighLimit <0)	Jump_HighLimit = 0;
		Mario.if_Falling = 1;
		Jump_Plus = 0;					//大跳
	}
	if(GPIO_ReadInputDataBit(Right_Port,Right_Pin) == 0)		//向右
	{
		if(Mario.X <= 60 && Move_Obstruct(&Mario) != 1)
		{
			Mario.Speed = 1;
			OLED_ClearArea(Mario.X,Mario.Y,3,10);
			Mario.X++;
		}
		else if(Mario.X > 60 && Move_Obstruct(&Mario) != 1)					//向右更新地图
		{
			Gat_MapData();
			Dynamic=1;										//动态补偿
		}
		else if(Move_Obstruct(&Mario) == 1)					//人物撞墙时不用动态补偿
			Dynamic=0;	
	}
	else if(GPIO_ReadInputDataBit(Right_Port,Right_Pin) == 1)//不向右时重置动态补偿参数
	{	Dynamic=0;}				//动态补偿
	if(GPIO_ReadInputDataBit(Left_Port,Left_Pin) == 0)		//向左
	{
		if(Mario.X > 0 && Move_Obstruct(&Mario) != 2)
		{
			Mario.Speed = -1;
			OLED_ClearArea(Mario.X+7,Mario.Y,3,10);
			Mario.X--;
		}
	}
	else if(GPIO_ReadInputDataBit(Down_Port,Down_Pin) == 0)		//向下
	{
		if(Mario_Map[(Mario.Y+Mario.Height-8)/3][(Mario.X+4)/3] == 5)	//判断是否要下管道
		{
			//显示动画
			for(u8 i=1;i<8;i++)
			{
				OLED_ClearArea(Mario.X,Mario.Y,Mario.Width,Mario.Height);
				OLED_ShowImage(Mario.X,Mario.Y+i,Mario.Width,Mario.Height-i,Mario.Image);
				OLED_Update();
				Delay_ms(100);
			}
			Mario.X=10,Mario.Y=13;
			OLED_Clear();
			Pipeline_1();	//重置地图函数
			Round_flag++;	//回合判断++
		}
	}
}

//显示马里奥
void Show_Object()
{
	//按键
	Man_Control();
	//重力
	Man_Gravity(&Mario);
	//显示人物
	OLED_ShowImage(Mario.X,Mario.Y,Mario.Width,Mario.Height,Mario.Image);
}

//显示自动移动的物体
void Show_AutoObject(struct player* object)
{
	uint8_t tem;
	if((*object).life == 0)
		return;
	Object_Gravity(object);				//重力函数
	//判断是否撞墙
	if(Mario_Map[((*object).Y-7)/3][((*object).X+(*object).Width+1+Show_Offset)/3] != 0 ||
		Mario_Map[((*object).Y+(*object).Height-9)/3][((*object).X+(*object).Width+1+Show_Offset)/3] != 0||
		Mario_Map[((*object).Y-7)/3][((*object).X-Show_Offset)/3] != 0 || 
		Mario_Map[((*object).Y+(*object).Height-9)/3][((*object).X-Show_Offset)/3] != 0)
	{	
		if((*object).flag==5)	{(*object).life=0;OLED_ClearArea((*object).X,(*object).Y,6,6);}
		(*object).Speed = -(*object).Speed;
	}
	//如果两边都是墙就死亡
	if(Mario_Map[((*object).Y-7)/3][((*object).X-Show_Offset+2)/3]!=0 &&
		Mario_Map[((*object).Y-7)/3][((*object).X+(*object).Width+Show_Offset-1)/3] != 0)
	{
		(*object).life = 0;
		OLED_ClearArea((*object).X-1,(*object).Y-1,(*object).Width+3,(*object).Height+2);
		(*object).X = 119,(*object).Y = 51;
	}
	//当地图刷新时动态补偿
	if(Dynamic)
	{
		if((*object).Speed == -1)
		{
			OLED_ClearArea((*object).X+(*object).Width-2,(*object).Y,3,(*object).Height);//右消影
			(*object).X -= 2;		//移动
		}
		else if((*object).Speed == 1)
		{
			OLED_ClearArea((*object).X-1,(*object).Y,1,(*object).Height);				//左消影
		}
	}
	else
	{
		OLED_ClearArea((*object).X+(*object).Width-2,(*object).Y,4,(*object).Height);//右消影
		OLED_ClearArea((*object).X-1,(*object).Y,2,(*object).Height);				//左消影
		(*object).X += (*object).Speed;		//移动
	}
	if((*object).life)
	OLED_ShowImage((*object).X,(*object).Y,(*object).Width,(*object).Height,(*object).Image);	//显示图像
	tem = Object_Crash(&Mario,object);		//物体碰撞
	if(tem)
	{
		if((*object).flag == 1 && tem)		//吃到变大菇
		{									//置相关参数（小跳一下）
			Mario.Image=Mario_ManMax, Mario.Height=12, Jump_HighLimit = Mario.Y - 4, Mario.if_Falling=1;
			Mario.Width=10, Mario.life=2, (*object).life = 0,(*object).Speed=1;
			OLED_ClearArea((*object).X-1,(*object).Y-1,(*object).Width+2,(*object).Height+2);	//消除
		}
		//踩死乌龟
		else if((*object).flag==4 && tem==2)
		{
			OLED_ClearArea((*object).X-1,(*object).Y,(*object).Width+2,(*object).Height);		//消除
			(*object).X-=5,(*object).flag=5,(*object).Width=6,(*object).Speed=0,(*object).Image=Mario_ToroiseShell;//置相关参数
			Jump_HighLimit=Mario.Y-18,Mario.if_Falling=1;
		}
		//推龟壳
		else if((*object).flag==5 && tem)	
		{
			Mario_Grade+=20;
			(*object).Speed=Mario.Speed;
		}
		//碰到敌人
		else if(tem == 1)					
		{OLED_ClearArea((*object).X,(*object).Y,(*object).Width+1,(*object).Height);(*object).X += Mario.Speed*20;Game_Over();}
		//敌人被踩死
		else if(tem == 2)					
		{
			Mario_Grade+=20;
			OLED_ClearArea((*object).X-1,(*object).Y,(*object).Width+2,(*object).Height);
			(*object).life=0,(*object).X = 119,(*object).Y = 51;		//置相关参数
			Jump_HighLimit=Mario.Y-9,Mario.if_Falling=1;				//踩死敌人小跳一下
		}
		
	}
	if((*object).X < 3 || (*object).X>120)	//到边缘就死亡并重置相关参数
	{	
		(*object).life = 0;
		OLED_ClearArea((*object).X-1,(*object).Y-1,(*object).Width+3,(*object).Height+2);
		(*object).X = 119,(*object).Y = 51;
	}
	//龟壳碰撞敌人
	if((*object).flag==5&&(*object).Speed!=0)	
	{
		tem = Object_Crash(&Enemy_Aaric[0],object);		//敌人1物体碰撞
		if(tem)
		{
			OLED_ClearArea(Enemy_Aaric[0].X-1,Enemy_Aaric[0].Y-1,Enemy_Aaric[0].Width+2,Enemy_Aaric[0].Height+2);
			Enemy_Aaric[0].life=0,Enemy_Aaric[0].X=119,Enemy_Aaric[0].Y = 51;
		}
		tem = Object_Crash(&Enemy_Aaric[1],object);		//敌人2物体碰撞
		if(tem) 
		{
			OLED_ClearArea(Enemy_Aaric[1].X-1,Enemy_Aaric[1].Y-1,Enemy_Aaric[1].Width+2,Enemy_Aaric[1].Height+2);
			Enemy_Aaric[1].life=0,Enemy_Aaric[1].X=119,Enemy_Aaric[1].Y = 51;
		}
	}
}

//金币显示函数
void Show_Property(struct Property* object)
{
	uint8_t Hight_Limit = 0;
	if((*object).life==0)
	return;
	
	//重力部分		
	if((*object).if_Falling == 1)			//上升
	{	
		(*object).Y--;
		OLED_ClearArea((*object).X-1,(*object).Y,3,3);
		if((*object).Y<=(*object).H_Limit)	//到顶时
		{(*object).if_Falling=2;}
	}
	else if((*object).if_Falling == 2)		//下落时
	{
		OLED_ClearArea((*object).X-1,(*object).Y-1,3,2);
		(*object).Y++;
		if(Mario_Map[((*object).Y-5)/3][((*object).X)/3]!=0)			//是否下落到地板(并且消失)
		{
			(*object).if_Falling = 0,(*object).life=0;	//置相关参数
			OLED_ClearArea((*object).X-1,(*object).Y-2,3,3);
		}
	}
	//地图移动补偿
	if((*object).Move_flag != Show_Offset)
	{
		(*object).X--;
	}
	(*object).Move_flag = Show_Offset;
	if((*object).life)
	OLED_DrawCircle((*object).X,(*object).Y,1,OLED_FILLED);
}

void Mario_Play(void)
{
	Init_MapDate();
	while(1)
	{	
		Show_Object();					//显示马里奥人物
		Show_Map();								//显示地图
		Show_AutoObject(&Enemy_Aaric[0]);		//显示敌人蘑菇
		Show_AutoObject(&Enemy_Aaric[1]);		
		Show_AutoObject(&Enemy_Tortoise[0]);	//显示乌龟
		Show_AutoObject(&Enemy_Tortoise[1]);
		Show_AutoObject(&Plus_Aaric);			//显示放大菇
		Show_Property(&coin[0]);				//显示金币
		Show_Property(&coin[1]);
		Show_Property(&coin[2]);
		Show_Property(&coin[3]);
		Show_Property(&coin[4]);
		OLED_Update();
		if(MarioLife_Flag == 0)					//游戏结束时
		{
			OLED_Clear();
			while(1)
			{
				OLED_Printf(0,0,OLED_8X16,"Game Over!!!");
				OLED_Update();
			}
		}
	}
}

