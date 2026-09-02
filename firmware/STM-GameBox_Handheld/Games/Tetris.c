#include "stm32f10x.h"                  // Device header
#include <stdlib.h>
#include <stdbool.h>
#include "OLED.h"
#include "Key.h"
#include "Timer.h"
#include "Tetris.h"
#include "Game_Dion.h"
#include "Delay.h"
#include "Myflash.h"


//俄罗斯方块游戏的定义----------------------------------------

//边界的定义
const int16_t Border_x = 40;		//（40------76）
const int16_t Border_y = 2;			//（2-------62）
//显示下一个的区域
const int16_t Next_x = 86;			//（86------106）
const int16_t Next_y = 15;			//（15------35）

typedef struct  Point2
{
    uint16_t x;   //方块的坐标
    uint16_t y; 
}Point2;

Point2 CurrBlock[4]; // 当前方块信息
Point2 NextBlock[4]; // 下一个方块信息

int16_t Tetris_Map[20][12];

int16_t Tetirs_Grade;        //分数

int16_t TetrisCurrIndex;
int16_t TetrisNextIndex;
int16_t RotationsNum;

//按键
uint16_t KeyNum;


void GenerateNext(void)
{
    int16_t index;
    srand(rand());
    index = rand() % 7;

    TetrisNextIndex = index;
    switch (index)
    {
    case 0:	//长条
        NextBlock[0].x = 5;
        NextBlock[0].y = 0;

        NextBlock[1].x = 5;
        NextBlock[1].y = 1;

        NextBlock[2].x = 5;
        NextBlock[2].y = 2;

        NextBlock[3].x = 5;
        NextBlock[3].y = 3;
        break;
    case 1://正方形
        NextBlock[0].x = 5;
        NextBlock[0].y = 0;

        NextBlock[1].x = 6;
        NextBlock[1].y = 0;

        NextBlock[2].x = 5;
        NextBlock[2].y = 1;

        NextBlock[3].x = 6;
        NextBlock[3].y = 1;
        break;
    case 2://倒山形
        NextBlock[0].x = 5;
        NextBlock[0].y = 0;

        NextBlock[1].x = 4;
        NextBlock[1].y = 1;

        NextBlock[2].x = 5;
        NextBlock[2].y = 1;

        NextBlock[3].x = 6;
        NextBlock[3].y = 1;
        break;
    case 3://弯形
        NextBlock[0].x = 4;
        NextBlock[0].y = 0;

        NextBlock[1].x = 5;
        NextBlock[1].y = 0;

        NextBlock[2].x = 5;
        NextBlock[2].y = 1;

        NextBlock[3].x = 6;
        NextBlock[3].y = 1;
        break;

    case 4://弯形
        NextBlock[0].x = 5;
        NextBlock[0].y = 0;

        NextBlock[1].x = 6;
        NextBlock[1].y = 0;

        NextBlock[2].x = 4;
        NextBlock[2].y = 1;

        NextBlock[3].x = 5;
        NextBlock[3].y = 1;
        break;

    case 5://躺L形
        NextBlock[0].x = 6;
        NextBlock[0].y = 0;

        NextBlock[1].x = 4;
        NextBlock[1].y = 1;

        NextBlock[2].x = 5;
        NextBlock[2].y = 1;

        NextBlock[3].x = 6;
        NextBlock[3].y = 1;

        break;
    case 6://躺L形
        NextBlock[0].x = 4;
        NextBlock[0].y = 0;

        NextBlock[1].x = 4;
        NextBlock[1].y = 1;

        NextBlock[2].x = 5;
        NextBlock[2].y = 1;

        NextBlock[3].x = 6;
        NextBlock[3].y = 1;
        break;
    default:
        break;
    }
}

//更新当前方块
void UpdateCurr(void)
{
    TetrisCurrIndex = TetrisNextIndex;
    RotationsNum = 0;
    CurrBlock[0].x = NextBlock[0].x;
    CurrBlock[1].x = NextBlock[1].x;
    CurrBlock[2].x = NextBlock[2].x;
    CurrBlock[3].x = NextBlock[3].x;


    CurrBlock[0].y = NextBlock[0].y;
    CurrBlock[1].y = NextBlock[1].y;
    CurrBlock[2].y = NextBlock[2].y;
    CurrBlock[3].y = NextBlock[3].y;
  
}

