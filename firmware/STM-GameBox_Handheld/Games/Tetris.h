#ifndef __TETRIS_H
#define __TETRIS_h

void GenerateNext(void);
void UpdateCurr(void);
void TetrisMoveDown(void);
void TetrisMoveLeft(void);
void TetrisMoveRight(void);
void Elimination(void);
int MapStroage(void);

void Rotate(void);
void RotateJudge(uint8_t Num);

void TetrisGame(void);
void TetrisInit(void);

void TetrisDrawWall(void);
void TetrisDrawNext(void);
void TetrisDrawCurr(void);                                                                                                                            
void TeTrisDrawMap(void);

void Tetris_start(void);

//定时起中的状态
//enum State{
//	neutral =  10;
//	Dino   	=  1
//	Tetris 	=  2,
//	Snake 	=  3,
//	_2048 	=  4,
//};

//extern enum State CurrMode; //当前模式

#endif
