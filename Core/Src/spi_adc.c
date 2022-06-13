// spi_adc.c
//  Lib to handle geting data of the ADS868xA 16-Bit ADC with SPI


#include "spi_adc.h"
#include "main.h"

#define TIM_CLOCK_BASE_FREQ (HAL_RCC_GetPCLK1Freq() << 1) // APB1 Timer clock = PCLK1 * 2
#define TIM_MAX_VAL 65536

// SPI command stuff
#define CHAN_REQ_SIZE 1
const U16 AUTO_RESET_CMD = 0x8500;

// array to store how many chips in each bus
volatile U16 chips_per_bus[] = {
		BUS_0_CHIPS,
		BUS_1_CHIPS,
		BUS_2_CHIPS,
		BUS_3_CHIPS,
		BUS_4_CHIPS
};

// DMA buffers for each channel
volatile U16 bus_0_buffer[BUS_0_CHIPS*CHANNELS_PER_CHIP];
volatile U16 bus_1_buffer[BUS_1_CHIPS*CHANNELS_PER_CHIP];
volatile U16 bus_2_buffer[BUS_2_CHIPS*CHANNELS_PER_CHIP];
volatile U16 bus_3_buffer[BUS_3_CHIPS*CHANNELS_PER_CHIP];
volatile U16 bus_4_buffer[BUS_4_CHIPS*CHANNELS_PER_CHIP];

// way to access the buffers with an index
volatile U16* bus_buffers[] = {
		bus_0_buffer,
		bus_1_buffer,
		bus_2_buffer,
		bus_3_buffer,
		bus_4_buffer
};

// way to store the commands to request each channel
volatile U16 channel_req_cmds[] = {
		 0xC000,
		 0xC400,
		 0xC800,
		 0xCC00,
		 0xD000,
		 0xD400,
		 0xD800,
		 0xDC00,
};

// final array for each of the averaged value
// chip0chan0, chip0chan1, chip0chan2, ..., chip1chan0, chip1chan1, ..., chipLASTchan7
volatile U16 channel_averages[TOTAL_CHIPS*CHANNELS_PER_CHIP];

// all of the handles
SPI_HandleTypeDef* hspi_ptr_arr[NUMBER_OF_BUSSES];
TIM_HandleTypeDef* tim_ptr;

// tracks the current channel that has been requested
volatile U8 last_input_channel[NUMBER_OF_BUSSES] = { 0 };
volatile U8 current_input_channel[NUMBER_OF_BUSSES] = { 0 };
U16 tx_buffer;


// define_spi_bus
//  Input a spi handle and define which bus it is
SPI_ADC_ERR define_spi_bus(SPI_HandleTypeDef* hspi, U8 bus_number)
{
	if (bus_number >= NUMBER_OF_BUSSES) return SPI_BUS_OOB;
	hspi_ptr_arr[bus_number] = hspi;
	return SPI_OK;
}


// init_timer
//  Setup the timer to start running at a set frequency. Timer 14 is recommended
SPI_ADC_ERR init_timer(TIM_HandleTypeDef* htim, U16 psc)
{
	if (!htim) return SPI_NULL_INPUT;

	tim_ptr = htim;
	__HAL_TIM_DISABLE(htim);
	__HAL_TIM_SET_COUNTER(htim, 0);
	U32 reload;
	do {
		reload = (U32)((TIM_CLOCK_BASE_FREQ/psc) / (SAMPLING_FRQ*2));
		psc <<= 1;
	} while (reload > TIM_MAX_VAL);

	__HAL_TIM_SET_PRESCALER(htim, psc);
	__HAL_TIM_SET_AUTORELOAD(htim, reload);
	__HAL_TIM_ENABLE(htim);

	return SPI_OK;
}


// start_spi_collection
//  Start collecting data by starting the timer interrupt
void start_spi_collection(void)
{
	U8 c;

	// pull the chip select low
	HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);

	// send the AUTO_RESET command to each of the chips
	for (c = 0; c < NUMBER_OF_BUSSES; c++)
	{
		HAL_SPI_Transmit(hspi_ptr_arr[c], (U8*)&AUTO_RESET_CMD, CHAN_REQ_SIZE, 100);
	}

	// pull the chip select high
	HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);

	// start the timer
	HAL_TIM_Base_Start_IT(tim_ptr);
}