//下落
void TetrisMoveDown(void)
{
	//消除上一个位置
	for(uint16_t i=0;i<4;i++)
		OLED_ClearArea(Border_x+(CurrBlock[i].x * 3),Border_y+(CurrBlock[i].y * 3),3,3);
    CurrBlock[0].y += 1;
    CurrBlock[1].y += 1;
    CurrBlock[2].y += 1;
    CurrBlock[3].y += 1;
	
}

//向左移动
void TetrisMoveLeft(void)
{
    //在最左边
    if(CurrBlock[0].x == 0 || CurrBlock[1].x == 0 || CurrBlock[2].x == 0 || CurrBlock[3].x == 0)
        return ;

    //左边没有方块
    else if(Tetris_Map[CurrBlock[0].y][CurrBlock[0].x -1] == 0 && Tetris_Map[CurrBlock[1].y][CurrBlock[1].x -1] == 0 && Tetris_Map[CurrBlock[2].y][CurrBlock[2].x -1] == 0 && Tetris_Map[CurrBlock[3].y][CurrBlock[3].x -1] == 0)
    {		
		//消除上一个位置
		for(uint16_t i=0;i<4;i++)
			OLED_ClearArea(Border_x+(CurrBlock[i].x * 3),Border_y+(CurrBlock[i].y * 3),3,3);
        CurrBlock[0].x -= 1;
        CurrBlock[1].x -= 1;
        CurrBlock[2].x -= 1;
        CurrBlock[3].x -= 1;
		
        TetrisDrawCurr();
    }
}
//向右移动
void TetrisMoveRight(void)
{
      //在最右边
    if(CurrBlock[0].x == 11 || CurrBlock[1].x ==11 || CurrBlock[2].x == 11 || CurrBlock[3].x == 11)
        return ;
    
    //右边没有方块
    else if(Tetris_Map[CurrBlock[0].y][CurrBlock[0].x + 1] == 0 && Tetris_Map[CurrBlock[1].y][CurrBlock[1].x + 1] == 0 && Tetris_Map[CurrBlock[2].y][CurrBlock[2].x + 1] == 0 && Tetris_Map[CurrBlock[3].y][CurrBlock[3].x + 1] == 0)
    {
		//消除上一个位置
		for(uint16_t i=0;i<4;i++)			
			OLED_ClearArea(Border_x+(CurrBlock[i].x * 3),Border_y+(CurrBlock[i].y * 3),3,3);
        CurrBlock[0].x += 1;
        CurrBlock[1].x += 1;
        CurrBlock[2].x += 1;
        CurrBlock[3].x += 1;
        TetrisDrawCurr();
    }
}

//消行
void Elimination(void)
{
    int i,j;
    for(i = 19 ; i >= 0; i--)
    {
        int value = 0;
        for(j=0;j<12;j++)
            value += Tetris_Map[i][j];
        if(value == 12)
        {
            int k;
            for(k = i ; k > 0; k--)
            {
                Tetris_Map[k][0] = Tetris_Map[k-1][0];
                Tetris_Map[k][1] = Tetris_Map[k-1][1];
                Tetris_Map[k][2] = Tetris_Map[k-1][2];
                Tetris_Map[k][3] = Tetris_Map[k-1][3];
                Tetris_Map[k][4] = Tetris_Map[k-1][4];
                Tetris_Map[k][5] = Tetris_Map[k-1][5];
                Tetris_Map[k][6] = Tetris_Map[k-1][6];
                Tetris_Map[k][7] = Tetris_Map[k-1][7];
                Tetris_Map[k][8] = Tetris_Map[k-1][8];
                Tetris_Map[k][9] = Tetris_Map[k-1][9];
				Tetris_Map[k][10] = Tetris_Map[k-1][10];
				Tetris_Map[k][11] = Tetris_Map[k-1][11];
            }
            Tetirs_Grade += 1; 									// 分数加一
			OLED_ShowNum(87,46,Tetirs_Grade,3,OLED_6X8);		//显示分数
            i++;  												//再次检测最后一行
			uint16_t i,j;
			for(i = 0 ; i< 20;i++)
			{
				for(j = 0 ;j <12 ;j++)
				{
					if(Tetris_Map[i][j] == 0)
					{   
						OLED_ClearArea(Border_x+(j *                                       3),Border_y+(i * 3),3,3);
					}
				}
			}
        }
    }
}

