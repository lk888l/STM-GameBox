#ifndef __OLED_DATA_H
#define __OLED_DATA_H

#include <stdint.h>

/*中文字符字节宽度*/
#define OLED_CHN_CHAR_WIDTH			3		//UTF-8编码格式给3，GB2312编码格式给2

/*字模基本单元*/
typedef struct 
{
	uint8_t Data[32];						//字模数据
	char Index[OLED_CHN_CHAR_WIDTH + 1];	//汉字索引
} ChineseCell_t;

/*ASCII字模数据声明*/
extern const uint8_t OLED_F8x16[][16];
extern const uint8_t OLED_F6x8[][6];

/*汉字字模数据声明*/
extern const ChineseCell_t OLED_CF16x16[];

/*图像数据声明*/
extern const uint8_t Diode[];
/*按照上面的格式，在这个位置加入新的图像数据声明*/
extern const uint8_t Cursor[];

extern const uint8_t groud[];
extern const uint8_t OLED_Cloud[1][27];
extern const uint8_t OLED_Dino[2][16][16];
extern const uint8_t OLED_Dino_Jump[][5][16];
extern const uint8_t OLED_Cactus1[2][8];
extern const uint8_t OLED_Cactus2[2][16];
extern const uint8_t OLED_Cactus3[][16];


extern const uint8_t Wallpaper[];
extern const uint8_t bmp_dog[];
extern const uint8_t Img_MyAir[];
extern const uint8_t Img_EnmyAir[];
extern const uint8_t Img_Bullet[];
extern const uint8_t aixin[];
extern const uint8_t Image_Piarprop[];
extern const uint8_t Image_PiarAddBat[];
//...
	
#endif


/*****************江协科技|版权所有****************/
/*****************jiangxiekeji.com*****************/
