/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file : main.c
  * @brief : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dac.h"
#include "dma.h"
#include "fatfs.h"
#include "sdio.h"
#include "tim.h"
#include "gpio.h"
#include "fsmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lcd.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define AUDIO_BUF_SIZE 512
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
DIR dir;
FILINFO fno;
FATFS fs;
FIL wavFile;
uint8_t audioBuf[AUDIO_BUF_SIZE];
uint32_t sampleRate;
uint16_t channels;
uint16_t bitDepth;
uint8_t isPlaying = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
uint8_t FindFirstWavFile(char *path);
uint8_t ParseWavHeader(void);
void PlayWavFile(void);
void DAC_Output_16bit(int16_t pcm);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint8_t FindFirstWavFile(char *path)
{
  /*
  strcpy(path, "test.wav");
  return 0;
  */
	FRESULT res;
	// clear path
	path[0] = '\0';
	// open directory for searching
	res = f_opendir(&dir, "0:/");
	if (res != FR_OK)
	{
		return 2;
	}
	// use f_findfirst find first wav file
	res = f_findfirst(&dir, &fno, "0:/", "*.wav");
	if (res == FR_OK && fno.fname[0] != 0)
	{
		// find file, construct path "0:/filename.wav"
		sprintf(path, "0:/%s", fno.fname);
		f_closedir(&dir);
		return 0; // success
	}
	else
	{
		// no wav file is found
		f_closedir(&dir);
		return 1;
}
}
uint8_t ParseWavHeader(void)
{
  uint8_t header[44];
  UINT bytesRead;

  f_read(&wavFile, header, 44, &bytesRead);
  if(bytesRead != 44) return 1;

  sampleRate = *(uint32_t*)&header[24];
  channels = *(uint16_t*)&header[22];
  bitDepth = *(uint16_t*)&header[34];
  char buf[64];

  sprintf(buf, "SR:%ld CH:%d BIT:%d", sampleRate, channels, bitDepth);
  LCD_DrawString(10, 80, buf);
  if(bitDepth != 16) return 2;

  return 0;
}
void DAC_Output_16bit(int16_t pcm)
{
    uint32_t dacVal = ((uint32_t)(pcm + 32768)) >> 4;
    if (dacVal > 4095) dacVal = 4095;
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dacVal);
}

/*
void audio_delay_us(uint32_t us)
{
    us = us * 9;  // 72MHz ? ¡??†ç³»?•°
    while(us--);
}
*/

void PlayWavFile(void)
{
  UINT bytesRead;
  int16_t pcmData;
  uint32_t index = 0;
  while(1)
  {

    f_read(&wavFile, audioBuf, AUDIO_BUF_SIZE, &bytesRead);
    if(bytesRead == 0) break;
    for(index=0; index<bytesRead; index+=2)
    {
      pcmData = (audioBuf[index+1] << 8) | audioBuf[index];
      DAC_Output_16bit(pcmData);
      // 44.1kHz standard delay
      for(uint32_t i=0; i<130; i++);
      //audio_delay_us(sample_delay);
    }
  }
}
void SD_WAV_Player_Main(void)
{
	  FRESULT res;
	  char wavPath[64] = {0};
	  uint8_t ret;
	  HAL_Delay(500);
	  // load sd card
	  res = f_mount(&fs, "0:", 1);
	  if(res != FR_OK)
	  {
		LCD_DrawString(10,20,"SD Mount ERR");
		return;
	  }
	  LCD_DrawString(10,20,"SD Mount OK");
	  // find file
	  ret = FindFirstWavFile(wavPath);
	  LCD_DrawString(10,40,wavPath);
	  // open file
	  res = f_open(&wavFile, wavPath, FA_READ);
	  if(res != FR_OK)
	  {
		LCD_DrawString(10,60,"Open ERR");
		return;
	  }
	  // analyze head
	  ret = ParseWavHeader();
	  if(ret != 0)
	  {
		LCD_DrawString(10,100,"WAV ERR");
		f_close(&wavFile);
		return;
	  }
	  LCD_DrawString(10,120,"Playing...");
	  // start TIM6 + DAC
	  HAL_TIM_Base_Start(&htim6);
	  HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
	  PlayWavFile();
	  LCD_DrawString(10,140,"Finish!");
	  HAL_DAC_Stop(&hdac, DAC_CHANNEL_1);
	  f_close(&wavFile);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FSMC_Init();
  MX_FATFS_Init();
  MX_DAC_Init();
  MX_DMA_Init();
  MX_TIM6_Init();
  MX_SDIO_SD_Init();
  /* USER CODE BEGIN 2 */
  LCD_INIT();
  //SDCard_Check();
  SD_WAV_Player_Main();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
