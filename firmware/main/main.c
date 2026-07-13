#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "driver/i2c_master.h"

#include "sdkconfig.h"

// WiFi
#define WIFI_SSID                CONFIG_WIFI_SSID
#define WIFI_PASS                CONFIG_WIFI_PASSWORD
#define WIFI_CONNECTED_BIT       BIT0
#define WIFI_FAIL_BIT            BIT1
#define WIFI_AUTH_FAIL_BIT       BIT2
#define WIFI_MAX_RETRIES         CONFIG_WIFI_MAX_RETRIES
#define WIFI_RETRY_INIT_DELAY_MS 1000
#define WIFI_RETRY_MAX_DELAY_MS  CONFIG_WIFI_MAX_RETRY_DELAY_MS
#define WIFI_SEND_TIMEOUT_MS     10000

#if CONFIG_WIFI_WPA3_SAE_PWE_HUNT_AND_PECK
#define WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define WIFI_H2E_IDENTIFIER ""
#elif CONFIG_WIFI_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define WIFI_H2E_IDENTIFIER CONFIG_WIFI_PW_ID
#elif CONFIG_WIFI_WPA3_SAE_PWE_BOTH
#define WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define WIFI_H2E_IDENTIFIER CONFIG_WIFI_PW_ID
#endif

#if CONFIG_WIFI_AUTH_OPEN
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_WIFI_AUTH_WEP
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_WIFI_AUTH_WPA_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_WIFI_AUTH_WPA2_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_WIFI_AUTH_WPA_WPA2_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_WIFI_AUTH_WPA3_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_WIFI_AUTH_WPA2_WPA3_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_WIFI_AUTH_WAPI_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

// Application
#define MONITOR_CYCLE_DELAY_MS            2000
#define MONITOR_RETRY_DELAY_MS            2000
#define MONITOR_RESTART_DELAY_MS          1000
#define MONITOR_AUTH_RESTART_DELAY_MS     60000
#define MONITOR_MAX_RETRIES               20

// I2C Bus
#define I2C_MASTER_SCL_IO                 CONFIG_AHT20_SCL_GPIO
#define I2C_MASTER_SDA_IO                 CONFIG_AHT20_SDA_GPIO
#define I2C_MASTER_NUM                    I2C_NUM_0
#define I2C_MASTER_FREQ_HZ                CONFIG_AHT20_SCL_FREQ_HZ

// AHT20
#define AHT20_SENSOR_ADDR                 0x38
#define AHT20_SENSOR_CAL_BIT              3     // Calibration Enable Bit
#define AHT20_SENSOR_BUSY_BIT             7     // Busy Indication Bit
#define AHT20_SENSOR_BUSY_MAX_RETRIES     8
#define AHT20_SENSOR_POWERON_DELAY_MS     40
#define AHT20_SENSOR_SOFT_RESET_DELAY_MS  20
#define AHT20_SENSOR_INIT_DELAY_MS        10
#define AHT20_SENSOR_MEAS_DELAY_MS        80
#define AHT20_SENSOR_BUSY_DELAY_MS        10
#define AHT20_SENSOR_XFER_TIMEOUT_MS      100   // Read/Write Wait Timeout

#define AHT20_SENSOR_CMD_SOFT_RESET       0xBA
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

static const char *MONITOR_TAG = "Monitor";

static const char *WIFI_TAG = "WiFi";
static EventGroupHandle_t s_wifi_event_group;
static esp_timer_handle_t s_wifi_retry_timer;

static const char *AHT20_TAG = "AHT20";

