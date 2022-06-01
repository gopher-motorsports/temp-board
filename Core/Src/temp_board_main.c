// temp_board_main.c
//  TODO DOCS

#include "temp_board_main.h"

// the HAL_CAN struct. This example only works for a single CAN bus
CAN_HandleTypeDef* example_hcan;


// Use this to define what module this board will be
#define THIS_MODULE_ID TEMP_BOARD_ID

// init
//  What needs to happen on startup in order to run GopherCAN
void init(CAN_HandleTypeDef* hcan_ptr)
{
	example_hcan = hcan_ptr;

	// initialize CAN
	// NOTE: CAN will also need to be added in CubeMX and code must be generated
	// Check the STM_CAN repo for the file "F0xx CAN Config Settings.pptx" for the correct settings
	if (init_can(example_hcan, THIS_MODULE_ID, BXTYPE_MASTER))
	{
		// an error occurred, give a blink code
		// TODO
	}

	// start the timer that will handle spi requests for the external ADCs
	// TODO

	// enable all of the variables in GopherCAN for testing
	set_all_params_state(TRUE);
}


// main_loop
//  TODO DOCS
void main_loop(void)
{
	while (1)
	{
		// update all of the temps from the averages converted in the timer interrupt
		// into GCAN with the GSense function
		// TODO

		// Send the min, max, and average temp to the BMS over the second CAN bus
		// TODO

		osDelay(MAIN_LOOP_DELAY_ms);
	}
}


// can_buffer_handling_loop
//  This loop will handle CAN RX software task and CAN TX hardware task. Should be
//  called every 1ms or as often as received messages should be handled
void can_buffer_handling_loop()
{
	// TODO this will be handled in DAM.c
	// handle each RX message in the buffer
	service_can_rx_buffer();

	// handle the transmission hardware for each CAN bus
	service_can_tx_hardware(example_hcan);
}


// end of temp_board_main.c
