#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"
#include "Mario.h"

int main(void)
{
	Key_Init();
	OLED_Init();
	while (1)
	{
		Mario_Play();
//		for(int8_t i=0;i<42;i++)
//		{
//			OLED_ShowImage(i*3,10,3,4,Mario_Grand);
//		}
//		OLED_Update();
	}
}
