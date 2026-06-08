#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define I2C_MASTER_SCL_IO            CONFIG_I2C_MASTER_SCL
#define I2C_MASTER_SDA_IO            CONFIG_I2C_MASTER_SDA
#define I2C_MASTER_FREQ_HZ           CONFIG_I2C_MASTER_FREQUENCY

void app_main(void)
{
}
