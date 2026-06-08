#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"

static const char *TAG = "AHT20";

#define I2C_MASTER_SCL_IO            CONFIG_I2C_MASTER_SCL
#define I2C_MASTER_SDA_IO            CONFIG_I2C_MASTER_SDA
#define I2C_MASTER_NUM               I2C_NUM_0
#define I2C_MASTER_FREQ_HZ           CONFIG_I2C_MASTER_FREQUENCY

#define AHT20_SENSOR_ADDR            0x38
#define AHT20_SENSOR_CAL_BIT         3    // Calibration Enable Bit
#define AHT20_SENSOR_XFER_TIMEOUT_MS 100  // Read/Write Wait Timeout
#define AHT20_SENSOR_CMD_INIT        0xBE
#define AHT20_SENSOR_CMD_INIT_PARAM1 0x08
#define AHT20_SENSOR_CMD_INIT_PARAM2 0x00

static void i2c_master_init(
    i2c_master_bus_handle_t *bus_handle,
    i2c_master_dev_handle_t *dev_handle)
{
    /**
     * External pull-ups present on SDA/SCL.
     * flags.enable_internal_pullup left unset
     */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT20_SENSOR_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
}

static esp_err_t aht20_init(i2c_master_dev_handle_t dev_handle)
{
    // Wait 40ms after power-on
    vTaskDelay(pdMS_TO_TICKS(40));

    // Get status
    uint8_t status;
    ESP_ERROR_CHECK(i2c_master_receive(dev_handle, &status, 1, AHT20_SENSOR_XFER_TIMEOUT_MS));
    ESP_LOGI(
        TAG,
        "Initial status: 0x%02X, CAL bit: %d",
        status,
        (status >> AHT20_SENSOR_CAL_BIT) & 1
    );

    // Check calibration enable bit and init AHT20 if necessary
    if (!(status & (1 << AHT20_SENSOR_CAL_BIT))) {
        ESP_LOGI(TAG, "Calibration bit not set. Sending initialization command");
        const uint8_t init_cmd[3] = {
            AHT20_SENSOR_CMD_INIT,
            AHT20_SENSOR_CMD_INIT_PARAM1,
            AHT20_SENSOR_CMD_INIT_PARAM2,
        };
        ESP_ERROR_CHECK(i2c_master_transmit(
            dev_handle,
            init_cmd,
            sizeof(init_cmd),
            AHT20_SENSOR_XFER_TIMEOUT_MS
        ));
        vTaskDelay(pdMS_TO_TICKS(10));

        ESP_ERROR_CHECK(i2c_master_receive(dev_handle, &status, 1, AHT20_SENSOR_XFER_TIMEOUT_MS));
        if (!(status & (1 << AHT20_SENSOR_CAL_BIT))) {
            ESP_LOGE(TAG, "Invalid status during initialization: 0x%02X", status);
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI(
            TAG,
            "Post-initialization status: 0x%02X, CAL bit: %d",
            status,
            (status >> AHT20_SENSOR_CAL_BIT) & 1
        );
    }

    return ESP_OK;
}

void app_main(void)
{
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");

    ESP_ERROR_CHECK(aht20_init(dev_handle));
    ESP_LOGI(TAG, "AHT20 initialized successfully");
}
