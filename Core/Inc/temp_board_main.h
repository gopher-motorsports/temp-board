// temp_board_main.h
//  Header file for temp_board_main.c

#ifndef TEMP_BOARD_MAIN_H
#define TEMP_BOARD_MAIN_H

#include "GopherCAN.h"

#define MAIN_LOOP_DELAY_ms 10

void init(CAN_HandleTypeDef* hcan_ptr);
void can_buffer_handling_loop();
void main_loop();

#endif