//map存储
int MapStroage(void)
{
    int flag = 0;
    //判断是否达到存储的条件
    if(CurrBlock[0].y == 19 || CurrBlock[1].y == 19 || CurrBlock[2].y == 19 || CurrBlock[3].y == 19)
    {
        flag = 1;
    }
    else if(Tetris_Map[CurrBlock[0].y + 1][CurrBlock[0].x] == 1 || Tetris_Map[CurrBlock[1].y + 1][CurrBlock[1].x] == 1 || Tetris_Map[CurrBlock[2].y + 1][CurrBlock[2].x] == 1 || Tetris_Map[CurrBlock[3].y + 1][CurrBlock[3].x] == 1)
    {
        flag = 1;
    }

    if(flag == 1)
    {
        Tetris_Map[CurrBlock[0].y][CurrBlock[0].x] = 1;
        Tetris_Map[CurrBlock[1].y][CurrBlock[1].x] = 1;
        Tetris_Map[CurrBlock[2].y][CurrBlock[2].x] = 1;
        Tetris_Map[CurrBlock[3].y][CurrBlock[3].x] = 1;
    }

    return flag;
}


void RotateJudge(uint8_t Num)
{
    int RNum = RotationsNum;
    uint16_t flag = 1;

    Point2 CBlock[4];

    CBlock[0] = CurrBlock[0];
    CBlock[1] = CurrBlock[1];
    CBlock[2] = CurrBlock[2];
    CBlock[3] = CurrBlock[3];
	//消除上一个位置
	uint8_t i;
	for(i=0;i<4;i++)			
		OLED_ClearArea(Border_x+(CurrBlock[i].x * 3),Border_y+(CurrBlock[i].y * 3),3,3);
	if(Num == 5)
		for(i=0;i<3;i++)
			Rotate();
	else if(Num == 6)
		Rotate();
    for(i=0;i<4;i++)
   { 
        if(CurrBlock[i].x < 0 || CurrBlock[i].x > 11)
        {
            flag = 0;
            break;
        }
        else if(CurrBlock[i].y < 0 || CurrBlock[i].y > 19)
        {
            flag = 0;
            break;
        }
        else if(Tetris_Map[CurrBlock[i].y][CurrBlock[i].x] == 1)
        {
            flag = 0;
            break;
        }
   }

   if(flag == 0)  //旋转失败
   {
        CurrBlock[0] = CBlock[0];
        CurrBlock[1] = CBlock[1];
        CurrBlock[2] = CBlock[2];
        CurrBlock[3] = CBlock[3];
        RotationsNum = RNum;
   }
    TetrisDrawCurr();

}


