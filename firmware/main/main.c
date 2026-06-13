#include <stdbool.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"

static const char *TAG = "AHT20";

// Application
#define MONITOR_CYCLE_DELAY_MS            2000
#define MONITOR_RETRY_DELAY_MS            2000
#define MONITOR_RESTART_DELAY_MS          1000
#define MONITOR_MAX_RETRIES               20

// I2C Bus
#define I2C_MASTER_SCL_IO                 CONFIG_I2C_MASTER_SCL
#define I2C_MASTER_SDA_IO                 CONFIG_I2C_MASTER_SDA
#define I2C_MASTER_NUM                    I2C_NUM_0
#define I2C_MASTER_FREQ_HZ                CONFIG_I2C_MASTER_FREQUENCY

// AHT20
#define AHT20_SENSOR_ADDR                 0x38
#define AHT20_SENSOR_CAL_BIT              3     // Calibration Enable Bit
#define AHT20_SENSOR_BUSY_BIT             7     // Busy Indication Bit
#define AHT20_SENSOR_BUSY_MAX_RETRIES     8
#define AHT20_SENSOR_POWERON_DELAY_MS     40
#define AHT20_SENSOR_INIT_DELAY_MS        10
#define AHT20_SENSOR_MEAS_DELAY_MS        80
#define AHT20_SENSOR_BUSY_DELAY_MS        10
#define AHT20_SENSOR_XFER_TIMEOUT_MS      100   // Read/Write Wait Timeout

#define AHT20_SENSOR_CMD_INIT             0xBE
#define AHT20_SENSOR_CMD_INIT_PARAM1      0x08
#define AHT20_SENSOR_CMD_INIT_PARAM2      0x00
#define AHT20_SENSOR_CMD_TRIG_MEAS        0xAC
#define AHT20_SENSOR_CMD_TRIG_MEAS_PARAM1 0x33
#define AHT20_SENSOR_CMD_TRIG_MEAS_PARAM2 0x00

typedef struct aht20_measurement {
    float humidity;
    float temperature;
} aht20_measurement_t;

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
        .glitch_ignore_cnt = 7,  // ESP-IDF recommended default
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
    vTaskDelay(pdMS_TO_TICKS(AHT20_SENSOR_POWERON_DELAY_MS));

    uint8_t status;
    ESP_ERROR_CHECK(i2c_master_receive(dev_handle, &status, 1, AHT20_SENSOR_XFER_TIMEOUT_MS));
    ESP_LOGI(
        TAG,
        "Initial status: 0x%02X, CAL bit: %d",
        status,
        (status >> AHT20_SENSOR_CAL_BIT) & 1
    );

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
        vTaskDelay(pdMS_TO_TICKS(AHT20_SENSOR_INIT_DELAY_MS));

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

/**
 * CRC-8/NRSC-5
 *   width = 8
 *   poly = 0x31
 *   init = 0xFF
 *   refin = false
 *   refout = false
 *   xorout = 0x00
 *
 * AHT20 datasheet only specifies that the generator polynomial is:
 *   CRC[7:0] = 1 + x^4 + x^5 + x^8 --> 0x131. 
 * and that the CRC initial value is 0xFF.
 * refin/refout/xorout are not documented. These were inferred from Aosong's reference
 * implementation.
 */
static bool aht20_crc8_check(const uint8_t *data, size_t data_size) {
    const uint8_t key = 0x31;

    uint8_t crc = 0xFF;
    for (size_t i = 0; i < data_size; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc <<= 1;
                crc ^= key;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc == 0;
}

static esp_err_t aht20_measure(i2c_master_dev_handle_t dev_handle, aht20_measurement_t *measurement)
{
    ESP_LOGI(TAG, "Sending trigger measurement command");
    const uint8_t trigger_measurement_cmd[3] = {
        AHT20_SENSOR_CMD_TRIG_MEAS,
        AHT20_SENSOR_CMD_TRIG_MEAS_PARAM1,
        AHT20_SENSOR_CMD_TRIG_MEAS_PARAM2,
    };
    esp_err_t ret = i2c_master_transmit(
        dev_handle,
        trigger_measurement_cmd,
        sizeof(trigger_measurement_cmd),
        AHT20_SENSOR_XFER_TIMEOUT_MS
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send trigger measurement command (%s)", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(AHT20_SENSOR_MEAS_DELAY_MS));

    bool sensor_busy = true;
    uint8_t retry_count = 0;
    uint8_t measurement_data[7];
    do {
        ret = i2c_master_receive(
            dev_handle,
            measurement_data,
            sizeof(measurement_data),
            AHT20_SENSOR_XFER_TIMEOUT_MS
        );
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read measurement data (%s)", esp_err_to_name(ret));
            return ret;
        }

        ESP_LOGD(TAG, "Raw measurement data:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, measurement_data, sizeof(measurement_data), ESP_LOG_DEBUG);

        sensor_busy = measurement_data[0] & (1 << AHT20_SENSOR_BUSY_BIT);
        if (sensor_busy) {
            vTaskDelay(pdMS_TO_TICKS(AHT20_SENSOR_BUSY_DELAY_MS));
        }
    } while (++retry_count < AHT20_SENSOR_BUSY_MAX_RETRIES && sensor_busy);
    if (sensor_busy) {
        ESP_LOGE(TAG, "Max retry limit reached waiting for busy sensor");
        return ESP_ERR_TIMEOUT;
    }

    if (!aht20_crc8_check(measurement_data, sizeof(measurement_data))) {
        ESP_LOGE(TAG, "CRC check failed! Raw measurement data:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, measurement_data, sizeof(measurement_data), ESP_LOG_ERROR);
        return ESP_ERR_INVALID_CRC;
    }
    ESP_LOGI(TAG, "CRC check passed");

    uint32_t signal_humidity = measurement_data[1];
    signal_humidity = signal_humidity << 8 | measurement_data[2];
    signal_humidity = signal_humidity << 4 | measurement_data[3] >> 4;
    measurement->humidity = signal_humidity * 100.0f / (float)(1 << 20);

    uint32_t signal_temperature = measurement_data[3] & 0x0F;
    signal_temperature = signal_temperature << 8 | measurement_data[4];
    signal_temperature = signal_temperature << 8 | measurement_data[5];
    measurement->temperature = signal_temperature * 200.0f / (float)(1 << 20) - 50.0f;

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

    /**
     * The AHT20 datasheet recommends measuring data every 2 seconds.
     * In addition to the 2 second delay, additional tasks and delays should provide a sufficient
     * buffer to prevent the temperature of the sensor from being affected.
     */
    uint8_t retry_count = 0;
    while (1) {
        aht20_measurement_t measurement;
        esp_err_t ret = aht20_measure(dev_handle, &measurement);

        if (ret != ESP_OK) {
            retry_count++;
            ESP_LOGW(TAG, "Measurement cycle %d/%d", retry_count, MONITOR_MAX_RETRIES);
            if (retry_count >= MONITOR_MAX_RETRIES) {
                ESP_LOGE(TAG, "Max retry limit reached. Restarting");
                vTaskDelay(pdMS_TO_TICKS(MONITOR_RESTART_DELAY_MS));
                esp_restart();
            }

            vTaskDelay(pdMS_TO_TICKS(MONITOR_RETRY_DELAY_MS));
            continue;
        }
        retry_count = 0;

        ESP_LOGI(TAG, "Relative Humidity(%): %f", measurement.humidity);
        ESP_LOGI(TAG, "Temperature(C):       %f", measurement.temperature);

        vTaskDelay(pdMS_TO_TICKS(MONITOR_CYCLE_DELAY_MS));
    }
}
