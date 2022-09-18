// temp_board_main.c
//  TODO DOCS

#include "temp_board_main.h"
#include "spi_adc.h"
#include "cmsis_os.h"
#include "main.h"
#include "DAM.h"
#include <stdbool.h>

static float adc_to_temp(U16 adc, float last_val);
static bool should_ignore_temp(FLOAT_CAN_STRUCT* param);
static void send_temps_to_bms(void);

// the HAL_CAN struct. This example only works for a single CAN bus
CAN_HandleTypeDef* example_hcan;
CAN_HandleTypeDef* bms_can;

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi3;
extern SPI_HandleTypeDef hspi4;
extern SPI_HandleTypeDef hspi5;
extern SPI_HandleTypeDef hspi6;

extern TIM_HandleTypeDef htim14;

#define TIMER_PSC 16

// BMS CAN Defines
#define BMS_MSG_ID 0x1839F380
#define BMS_MSG_LEN 8

// Use this to define what module this board will be
#define THIS_MODULE_ID TEMP_BOARD_ID

#define FIRST_BAT_STRUCT_ID (bat_0_temp.param_id)
#define NUMBER_OF_BATS 88
#define LAST_BAT_STRUCT_ID (FIRST_BAT_STRUCT_ID + NUMBER_OF_BATS)

U16 num_ignored_temps = 0;

float table_voltages[] = {
		1.30,
		1.31,
		1.32,
		1.33,
		1.34,
		1.35,
		1.37,
		1.38,
		1.4,
		1.43,
		1.45,
		1.48,
		1.51,
		1.55,
		1.59,
		1.63,
		1.68,
		1.74,
		1.8,
		1.86,
		1.92,
		1.99,
		2.05,
		2.11,
		2.17,
		2.23,
		2.27,
		2.32,
		2.35,
		2.38,
		2.4,
		2.42,
		2.44
};

float table_temps[] = {
		120.0,
		115.0,
		110.0,
		105.0,
		100.0,
		95.0,
		90.0,
		85.0,
		80.0,
		75.0,
		70.0,
		65.0,
		60.0,
		55.0,
		50.0,
		45.0,
		40.0,
		35.0,
		30.0,
		25.0,
		20.0,
		15.0,
		10.0,
		5.0,
		0.0,
		-5.0,
		-10.0,
		-15.0,
		-20.0,
		-25.0,
		-30.0,
		-35.0,
		-40.0
};
#define NUM_TABLE_VALUES 33

// init
//  What needs to happen on startup in order to run GopherCAN
void init(CAN_HandleTypeDef* hcan_ptr, CAN_HandleTypeDef* hcan2_ptr)
{
	example_hcan = hcan_ptr;
	bms_can = hcan2_ptr;

	// initialize CAN
	// NOTE: CAN will also need to be added in CubeMX and code must be generated
	// Check the STM_CAN repo for the file "F0xx CAN Config Settings.pptx" for the correct settings
	if (init_can(example_hcan, THIS_MODULE_ID, BXTYPE_MASTER))
	{
		// an error occurred, give a blink code
		// TODO
	}
	// this only sends, it does not produce anything in gsense
	DAM_init(example_hcan, bms_can, NULL, NULL, NULL, NULL, GSENSE_LED_GPIO_Port, GSENSE_LED_Pin);

	// enable all of the variables in GopherCAN for testing
	set_all_params_state(TRUE);

	// start the timer that will handle spi requests for the external ADCs
	init_timer(&htim14, TIMER_PSC);
	define_spi_bus(&hspi1, 0);
	define_spi_bus(&hspi3, 1);
	define_spi_bus(&hspi4, 2);
	define_spi_bus(&hspi5, 3);
	define_spi_bus(&hspi6, 4);

	HAL_GPIO_WritePin(HBEAT_LED_GPIO_Port, HBEAT_LED_Pin, GPIO_PIN_SET);
}