static void event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    static int retry_num = 0;
    static uint64_t retry_delay_ms = WIFI_RETRY_INIT_DELAY_MS;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(WIFI_TAG, "esp_wifi_connect failed due to: %s", esp_err_to_name(ret));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(WIFI_TAG, "Station successfully connected to AP");
        ESP_LOGD(WIFI_TAG, "SSID:%s", WIFI_SSID);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *data_got_ip = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(WIFI_TAG, "Got ip:" IPSTR, IP2STR(&data_got_ip->ip_info.ip));

        retry_num = 0;
        retry_delay_ms = WIFI_RETRY_INIT_DELAY_MS;

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_AUTH_FAIL_BIT);
        wifi_event_sta_disconnected_t *data_disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(WIFI_TAG, "Failed to connect to the AP due to: %d", data_disconnected->reason);

        if (data_disconnected->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
            data_disconnected->reason == WIFI_REASON_AUTH_FAIL) {
            ESP_LOGE(WIFI_TAG, "Authentication failure detected");
            xEventGroupSetBits(s_wifi_event_group, WIFI_AUTH_FAIL_BIT);
            return;
        }

        if (retry_num < WIFI_MAX_RETRIES) {
            retry_num++;
            ESP_LOGI(
                WIFI_TAG,
                "Retrying establishing connection to AP: %d/%d, Retry delay: %" PRIu64 "ms",
                retry_num,
                WIFI_MAX_RETRIES,
                retry_delay_ms
            );
            esp_timer_stop(s_wifi_retry_timer);
            ESP_ERROR_CHECK(esp_timer_start_once(s_wifi_retry_timer, retry_delay_ms * 1000));

            retry_delay_ms = MIN(retry_delay_ms * 2, WIFI_RETRY_MAX_DELAY_MS);
        } else {
            ESP_LOGE(WIFI_TAG, "Max WiFi connection retry limit reached");
            retry_num = 0;
            retry_delay_ms = WIFI_RETRY_INIT_DELAY_MS;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
}

static void wifi_retry_timer_callback(void *arg)
{
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "esp_wifi_connect failed due to: %s", esp_err_to_name(ret));
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_timer_create_args_t create_args = {
        .callback = wifi_retry_timer_callback,
        .arg = NULL,
        .name = "wifi_retry_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&create_args, &s_wifi_retry_timer));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_STA_START,
        &event_handler,
        NULL,
        NULL
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_STA_CONNECTED,
        &event_handler,
        NULL,
        NULL
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_STA_DISCONNECTED,
        &event_handler,
        NULL,
        NULL
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &event_handler,
        NULL,
        NULL
    ));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WIFI_SAE_MODE,
            .sae_h2e_identifier = WIFI_H2E_IDENTIFIER,
#ifdef CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
            .disable_wpa3_compatible_mode = 0,
#endif
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

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

