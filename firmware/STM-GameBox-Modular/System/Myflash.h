#ifndef __MYFLASH_H
#define __MYFLASH_H

/*
	*Store_Data[1]：记录光标状态
		0x0001为反相；
		0x0002为鼠标；
		0x0003为矩形；
		0x0004为爱心
	*Store_Data[2]：记录光标速度
	*Store_Data[3]：记录是否静音(0002为静音)
		
	*Store_Data[4]：记录恐龙游戏最高分数
	*Store_Data[5]：记录贪吃蛇最高分数
	*Store_Data[6]：记录飞机大战最高分数
	
	*Store_Data[7]：记录时钟年份
	*Store_Data[8]：记录时钟月份
	*Store_Data[9]：记录时钟日期
	*Store_Data[10]：记录时钟小时
	*Store_Data[11]：记录时钟分钟
	*Store_Data[12]：记录时钟秒数
*/
extern uint16_t Store_Data[];



//单独对flash操作（root）
//uint32_t MyFLASH_ReadWord(uint32_t Address);
//uint16_t MyFLASH_ReadHalfWord(uint32_t Address);
//uint8_t MyFLASH_ReadByte(uint32_t Address);
//void MyFLASH_EraseAllPages(void);
//void MyFLASH_ErasePage(uint32_t PageAddress);
//void MyFLASH_ErasePage(uint32_t PageAddress);
//void MyFLASH_ProgramWord(uint32_t Address, uint32_t Data);
//void MyFLASH_ProgramHalfWord(uint32_t Address, uint16_t Data);

//使用缓冲区进行读写
void Store_Init(void);
void Store_Save(void);
void Store_Clear(void);
#endif