// main_loop
//  TODO DOCS
void main_loop(void)
{
	start_spi_collection();

	while (1)
	{
		U16 curr_can_struct_id = FIRST_BAT_STRUCT_ID;
		FLOAT_CAN_STRUCT* curr_can_struct;
		float val;
		float minimum = 119.0;
		float maximum = -39.0;
		double total = 0;
		U32 count = 0;
		num_ignored_temps = 0;

		// update all of the temps from the averages converted in the timer interrupt
		// into GCAN with the GSense function
		for (U8 bus = 0;
			 bus < NUMBER_OF_BUSSES && curr_can_struct_id < LAST_BAT_STRUCT_ID;
			 bus++)
		{
			for (U8 chip = 0;
				 chip < chips_per_bus[bus] && curr_can_struct_id < LAST_BAT_STRUCT_ID;
				 chip++)
			{
				for (U8 channel = 0;
					 channel < CHANNELS_PER_CHIP && curr_can_struct_id < LAST_BAT_STRUCT_ID;
					 channel++)
				{
					// get the data and put it into this CAN struct
					curr_can_struct = (FLOAT_CAN_STRUCT*)all_parameter_structs[curr_can_struct_id];
					val = adc_to_temp(get_channel_average(bus, chip, channel), curr_can_struct->data);
					update_and_queue_param_float(curr_can_struct, val);

					// move on to the next one
					curr_can_struct_id++;

					// if this channel should not be ignored, use it for the min, max, and ave
					if (!should_ignore_temp(curr_can_struct))
					{
						if (curr_can_struct->data < minimum) minimum = curr_can_struct->data;
						if (curr_can_struct->data > maximum) maximum = curr_can_struct->data;
						total += curr_can_struct->data;
						count++;
					}
					else
					{
						num_ignored_temps++;
					}
				}
			}
		}

		// Send the min, max, and average temp to the BMS over the second CAN bus
		update_and_queue_param_float(&bat_ave_temp, (total / count));
		update_and_queue_param_float(&bat_min_temp, minimum);
		update_and_queue_param_float(&bat_max_temp, maximum);

		// send to BMS every 100ms
		static U32 bms_send_count = 0;
		if (bms_send_count++ >= 10)
		{
			send_temps_to_bms();
			bms_send_count = 0;
		}

		// toggle the LED
		static U32 counter = 0;
		if (counter++ >= 100)
		{
			HAL_GPIO_TogglePin(HBEAT_LED_GPIO_Port, HBEAT_LED_Pin);
			counter = 0;
		}

		osDelay(MAIN_LOOP_DELAY_ms);
	}
}


// adc_to_temp
//  takes in a 16bit ADC value and converts into a temperature in C
static float adc_to_temp(U16 adc, float last_val)
{
	float voltage;

	// convert to a [-5.12, 5.12]
	voltage = (((float)adc / (1 << 16))*10.24 - 5.12) * 2;

	// make sure the values make sense
	if (voltage < -0.2 || voltage > 3.5) return last_val;

	// interpolate into a temperature
	if (voltage < table_voltages[0]) return table_temps[0];
	if (voltage > table_voltages[NUM_TABLE_VALUES-1]) return table_temps[NUM_TABLE_VALUES-1];

	for (U16 i = 0; i < NUM_TABLE_VALUES-2; i++)
	{
		float x0 = table_voltages[i];
		float y0 = table_temps[i];
		float x1 = table_voltages[i+1];
		float y1 = table_temps[i+1];

		if (voltage >= x0 && voltage <= x1)
		{
			return ((y0 * (x1 - voltage)) + (y1 * (voltage - x0))) / (x1 - x0);
		}
	}

	// not sure how we got here
	return voltage;
}


// should_ignore_temp
//  pass in a temp param and this function will check if it is one that
//  should be ignored. Returns true if it should be ignored, false otherwise
static bool should_ignore_temp(FLOAT_CAN_STRUCT* param)
{
	return (param->data < -39.0 || param->data > 119.0);
}


// send_temps_to_bms
//  Send the min, max, and average temps to the BMS over CAN
static void send_temps_to_bms(void)
{
	static U32 num_errors = 0;
	CAN_TxHeaderTypeDef tx_header;
	U32 tx_mailbox_num;
	U8 bms_msg[BMS_MSG_LEN];
	U8 checksum = 0;
	S8 temp;
	U8 c;

	// build the can message
	tx_header.IDE = CAN_ID_EXT;
	tx_header.TransmitGlobalTime = DISABLE;
	tx_header.RTR = 0;
	tx_header.ExtId = BMS_MSG_ID;
	tx_header.DLC = BMS_MSG_LEN;

	// byte 0 is the TEM number
	bms_msg[0] = 1;

	// byte 1 is the lowest temp, S8 form
	temp = (S8)bat_min_temp.data;
	bms_msg[1] = temp;

	// byte 2 is the highest temp, S8 form
	temp = (S8)bat_max_temp.data;
	bms_msg[2] = temp;

	// byte 3 is the average temp, S8 form
	temp = (S8)bat_ave_temp.data;
	bms_msg[3] = temp;

	// byte 4 is number of thermistors
	bms_msg[4] = 3;

	// byte 5 is highest thermistor id
	bms_msg[5] = 2;

	// byte 6 is lowest thermistor id
	bms_msg[6] = 0;

	// byte 7 is the checksum
	checksum = 0;
	checksum += 0x39;
	checksum += BMS_MSG_LEN;
	for (c = 0; c < BMS_MSG_LEN - 1; c++)
	{
		checksum += bms_msg[c];
	}
	bms_msg[7] = checksum;

	//if (HAL_CAN_AddTxMessage(bms_can, &tx_header, bms_msg, &tx_mailbox_num) != HAL_OK)
	if (HAL_CAN_AddTxMessage(example_hcan, &tx_header, bms_msg, &tx_mailbox_num) != HAL_OK)
	{
		num_errors++;
	}
}


// end of temp_board_main.c