static esp_err_t aht20_soft_reset(i2c_master_dev_handle_t dev_handle)
{
    ESP_LOGI(AHT20_TAG, "Sending soft reset command");
    const uint8_t soft_reset_cmd = AHT20_SENSOR_CMD_SOFT_RESET;
    esp_err_t ret = i2c_master_transmit(
        dev_handle,
        &soft_reset_cmd,
        sizeof(soft_reset_cmd),
        AHT20_SENSOR_XFER_TIMEOUT_MS
    );
    if (ret != ESP_OK) {
        ESP_LOGE(AHT20_TAG, "Failed to send soft reset command (%s)", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(AHT20_SENSOR_SOFT_RESET_DELAY_MS));

    return ESP_OK;
}

static esp_err_t aht20_init(i2c_master_dev_handle_t dev_handle)
{
    uint8_t status;
    ESP_ERROR_CHECK(i2c_master_receive(dev_handle, &status, 1, AHT20_SENSOR_XFER_TIMEOUT_MS));
    ESP_LOGI(
        AHT20_TAG,
        "Initial status: 0x%02X, CAL bit: %d",
        status,
        (status >> AHT20_SENSOR_CAL_BIT) & 1
    );

    if (!(status & (1 << AHT20_SENSOR_CAL_BIT))) {
        ESP_LOGI(AHT20_TAG, "Calibration bit not set. Sending initialization command");
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
            ESP_LOGE(AHT20_TAG, "Invalid status during initialization: 0x%02X", status);
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI(
            AHT20_TAG,
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
static bool aht20_crc8_check(const uint8_t *data, size_t data_size)
{
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
    ESP_LOGI(AHT20_TAG, "Sending trigger measurement command");
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
        ESP_LOGE(AHT20_TAG, "Failed to send trigger measurement command (%s)", esp_err_to_name(ret));
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
            ESP_LOGE(AHT20_TAG, "Failed to read measurement data (%s)", esp_err_to_name(ret));
            return ret;
        }

        ESP_LOGD(AHT20_TAG, "Raw measurement data:");
        ESP_LOG_BUFFER_HEX_LEVEL(AHT20_TAG, measurement_data, sizeof(measurement_data), ESP_LOG_DEBUG);

        sensor_busy = measurement_data[0] & (1 << AHT20_SENSOR_BUSY_BIT);
        if (sensor_busy) {
            vTaskDelay(pdMS_TO_TICKS(AHT20_SENSOR_BUSY_DELAY_MS));
        }
    } while (++retry_count < AHT20_SENSOR_BUSY_MAX_RETRIES && sensor_busy);
    if (sensor_busy) {
        ESP_LOGE(AHT20_TAG, "Max retry limit reached waiting for busy sensor");
        return ESP_ERR_TIMEOUT;
    }

    if (!aht20_crc8_check(measurement_data, sizeof(measurement_data))) {
        ESP_LOGE(AHT20_TAG, "CRC check failed! Raw measurement data:");
        ESP_LOG_BUFFER_HEX_LEVEL(AHT20_TAG, measurement_data, sizeof(measurement_data), ESP_LOG_ERROR);
        return ESP_ERR_INVALID_CRC;
    }
    ESP_LOGI(AHT20_TAG, "CRC check passed");

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

static esp_err_t send_measurement(aht20_measurement_t measurement)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_AUTH_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_SEND_TIMEOUT_MS)
    );
    if (!(bits & (WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_AUTH_FAIL_BIT))) {
        ESP_LOGE(MONITOR_TAG, "WiFi connection timed out");
        return ESP_ERR_TIMEOUT;
    }
    if (bits & WIFI_AUTH_FAIL_BIT) {
        return ESP_ERR_WIFI_PASSWORD;
    }
    if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    }

    ESP_LOGD(MONITOR_TAG, "Connection is up. Sending measurements");
    // TODO Send Measurements

    return ESP_OK;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(MONITOR_TAG, "NVS initialized successfully");

    wifi_init_sta();
    ESP_LOGI(MONITOR_TAG, "WiFi initialized successfully");

    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(MONITOR_TAG, "I2C initialized successfully");

    vTaskDelay(pdMS_TO_TICKS(AHT20_SENSOR_POWERON_DELAY_MS));
    ESP_ERROR_CHECK(aht20_soft_reset(dev_handle));
    ESP_ERROR_CHECK(aht20_init(dev_handle));
    ESP_LOGI(MONITOR_TAG, "AHT20 initialized successfully");

    /**
     * The AHT20 datasheet recommends measuring data every 2 seconds.
     * In addition to the 2 second delay, additional tasks and delays should provide a sufficient
     * buffer to prevent the temperature of the sensor from being affected.
     */
    uint8_t retry_count = 0;
    while (1) {
        aht20_measurement_t measurement;
        ret = aht20_measure(dev_handle, &measurement);

        if (ret != ESP_OK) {
            retry_count++;
            ESP_LOGW(MONITOR_TAG, "Measurement cycle %d/%d", retry_count, MONITOR_MAX_RETRIES);
            if (retry_count >= MONITOR_MAX_RETRIES) {
                ESP_LOGE(MONITOR_TAG, "Max retry limit reached. Restarting");
                vTaskDelay(pdMS_TO_TICKS(MONITOR_RESTART_DELAY_MS));
                esp_restart();
            }

            vTaskDelay(pdMS_TO_TICKS(MONITOR_RETRY_DELAY_MS));
            continue;
        }
        retry_count = 0;

        ESP_LOGI(MONITOR_TAG, "Relative Humidity(%): %f", measurement.humidity);
        ESP_LOGI(MONITOR_TAG, "Temperature(C):       %f", measurement.temperature);

        /**
         * TODO: replace with a queue-based approach to buffer measurements during outages
         * instead of skipping send_measurement() and losing measured data.
         */
        ret = send_measurement(measurement);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(MONITOR_TAG, "WiFi not available, skipping send");
        } else if (ret == ESP_ERR_WIFI_PASSWORD) {
            ESP_LOGE(
                MONITOR_TAG,
                "WiFi authentication failed — check credentials. Restarting in %ds",
                MONITOR_AUTH_RESTART_DELAY_MS / 1000
            );
            vTaskDelay(pdMS_TO_TICKS(MONITOR_AUTH_RESTART_DELAY_MS));
            esp_restart();
        } else if (ret != ESP_OK) {
            ESP_LOGE(MONITOR_TAG, "Critical WiFi failure encountered. Restarting");
            vTaskDelay(pdMS_TO_TICKS(MONITOR_RESTART_DELAY_MS));
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(MONITOR_CYCLE_DELAY_MS));
    }
}