// get_channel_average
//  return the average of a channel based on the bus, chip, and channel
U16 get_channel_average(U8 bus, U8 chip, U8 channel)
{
	U16 channel_offset = 0;
	U16 c;

	// get the correct offset based on what bus is being accessed
	if (bus >= NUMBER_OF_BUSSES) return 0;
	for (c = 0; c < bus; c++)
	{
		channel_offset += CHANNELS_PER_CHIP*chips_per_bus[c];
	}

	return channel_averages[channel_offset + chip*CHANNELS_PER_CHIP + channel];
}


// spi_timer_interrupt
//  timer interrupt that handles incrementing and sending the request to the
//  SPI ADC to get the ADC value at that channel
// will be run at 1000Hz
void spi_timer_interrupt(void)
{
	U8 c, bus;
	static U32 bus_failures[NUMBER_OF_BUSSES] = { 0 };
	static U32 bus_passes[NUMBER_OF_BUSSES] = { 0 };

	// toggle the chip select when sending the SPI command. Because this is
	// the same for every bus we will set it low at the start of the requests
	// and set it high again at the end
	HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);
	for (U32 delay = 0; delay < 100; delay++) __ASM volatile ("NOP");
	HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);
	for (U32 delay = 0; delay < 10; delay++) __ASM volatile ("NOP");

	for (bus = 0; bus < NUMBER_OF_BUSSES; bus++)
	{
		if (last_input_channel[bus] != current_input_channel[bus])
		{
			// the MCU did not receive a spi message from this bus. Reset from the beginning
			// TODO error handling
			bus_failures[bus]++;
			last_input_channel[bus] = 0;
			current_input_channel[bus] = 0;
			continue;
		}

		bus_passes[bus]++;

		// check if we just rolled over this bus. if so, put all of the values into
		// the new buffer
		if (current_input_channel[bus] == 0)
		{
			U8 chip, channel;
			U16 value;
			U32 channel_offset = 0;

			for (chip = 0; chip < chips_per_bus[bus]; chip++)
			{
				for (channel = 0; channel < CHANNELS_PER_CHIP; channel++)
				{
					// get the data location based off bus, chip, and channel
					value = *((U16*)bus_buffers[bus] + channel*chips_per_bus[bus] + chip);

					// find where it goes in the channel average buffer and add it
					channel_offset = 0;
					for (c = 0; c < bus; c++)
					{
						channel_offset += CHANNELS_PER_CHIP*chips_per_bus[c];
					}

					channel_averages[channel_offset + chip*CHANNELS_PER_CHIP + channel] = value;
				}
			}

			last_input_channel[bus] = 0;
			current_input_channel[bus] = 0;
		}

		// send the message to request the next channel using the AUTO_RESET mode.
		// the RX will be enabled when the TX has confirmed to be sent
		tx_buffer = channel_req_cmds[current_input_channel[bus]];
		HAL_SPI_Transmit_IT(hspi_ptr_arr[bus], (U8*)&tx_buffer, CHAN_REQ_SIZE);

		// increment what the current input channel is and request that new
		// channel with a SPI request
		current_input_channel[bus] = (current_input_channel[bus] + 1) % CHANNELS_PER_CHIP;

		// now we can move onto the next bus
	}
}


// spi_tx_interrupt
//  Once the packet has been sent for this bus, start recieving with it
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef* hspi)
{
	U8 c;

	// find which bus this is
	for (c = 0; c < NUMBER_OF_BUSSES; c++)
	{
		if (hspi == hspi_ptr_arr[c])
		{
			// start the spi RX for this bus
			HAL_SPI_Receive_IT(hspi_ptr_arr[c], (U8*)(bus_buffers[c] + last_input_channel[c]*chips_per_bus[c]),
							   chips_per_bus[c]);
			return;
		}
	}
}


// spi_rx_interrupt
//  For each SPI rx message, note that this channel was received. This is important
//  to ensure there is no problem with the data in the buffer and it can be
//  correctly parsed
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef* hspi)
{
	U8 c;

	// find which bus this is from and increase that counter
	for (c = 0; c < NUMBER_OF_BUSSES; c++)
	{
		if (hspi == hspi_ptr_arr[c])
		{
			last_input_channel[c] = (last_input_channel[c] + 1) % CHANNELS_PER_CHIP;
			return;
		}
	}

	// no idea what happened
	// TODO error handling
	HAL_GPIO_WritePin(GPIOB, HBEAT_LED_Pin, GPIO_PIN_SET);
}