//旋转
void Rotate(void)
{

    if(TetrisCurrIndex == 0)
    {
        if(RotationsNum == 0 || RotationsNum == 2)
        {
            CurrBlock[0].x += 2;
            CurrBlock[0].y += 2;

            CurrBlock[1].x += 1;
            CurrBlock[1].y += 1;

            CurrBlock[2].x = CurrBlock[2].x;
            CurrBlock[2].y = CurrBlock[2].y;

            CurrBlock[3].x -= 1;
            CurrBlock[3].y -= 1;
        }
        else if(RotationsNum == 1 || RotationsNum == 3)
        {
            CurrBlock[0].x -= 2;
            CurrBlock[0].y -= 2;

            CurrBlock[1].x -= 1;
            CurrBlock[1].y -= 1;

            CurrBlock[2].x = CurrBlock[2].x;
            CurrBlock[2].y = CurrBlock[2].y;

            CurrBlock[3].x += 1;
            CurrBlock[3].y += 1;
        }
       
    }
    else if(TetrisCurrIndex == 1)
    {
        //方块不用动
    }
    else if(TetrisCurrIndex == 2)
    {
         if(RotationsNum == 0)
        {
            CurrBlock[0].x += 1;
            CurrBlock[0].y += 1;

            CurrBlock[1].x += 1;
            CurrBlock[1].y -= 1;

            CurrBlock[2].x = CurrBlock[2].x;
            CurrBlock[2].y = CurrBlock[2].y;

            CurrBlock[3].x -= 1;
            CurrBlock[3].y += 1;
        }
        else if(RotationsNum == 1)
        {
            CurrBlock[0].x -= 1;
            CurrBlock[0].y += 1;

            CurrBlock[1].x += 1;
            CurrBlock[1].y += 1;

            CurrBlock[2].x = CurrBlock[2].x;
            CurrBlock[2].y = CurrBlock[2].y;

            CurrBlock[3].x -= 1;
            CurrBlock[3].y -= 1;
        }
        else if(RotationsNum == 2)
        {
            CurrBlock[0].x -= 1;
            CurrBlock[0].y -= 1;

            
            CurrBlock[1].x -= 1;
            CurrBlock[1].y += 1;

            CurrBlock[2].x = CurrBlock[2].x;
            CurrBlock[2].y = CurrBlock[2].y;

            CurrBlock[3].x += 1;
            CurrBlock[3].y -= 1;

        }
        else{
            CurrBlock[0].x += 1;
            CurrBlock[0].y -= 1;
            
            CurrBlock[1].x -= 1;
            CurrBlock[1].y -= 1;

            CurrBlock[2].x = CurrBlock[2].x;
            CurrBlock[2].y = CurrBlock[2].y;

            CurrBlock[3].x += 1;
            CurrBlock[3].y += 1;
        }
    }
    else if(TetrisCurrIndex == 3)
    {
         if(RotationsNum == 0 || RotationsNum == 2)
        {
            CurrBlock[0].x += 1;
            CurrBlock[0].y -= 1;


            CurrBlock[1].x = CurrBlock[1].x;
            CurrBlock[1].y = CurrBlock[1].y;
            
            CurrBlock[2].x -= 1;
            CurrBlock[2].y -= 1;

            CurrBlock[3].x -= 2;
            CurrBlock[3].y  =  CurrBlock[3].y;
        }
        else if(RotationsNum == 1 || RotationsNum == 3)
        {
            CurrBlock[0].x -= 1;
            CurrBlock[0].y += 1;


            CurrBlock[1].x = CurrBlock[1].x;
            CurrBlock[1].y = CurrBlock[1].y;
            
            CurrBlock[2].x += 1;
            CurrBlock[2].y += 1;

            CurrBlock[3].x += 2;
            CurrBlock[3].y  =  CurrBlock[3].y;
        }

    }
    else if(TetrisCurrIndex == 4)
    {
         if(RotationsNum == 0 || RotationsNum == 2)
        {
            CurrBlock[0].x += 1;
            CurrBlock[0].y += 1;


            CurrBlock[1].x = CurrBlock[1].x;
            CurrBlock[1].y += 2;

            CurrBlock[2].x += 1;
            CurrBlock[2].y -= 1;

            CurrBlock[3].x  = CurrBlock[3].x;
            CurrBlock[3].y  = CurrBlock[3].y;


        }
        else if(RotationsNum == 1 || RotationsNum == 3)
        {
            CurrBlock[0].x -= 1;
            CurrBlock[0].y -= 1;


            CurrBlock[1].x  = CurrBlock[1].x;
            CurrBlock[1].y -= 2;

            CurrBlock[2].x -= 1;
            CurrBlock[2].y += 1;

            CurrBlock[3].x  = CurrBlock[3].x;
            CurrBlock[3].y  = CurrBlock[3].y;
        }
    }
    else if(TetrisCurrIndex == 5)
    {
         if(RotationsNum == 0)
        {
            CurrBlock[0].x += 1;
            CurrBlock[0].y += 1;


            CurrBlock[1].x += 2;
            CurrBlock[1].y -= 2;

            CurrBlock[2].x += 1;
            CurrBlock[2].y -= 1;

            CurrBlock[3].x = CurrBlock[3].x;
            CurrBlock[3].y = CurrBlock[3].y;
        }
        else if(RotationsNum ==1)
        {
            CurrBlock[0].x -= 1;
            CurrBlock[0].y += 1;


            CurrBlock[1].x += 2;
            CurrBlock[1].y += 2;

            CurrBlock[2].x += 1;
            CurrBlock[2].y += 1;

            CurrBlock[3].x = CurrBlock[3].x;
            CurrBlock[3].y = CurrBlock[3].y;
        }
        else if(RotationsNum == 2)
        {
            CurrBlock[0].x -= 1;
            CurrBlock[0].y -= 1;


            CurrBlock[1].x -= 2;
            CurrBlock[1].y += 2;

            CurrBlock[2].x -= 1;
            CurrBlock[2].y += 1;

            CurrBlock[3].x = CurrBlock[3].x;
            CurrBlock[3].y = CurrBlock[3].y;
        }
        else{
            CurrBlock[0].x += 1;
            CurrBlock[0].y -= 1;


            CurrBlock[1].x -= 2;
            CurrBlock[1].y -= 2;

            CurrBlock[2].x -= 1;
            CurrBlock[2].y -= 1;

            CurrBlock[3].x = CurrBlock[3].x;
            CurrBlock[3].y = CurrBlock[3].y;
        }
    }
    else if(TetrisCurrIndex == 6)
    {
         if(RotationsNum == 0)
        {
            CurrBlock[0].x += 1;
            CurrBlock[0].y += 1;

            CurrBlock[1].x = CurrBlock[1].x;
            CurrBlock[1].y = CurrBlock[1].y;

            CurrBlock[2].x -= 1;
            CurrBlock[2].y += 1;

            CurrBlock[3].x -= 2;
            CurrBlock[3].y += 2;
        }
        else if(RotationsNum == 1)
        {
            CurrBlock[0].x -= 1;
            CurrBlock[0].y += 1;

            CurrBlock[1].x = CurrBlock[1].x;
            CurrBlock[1].y = CurrBlock[1].y;

            CurrBlock[2].x -= 1;
            CurrBlock[2].y -= 1;

            CurrBlock[3].x -= 2;
            CurrBlock[3].y -= 2;
        }
        else if(RotationsNum == 2)
        {
            CurrBlock[0].x -= 1;
            CurrBlock[0].y -= 1;

            CurrBlock[1].x = CurrBlock[1].x;
            CurrBlock[1].y = CurrBlock[1].y;

            CurrBlock[2].x += 1;
            CurrBlock[2].y -= 1;

            CurrBlock[3].x += 2;
            CurrBlock[3].y -= 2;
        }
        else{
            CurrBlock[0].x += 1;
            CurrBlock[0].y -= 1;

            CurrBlock[1].x = CurrBlock[1].x;
            CurrBlock[1].y = CurrBlock[1].y;

            CurrBlock[2].x += 1;
            CurrBlock[2].y += 1;

            CurrBlock[3].x += 2;
            CurrBlock[3].y += 2;
        }
    }



    if(RotationsNum == 3)
        RotationsNum = 0;
    else
        RotationsNum += 1;
}

