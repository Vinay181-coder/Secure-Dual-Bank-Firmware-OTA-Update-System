/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Bootloader
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "metadata.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define FLASH_SECTOR_METADATA    FLASH_SECTOR_2
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */
static void     blink(uint32_t n);
static void     jump_to_app(uint32_t app_addr);
static uint32_t compute_crc32(uint32_t addr, uint32_t size);
static void     erase_sector(uint32_t sector);
static void     write_metadata(boot_metadata_t *m);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void blink(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
        HAL_Delay(200);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
        HAL_Delay(200);
    }
    HAL_Delay(600);
}

static void jump_to_app(uint32_t app_addr)
{
    uint32_t app_sp = *(volatile uint32_t *)(app_addr);
    uint32_t app_pc = *(volatile uint32_t *)(app_addr + 4U);

    if (app_sp < 0x20000000UL || app_sp > 0x20020000UL)
        return;

    if (app_pc < 0x08000000UL || app_pc > 0x08080000UL)
        return;

    /* Lock flash */
    HAL_FLASH_Lock();

    /* Disable SysTick */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* Clear all NVIC interrupts */
    for (uint8_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* Disable all interrupts */
   //__disable_irq();

    /* Relocate vector table */
    SCB->VTOR = app_addr;
    __DSB();
    __ISB();

    /* Set stack pointer and jump */
    __set_MSP(app_sp);
    __set_CONTROL(0U);
    __ISB();

   __enable_irq();

    ((void (*)(void))app_pc)();

    while (1);
}

static uint32_t compute_crc32(uint32_t addr, uint32_t size)
{
    __HAL_RCC_CRC_CLK_ENABLE();
    CRC->CR = CRC_CR_RESET;
    uint32_t *src   = (uint32_t *)addr;
    uint32_t  words = size / 4U;
    for (uint32_t i = 0; i < words; i++)
        CRC->DR = src[i];
    uint32_t remaining = size % 4U;
    if (remaining > 0U)
    {
        uint32_t last_word = 0U;
        uint8_t *tail = (uint8_t *)(addr + (words * 4U));
        for (uint32_t i = 0; i < remaining; i++)
            last_word |= ((uint32_t)tail[i] << (i * 8U));
        CRC->DR = last_word;
    }
    return CRC->DR;
}

static void erase_sector(uint32_t sector)
{
    FLASH_EraseInitTypeDef erase_init = {0};
    uint32_t sector_error = 0U;
    erase_init.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector       = sector;
    erase_init.NbSectors    = 1U;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASH_Unlock();
    HAL_FLASHEx_Erase(&erase_init, &sector_error);
    HAL_FLASH_Lock();
}

static void write_metadata(boot_metadata_t *m)
{
    erase_sector(FLASH_SECTOR_METADATA);
    HAL_FLASH_Unlock();
    uint32_t addr = METADATA_BASE;
    uint32_t *src = (uint32_t *)m;
    for (uint32_t i = 0; i < (sizeof(boot_metadata_t) / 4U); i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            Error_Handler();
        }
        addr += 4U;
    }
    HAL_FLASH_Lock();
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
  /* USER CODE BEGIN 2 */

    blink(1);  /* 1 blink = bootloader started */

    uint32_t boot_flag = METADATA->boot_flag;
    uint32_t crc_exp   = METADATA->fw_crc32;
    uint32_t fw_size   = METADATA->fw_size;
    uint32_t fw_ver    = METADATA->fw_version;

    if (boot_flag == BOOT_FLAG_BANK_B)
    {
        blink(2);  /* 2 blinks = OTA path */

        if (fw_size == 0U || fw_size > BANK_B_MAX_SIZE)
        {
            blink(5);  /* 5 blinks = bad size */
            jump_to_app(BANK_A_ADDR);
        }

        uint32_t crc_actual = compute_crc32(BANK_B_ADDR, fw_size);

        if (crc_actual == crc_exp)
        {
            blink(3);  /* 3 blinks = CRC passed */
            boot_metadata_t new_meta = {0};
            new_meta.boot_flag  = BOOT_FLAG_BANK_A;
            new_meta.fw_version = fw_ver;
            new_meta.fw_crc32   = crc_exp;
            new_meta.fw_size    = fw_size;
            new_meta.fw_bank    = BANK_B_ADDR;
            write_metadata(&new_meta);
            jump_to_app(BANK_B_ADDR);
        }
        else
        {
            blink(4);  /* 4 blinks = CRC failed */
            boot_metadata_t rollback = {0};
            rollback.boot_flag  = BOOT_FLAG_BANK_A;
            rollback.fw_version = fw_ver;
            rollback.fw_crc32   = 0U;
            rollback.fw_size    = 0U;
            rollback.fw_bank    = BANK_A_ADDR;
            write_metadata(&rollback);
            jump_to_app(BANK_A_ADDR);
        }
    }
    else
    {
        blink(1);  /* 1+1 blinks = normal boot path */
        jump_to_app(BANK_A_ADDR);

        /* Jump failed — fast blink */
        while (1)
        {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            HAL_Delay(200);
        }
    }

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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
