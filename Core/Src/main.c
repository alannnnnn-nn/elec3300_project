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
// 緩衝區大小設置為 4096 字節（對應 2048 個 16-bit 採樣）
// DMA 會以「半滿」和「全滿」觸發兩次中斷，每次只需讀取 2048 字節，平衡了內存與讀取頻率
#define AUDIO_BUF_SIZE 4096
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
FATFS fs;
FIL wavFile;
FRESULT res;
UINT bytesRead;

// 用於 DMA 的 16-bit 緩衝區（DAC 需要 12 位，但存儲用 16 位寬度）
uint16_t dmaBuffer[AUDIO_BUF_SIZE / 2];
// 用於從 SD 卡讀取原始數據的中間緩衝區
uint8_t  tempReadBuf[AUDIO_BUF_SIZE / 2];

volatile uint8_t refill_half = 0; // 標記哪一半緩衝區需要填充 (0: 前半, 1: 後半)
volatile uint8_t data_request = 0; // 數據請求標誌位
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN PFP */
void Play_Wav_NoBlocking(const char* filename);
uint16_t Process_Sample(int16_t pcm_val);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
/**
  * @brief DMA 傳輸一半完成回調
  */
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef* hdac) {
    refill_half = 0;    // DMA 正在播放後半部分，我們可以填充前半部分
    data_request = 1;
}

/**
  * @brief DMA 傳輸全部完成回調
  */
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef* hdac) {
    refill_half = 1;    // DMA 正在播放前半部分，我們可以填充後半部分
    data_request = 1;
}

/**
  * @brief 數據處理：將 WAV 的 16位有符號數 轉為 DAC 的 12位無符號數
  */
uint16_t Process_Sample(int16_t pcm_val) {
    // 1. WAV 是有符號的 (-32768 ~ 32767)，先轉為無符號 (0 ~ 65535)
    int32_t val = (int32_t)pcm_val + 32768;
    // 2. 將 16 位映射到 DAC 的 12 位 (0 ~ 4095)
    return (uint16_t)(val >> 4);
}
/* USER CODE END 0 */
/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  /* 初始化所有外設 */
  MX_GPIO_Init();
  MX_FSMC_Init();
  MX_SDIO_SD_Init();
  MX_FATFS_Init();
  MX_DAC_Init();
  MX_TIM6_Init();

  /* LCD 顯示初始狀態 */
  LCD_INIT();
  LCD_Clear(0, 0, 240, 320, BACKGROUND);
  LCD_DrawString(10, 10, "System Ready...");

  /* 掛載 SD 卡 */
  if(f_mount(&fs, "", 1) == FR_OK) {
      LCD_DrawString(10, 30, "SD Mounted. Starting Play...");
      Play_Wav_NoBlocking("music.wav");
  } else {
      LCD_DrawString(10, 30, "SD Error!");
  }

  while (1)
  {
    /* 這裡可以放你的 LCD 刷新代碼或其他邏輯
       例如：顯示播放進度、響應按鍵等。
       因為音頻播放交給了 DMA 中斷處理，這裡不會被卡住。
    */
    static uint32_t last_tick = 0;
    if(HAL_GetTick() - last_tick > 500) {
        LCD_DrawString(10, 100, "LCD is still running!");
        last_tick = HAL_GetTick();
    }
  }
}

void Play_Wav_NoBlocking(const char* filename) {
    res = f_open(&wavFile, filename, FA_READ);
    if (res != FR_OK) return;

    // 跳過 44 字節的 WAV 文件頭
    f_lseek(&wavFile, 44);

    // 預填充整個 dmaBuffer
    // 填充前半段
    f_read(&wavFile, tempReadBuf, AUDIO_BUF_SIZE / 2, &bytesRead);
    for(int i=0; i < (AUDIO_BUF_SIZE / 4); i++) {
        int16_t pcm = (tempReadBuf[2*i+1] << 8) | tempReadBuf[2*i];
        dmaBuffer[i] = Process_Sample(pcm);
    }
    // 填充後半段
    f_read(&wavFile, tempReadBuf, AUDIO_BUF_SIZE / 2, &bytesRead);
    for(int i=0; i < (AUDIO_BUF_SIZE / 4); i++) {
        int16_t pcm = (tempReadBuf[2*i+1] << 8) | tempReadBuf[2*i];
        dmaBuffer[i + (AUDIO_BUF_SIZE/4)] = Process_Sample(pcm);
    }

    // 啟動定時器和 DAC DMA 傳輸（模式需設為 Circular）
    HAL_TIM_Base_Start(&htim6);
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t*)dmaBuffer, AUDIO_BUF_SIZE / 2, DAC_ALIGN_12B_R);

    // 播放循環
    while(1) {
        // 當 DMA 回調觸發 data_request 時，才進行 SD 卡讀取
        if(data_request) {
            data_request = 0;

            // 每次只讀取緩衝區的一半長度
            f_read(&wavFile, tempReadBuf, AUDIO_BUF_SIZE / 2, &bytesRead);
            if(bytesRead == 0) break; // 播放結束

            // 根據標誌位決定寫入 dmaBuffer 的前半部分還是後半部分
            uint32_t offset = (refill_half == 0) ? 0 : (AUDIO_BUF_SIZE / 4);

            for(int i=0; i < bytesRead/2; i++) {
                int16_t pcm = (tempReadBuf[2*i+1] << 8) | tempReadBuf[2*i];
                dmaBuffer[offset + i] = Process_Sample(pcm);
            }
        }

        // --- 重要：這裡不再使用 delay 或死循環 ---
        // 你可以在這裡處理 LCD 繪圖，音頻會在背景自動播放
        // 注意：不要在循環內放過於耗時的操作，否則 SD 卡數據讀取會過慢導致噪音
    }

    // 停止播放
    HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_1);
    HAL_TIM_Base_Stop(&htim6);
    f_close(&wavFile);
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