//游戏初始化
void TetrisInit(void)
{
    int i,j;
    for(i=0;i<20;i++)
        for(j = 0;j<12;j++)
            Tetris_Map[i][j] = 0;
    GenerateNext();												//生成下一个方块信息
    UpdateCurr();												//更新当前方块
    GenerateNext();												//生成下一个方块信息
    Tetirs_Grade = 0;											//分数
	OLED_DrawRectangle(86,15,20,20,OLED_UNFILLED);				//绘制下一个区域
	OLED_DrawRectangle(86,40,20,20,OLED_UNFILLED);				//绘制分数区域
    TetrisDrawNext();  											//绘制下一个
	OLED_ShowNum(87,46,Tetirs_Grade,3,OLED_6X8);				//显示分数
}

//游戏开始
void TetrisGame(void)
{   
    if(MapStroage() == 1)						//落地后
    {
        Elimination();							//消行                                
		//消除上一个位置
		for(uint16_t i=0;i<4;i++)
			OLED_ClearArea(Border_x+(CurrBlock[i].x * 3),Border_y+(CurrBlock[i].y * 3),3,3);
        UpdateCurr();							//更行方块
        GenerateNext();							//生成下一个方块     
        TetrisDrawNext();  						//绘制下一个
        TeTrisDrawMap();   						//绘制落地区域
    }
    else
    {
        TetrisMoveDown();						//下落
        TetrisDrawCurr();  						//绘制当前
    }
    //TeTrisDrawMap();   //绘制游戏区域
    //TetrisDrawCurr();  //绘制当前
}   

