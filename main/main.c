#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define I2C_MASTER_SCL_IO    22
#define I2C_MASTER_SDA_IO    21
#define I2C_MASTER_NUM       I2C_NUM_0
#define I2C_MASTER_FREQ_HZ   100000
#define SHT30_SENSOR_ADDR    0x44
#define DS1307_ADDR          0x68

static const char *TAG = "rtc_timer";

// ===================== TYPE DEFINITIONS =========================
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t date;
    uint8_t month;
    uint8_t year;
} ds1307_time_t;

typedef enum {
    MODE_NONE = 0,
    MODE_PLA,
    MODE_ABS,
    MODE_PETG,
    MODE_TPU,
    MODE_COUNT
} drying_mode_t;

typedef struct {
    drying_mode_t mode;
    const char* name;
    float target_temp;
} drying_profile_t;

const drying_profile_t drying_profiles[MODE_COUNT] = {
    { MODE_NONE, "None", 0.0f },
    { MODE_PLA,  "PLA",  50.0f },
    { MODE_ABS,  "ABS",  80.0f },
    { MODE_PETG, "PETG", 70.0f },
    { MODE_TPU,  "TPU",  60.0f }
};

drying_mode_t active_mode = MODE_NONE;
bool heater_state = false;

// ===================== UTILITIES =========================
uint8_t bcd_to_dec(uint8_t val) {
    return ((val / 16) * 10 + (val % 16));
}

uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10) * 16 + (val % 10));
}

void heater_on() {
    if (!heater_state) {
        ESP_LOGI(TAG, "Heater ON");
        heater_state = true;
    }
}

void heater_off() {
    if (heater_state) {
        ESP_LOGI(TAG, "Heater OFF");
        heater_state = false;
    }
}

void show_mode_menu() {
    printf("\nAvailable drying modes:\n");
    for (int i = 1; i < MODE_COUNT; i++) {
        printf("  %d. %s (%.1f\xC2\xB0C)\n", i, drying_profiles[i].name, drying_profiles[i].target_temp);
    }
    printf("Select mode (1-%d): ", MODE_COUNT - 1);

    int selected = 0;
    scanf("%d", &selected);
    if (selected > 0 && selected < MODE_COUNT) {
        active_mode = (drying_mode_t)selected;
        ESP_LOGI(TAG, "Selected mode: %s", drying_profiles[active_mode].name);
    } else {
        ESP_LOGW(TAG, "Invalid selection");
    }
}

void mode_selection_task(void *arg) {
    while (1) {
        show_mode_menu();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ================== I2C INIT ==========================
static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// ================== DS1307 FUNCTIONS =====================
esp_err_t get_time_from_ds1307(ds1307_time_t *time) {
    uint8_t start_reg = 0x00;
    uint8_t data[7];

    esp_err_t ret = i2c_master_write_read_device(I2C_MASTER_NUM, DS1307_ADDR, &start_reg, 1, data, 7, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) return ret;

    time->second = bcd_to_dec(data[0] & 0x7F);
    time->minute = bcd_to_dec(data[1]);
    time->hour   = bcd_to_dec(data[2]);
    time->day    = bcd_to_dec(data[3]);
    time->date   = bcd_to_dec(data[4]);
    time->month  = bcd_to_dec(data[5]);
    time->year   = bcd_to_dec(data[6]);

    return ESP_OK;
}

// ================== TIME PRINT ========================
void printTime(ds1307_time_t *time) {
    ESP_LOGI(TAG, "Time: %02d:%02d:%02d %02d/%02d/20%02d",
             time->hour, time->minute, time->second,
             time->date, time->month, time->year);
}

// ==================== MAIN ============================
void app_main() {
    ESP_ERROR_CHECK(i2c_master_init());

    ds1307_time_t now;
    xTaskCreate(mode_selection_task, "mode_selection_task", 4096, NULL, 5, NULL);

    while (1) {
        ESP_ERROR_CHECK(get_time_from_ds1307(&now));
        printTime(&now);

        // Read temperature from SHT30
        uint8_t command[2] = {0x2C, 0x06};
        uint8_t data[6];
        esp_err_t ret = i2c_master_write_read_device(I2C_MASTER_NUM, SHT30_SENSOR_ADDR, command, sizeof(command), data, sizeof(data), pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SHT30 read error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        uint16_t temp_raw = (data[0] << 8) | data[1];
        float temperature = -45 + 175 * ((float)temp_raw / 65535.0);
        ESP_LOGI(TAG, "Current Temperature: %.2f \xC2\xB0C", temperature);

        if (active_mode != MODE_NONE) {
            float target = drying_profiles[active_mode].target_temp;

            if (temperature >= target) {
                heater_off();
            } else if (temperature < target - 2.0f) {
                heater_on();
            }

            ESP_LOGI(TAG, "Target Temperature: %.1f \xC2\xB0C (Mode: %s)", target, drying_profiles[active_mode].name);
        } else {
            heater_off();
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}