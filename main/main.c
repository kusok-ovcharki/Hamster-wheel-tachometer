#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "ssd1306.h"
#include "font8x8_basic.h"
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"

/*
 You have to set this config value with menuconfig
 CONFIG_INTERFACE
 for i2c
 CONFIG_MODEL
 CONFIG_SDA_GPIO
 CONFIG_SCL_GPIO
 CONFIG_RESET_GPIO
 for SPI
 CONFIG_CS_GPIO
 CONFIG_DC_GPIO
 CONFIG_RESET_GPIO
*/

#define MOSI_GPIO 10
#define SCLK_GPIO 8 
#define CS_GPIO 20
#define DC_GPIO 21
#define RESET_GPIO 3
#define SDA_GPIO 6
#define SCL_GPIO 7
#define BUTTON_GPIO 8     // GPIO для кнопки

#define SENSOR_GPIO 9   // Пин, к которому подключён CH912

#define tag "SSD1306"

#define DIA 0.22

static nvs_handle_t my_handle = 0;

// Функция для получения handle (открывает при первом вызове)
esp_err_t get_nvs_handle() {
    if (my_handle == 0) {
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err != ESP_OK) {
            ESP_LOGE("NVS", "Error opening NVS handle: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI("NVS", "NVS handle opened");
    }
    return ESP_OK;
}

// Функции чтения/записи
esp_err_t nvs_read_value(const char* key, int32_t* value) {
    esp_err_t err = get_nvs_handle();
    if (err != ESP_OK) return err;
    
    printf("Reading %s from NVS ... ", key);
    err = nvs_get_i32(my_handle, key, value);
    
    switch (err) {
        case ESP_OK:
            ESP_LOGI("NVS READ", "Done. %s = %" PRIi32, key, *value);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGW("NVS READ", "The value for %s is not initialized yet!", key);
            *value = 0;
            err = ESP_OK;
            break;
        default:
            ESP_LOGE("NVS READ", "Error (%s) reading %s!", esp_err_to_name(err), key);
    }
    
    return err;
}

esp_err_t nvs_write_value(const char* key, int32_t value) {
    esp_err_t err = get_nvs_handle();
    if (err != ESP_OK) return err;
    
    printf("Updating %s in NVS ... ", key);
    err = nvs_set_i32(my_handle, key, value);
    
    if (err != ESP_OK) {
        ESP_LOGE("NVS WRITE", "Failed to update %s!", key);
        return err;
    }

    printf("Committing updates in NVS ... ");
    err = nvs_commit(my_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE("NVS WRITE", "Failed to commit %s!", key);
    } else {
        ESP_LOGI("NVS WRITE", "Successfully updated %s to %" PRIi32, key, value);
    }
    
    return err;
}

void app_main(void)
{
	nvs_handle_t my_handle;
	// Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

	// Читаем начальные значения
    int32_t rotations = 0;
	int32_t record = 0;

    err = nvs_read_value("rotations",&rotations);
    if (err != ESP_OK) {
        ESP_LOGE("APP", "Failed to read initial value!");
        return;
    }

    err = nvs_read_value("record",&record);
    if (err != ESP_OK) {
		ESP_LOGE("APP", "Failed to read initial value!");
        return;
    }
	
	printf("Initial rotations value: %" PRIi32 "\n", rotations);
	printf("Initial record value: %" PRIi32 "\n", record);

	char str_rotations[100];  
	char str_speed[100];  
	char str_record[100];  
	int str_len = 0;

    // Конфигурация GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SENSOR_GPIO | 1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,       // На всякий случай
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    ESP_LOGI("HALL_CH912", "Ожидание сигнала от CH912 на GPIO %d", SENSOR_GPIO);

    int prev_state = gpio_get_level(SENSOR_GPIO);

	SSD1306_t dev;
	int center, top, bottom;
	char lineChar[20];

	int count = 0;

	#if CONFIG_I2C_INTERFACE
		ESP_LOGI(tag, "INTERFACE is i2c");
		ESP_LOGI(tag, "CONFIG_SDA_GPIO=%d",SDA_GPIO);
		ESP_LOGI(tag, "CONFIG_SCL_GPIO=%d",SCL_GPIO);
		ESP_LOGI(tag, "CONFIG_RESET_GPIO=%d",RESET_GPIO);
		i2c_master_init(&dev, SDA_GPIO, SCL_GPIO, RESET_GPIO);
	#endif // CONFIG_I2C_INTERFACE

	#if CONFIG_SPI_INTERFACE
		ESP_LOGI(tag, "INTERFACE is SPI");
		ESP_LOGI(tag, "CONFIG_MOSI_GPIO=%d",MOSI_GPIO);
		ESP_LOGI(tag, "CONFIG_SCLK_GPIO=%d",SCLK_GPIO);
		ESP_LOGI(tag, "CONFIG_CS_GPIO=%d", CS_GPIO);
		ESP_LOGI(tag, "CONFIG_DC_GPIO=%d",DC_GPIO);
		ESP_LOGI(tag, "CONFIG_RESET_GPIO=%d",RESET_GPIO);
		spi_master_init(&dev, MOSI_GPIO, SCLK_GPIO, CS_GPIO, DC_GPIO, RESET_GPIO);
	#endif // CONFIG_SPI_INTERFACE

	#if CONFIG_FLIP
		dev._flip = true;
		ESP_LOGW(tag, "Flip upside down");
	#endif

	#if CONFIG_SSD1306_128x64
		ESP_LOGI(tag, "Panel is 128x64");
		ssd1306_init(&dev, 128, 64);
	#endif // CONFIG_SSD1306_128x64
	#if CONFIG_SSD1306_128x32
		ESP_LOGI(tag, "Panel is 128x32");
		ssd1306_init(&dev, 128, 32);
	#endif // CONFIG_SSD1306_128x32

	ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);

    str_len = snprintf(str_rotations, 100, "Distance: %.1f", (float)rotations*DIA*3.14);  
	ssd1306_display_text(&dev, 2, str_rotations, str_len, false);
	ssd1306_display_text(&dev, 3, "Km/h: 0.0", 9, false);
  
	str_len = snprintf(str_record, 100, "Max speed: %.1f", (float)record/100);  
	ssd1306_display_text(&dev, 4, str_record, str_len, false);


	int32_t oldtime = 0;
	int32_t time_between = 0;
	float rpm = 0;
	float speed = 0;
	int32_t int_speed = 0;

	while (1) {

		int curr_state = gpio_get_level(SENSOR_GPIO);
		int button = gpio_get_level(BUTTON_GPIO);
		int32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
		time_between = now - oldtime;
		
		if ((button == 0) && (record!=0 || rotations!=0)){
			ESP_LOGI("BUTTON PRESSED", "Erase NVS");
			int32_t rotations = 0;
			int32_t record = 0;
			err = nvs_write_value("rotations", rotations);
			if (err != ESP_OK) {
				ESP_LOGE("APP", "Failed to write value!");
			}
			err = nvs_write_value("record", record);
			if (err != ESP_OK) {
				ESP_LOGE("APP", "Failed to write value!");
			}
			ssd1306_clear_screen(&dev, false);
			ssd1306_display_text(&dev, 2, "Distance: 0", 11, false);
			ssd1306_display_text(&dev, 3, "Km/h: 0.0", 9, false);
			ssd1306_display_text(&dev, 4, "Max speed: 0.0", 14, false);
			// printf("New rotations: %" PRIi32 ", new Max speed: %" PRIi32 "\n", rotations, record);
			vTaskDelay(3000 / portTICK_PERIOD_MS);
		}
		
		// Отслеживаем нисходящий фронт: 1 → 0
		else if (prev_state == 1 && curr_state == 0) {
			
			rotations++;
			ESP_LOGI("HALL", "New rotation");
			// ssd1306_clear_screen(&dev, false);
			// ssd1306_contrast(&dev, 0xff);
			str_len = snprintf(str_rotations, 100, "Distance: %.1f", (float)rotations*DIA*3.14);  
			ssd1306_display_text(&dev, 2, str_rotations, str_len, false);
			    
			// printf("New rotations value: %" PRIi32 "\n", rotations);

			
			if (oldtime>0 && time_between<4000){
				float rpm = (1000/ (float)time_between)*60;
				float speed = rpm * DIA * 0.75398;
				int_speed = speed * 100;
				// printf("RPM: %f, Speed: %.1f \n", rpm, speed);
				// printf("Speed: %" PRIi32 " \n", int_speed);
				
				str_len = snprintf(str_speed, 100, "Km/h: %.1f", speed);  
				ssd1306_display_text(&dev, 3, str_speed, str_len, false);
			}
			
			else {ssd1306_display_text(&dev, 3, "Km/h: 0.0", 9, false);}

			oldtime = now;

			if (rotations%10 == 0){
				// Записываем новое значение
				err = nvs_write_value("rotations", rotations);
				if (err != ESP_OK) {
					ESP_LOGE("APP", "Failed to write value!");
				}
			}
			if (int_speed > record){
				record = int_speed;
				// Записываем новое значение
				err = nvs_write_value("record", record);
				// printf("New record value: %" PRIi32 "\n", record);
				if (err != ESP_OK) {
					ESP_LOGE("APP", "Failed to write value!");
				}
			}
			str_len = snprintf(str_record, 100, "Max speed: %.1f", (float)record/100);  
			ssd1306_display_text(&dev, 4, str_record, str_len, false);
			vTaskDelay(pdMS_TO_TICKS(100));  // Частота опроса: 100 Гц
		}

		else if (time_between>15000){
			ssd1306_clear_screen(&dev, false);
		}

		else if (time_between>4000){
			ssd1306_display_text(&dev, 3, "Km/h: 0.0    ", 13, false);
		}
		prev_state = curr_state;
		vTaskDelay(pdMS_TO_TICKS(10));  // Частота опроса: 100 Гц
	}	
}