//游戏结束
//void TetrisGameOver(void)
//{
//    uint16_t value = 0 , i;
//    for(i = 0 ; i< 12; i ++)
//        value += Tetris_Map[0][i];
//    if(value != 0)
//    {
//		OLED_Clear();
//		OLED_ShowString(48,20,"GameOver",OLED_8X16);
//		OLED_Update();
//		while(1)
//		{
//			if(Key_Enter_Get())	{break;}			//确定键返回
//			if(Key_Back_Get())	{break;}			//返回键返回
//		}
//    }
//}



//绘制游戏map
void TeTrisDrawMap(void)
{
    uint16_t i,j;
    for(i = 0 ; i< 20;i++)
    {
        for(j = 0 ;j <12 ;j++)
        {
            if(Tetris_Map[i][j] == 1)
            {   
				OLED_DrawRectangle(Border_x+(j * 3),Border_y+(i * 3),3,3,OLED_UNFILLED);
            }
        }
    }
}

//显示下一个
void TetrisDrawNext(void)
{
	uint16_t i;
	//消除前一个	
	OLED_ClearArea(Next_x+1,Next_y+1,18,18);	
	for(i=0;i<4;i++)
	{
		OLED_DrawRectangle(Next_x+(NextBlock[i].x * 2)-3,Next_y+(NextBlock[i].y * 2)+5,3,3,OLED_UNFILLED);
	}
}
//显示当前图形
void TetrisDrawCurr(void)
{
    uint16_t i;
	for(i=0;i<4;i++)
	{
		
		OLED_DrawRectangle(Border_x+(CurrBlock[i].x * 3),Border_y+(CurrBlock[i].y * 3),3,3,OLED_UNFILLED);
	}
}

void Tetris_start()
{
	OLED_Clear();
	uint16_t value = 0;										//判断游戏是否结束相关
	TetrisInit();	
	TimerLength = 500;										//定时器中断刷新时长
	Timer_500ms = 1;										//定时器中断刷新开启
	OLED_DrawRectangle(39,2,38,61,OLED_UNFILLED);  			//绘制墙
	while(1)
	{
		KeyNum = Get_LeftKeyNum();
		if(KeyNum == 1)										//左按键向左
		{
			TetrisMoveRight();
			KeyNum = 0;			
		}
		else if(KeyNum == 2)								//右按键向右
		{
			TetrisMoveLeft();
			KeyNum = 0;				
		}
		
		KeyNum = Get_RighKeyNum();							//控制方块旋转
		if(KeyNum != 0)
		{
			RotateJudge(KeyNum);
			KeyNum = 0;	
		}
		
		KeyNum = Get_RollKeyNum_Tetris();
		if(KeyNum == 4)										//下按键向下
		{			
			TetrisGame();			
			KeyNum = 0;	
		}
		
		if(RefreshFlag == 1)								//刷新
		{			
			TetrisGame();
			RefreshFlag = 0;
		}
		//OLED_DrawRectangle(39,2,38,61,OLED_UNFILLED);  			//绘制墙
		OLED_DrawLine(40,2,74,2);							//补绘墙
		OLED_Update();
		//判断游戏是否结束
		//TetrisGameOver();	
		for(uint16_t i = 0 ; i< 12; i ++)
			value += Tetris_Map[0][i];
		if(value != 0)
		{
			Timer_500ms = 0;									//定时器中断关闭
			if(Tetirs_Grade > Store_Data[7])
			{
				Store_Data[7] = Tetirs_Grade;					//利用flash存储最高分数
				Store_Save();									//将store_Data写入flash中
			}		
			OLED_Clear();
			//显示游戏结算界面
			OLED_ShowImage(0,0,60,55,bmp_dog);	
			OLED_Printf(61, 0, OLED_8X16, "游戏结束");
			OLED_Printf(61, 20, OLED_8X16, "得分: ");
			OLED_Printf(61, 40, OLED_8X16, "最高: ");
			OLED_ShowNum(103,26,Tetirs_Grade,3,OLED_6X8);
			OLED_ShowNum(103,46,Store_Data[7],3,OLED_6X8);
			OLED_Update();
			while(1)
			{
				if(Key_Enter_Get())	{Timer_500ms = 0;break;}			//确定键返回
				if(Key_Back_Get())	{Timer_500ms = 0;break;}			//返回键返回
			}
			return;
		}
		if(Key_Enter_Get())	{Timer_500ms = 0;return;}			//确定键返回
		if(Key_Back_Get())	{Timer_500ms = 0;return;}			//返回键返回
		//Delay_ms(500);
	}
}
