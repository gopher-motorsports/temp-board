// spi_adc.h
//  Header file for spi_adc.c


#ifndef SPI_ADC_H
#define SPI_ADC_H

#include "base_types.h"
#include "stm32f7xx_hal.h"
#include "stm32f7xx_hal_tim.h"
#include "stm32f7xx_hal_spi.h"

#define NUMBER_OF_BUSSES 5 // number of SPI busses being run
#define BUS_0_CHIPS 3
#define BUS_1_CHIPS 2
#define BUS_2_CHIPS 2
#define BUS_3_CHIPS 2
#define BUS_4_CHIPS 2
#define TOTAL_CHIPS (BUS_0_CHIPS*BUS_1_CHIPS*BUS_2_CHIPS*BUS_3_CHIPS*BUS_4_CHIPS)
#define CHANNELS_PER_CHIP 8 // number of inputs on each ADC chip
#define SAMPLING_FRQ 100 // how often to poll the ADC chip. This is the time to go from one channel to the next

extern volatile U16 chips_per_bus[];

typedef enum
{
	SPI_OK = 0,
	SPI_NULL_INPUT = -1,
	SPI_BUS_OOB = -2,
} SPI_ADC_ERR;

SPI_ADC_ERR define_spi_bus(SPI_HandleTypeDef* hspi, U8 bus_number);
SPI_ADC_ERR init_timer(TIM_HandleTypeDef* htim, U16 psc);
void start_spi_collection(void);
U16 get_channel_average(U8 bus, U8 chip, U8 channel);
void spi_timer_interrupt(void);
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef* hspi);

#endif // SPI_ADC_H


