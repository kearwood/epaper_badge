/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "main.h"

#include <driver/i2c.h>

//#define I2C_MASTER_SCL         19 /* Use yellow wire. */
//#define I2C_MASTER_SDA         18 /* Use green wire. */

#define I2C_MASTER_SCL         22
#define I2C_MASTER_SDA         21
#define I2C_MASTER_NUM         I2C_NUM_1
#define I2C_MASTER_TX_BUF_LEN  0
#define I2C_MASTER_RX_BUF_LEN  0
#define I2C_MASTER_FREQ_HZ     100000

#define ACK_CHECK_ENABLE       0x1 /* Master will require ack from slave */
#define ACK_CHECK_DISABLE      0x0
#define ACK_VAL                0x0
#define NACK_VAL               0x1

void i2c_master_init();
void i2c_master_scan();
esp_err_t i2c_master_probe(uint8_t address);


void i2c_master_init()
{
    i2c_port_t i2c_master_port = I2C_MASTER_NUM;
//    ESP_LOGD(TAG, "Starting I2C master at port %d.", i2c_master_port);

    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)I2C_MASTER_SDA;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    conf.scl_io_num = (gpio_num_t)I2C_MASTER_SCL;
    conf.scl_pullup_en = (gpio_pullup_t)GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;

    ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(
        i2c_master_port,
        conf.mode,
        I2C_MASTER_RX_BUF_LEN,
        I2C_MASTER_TX_BUF_LEN,
        0
    ));
}

esp_err_t i2c_master_probe(uint8_t address)
{
    esp_err_t result;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, 1 /* expect ack */);
    i2c_master_stop(cmd);

    result = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 10 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    return result;
}

void i2c_master_scan()
{
    uint8_t address;
    esp_err_t result;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    printf("00:         ");
    for (address = 3; address < 0x78; address++) {
        result = i2c_master_probe(address);

        if (address % 16 == 0) {
            printf("\n%.2x:", address);
        }
        if (result == ESP_OK) {
            printf(" %.2x", address);
        } else {
            printf(" --");
        }
    }
    printf("\n");
}

extern "C" void i2c_scan_task(void *params)
{
    while(1) {
        i2c_master_scan();
        vTaskDelay(3000 / portTICK_RATE_MS);
    }

    vTaskDelete(NULL);
}

extern "C" int app_main()
{
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

printf("Checking status...\n");
i2c_cmd_handle_t cmd = i2c_cmd_link_create();

/*
i2c_master_start(cmd);
i2c_master_write_byte(cmd, (0x75 << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
i2c_master_write_byte(cmd, 0x78, ACK_CHECK_EN);
i2c_master_stop(cmd);

result = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 10 / portTICK_PERIOD_MS);
i2c_cmd_link_delete(cmd);
*/

printf("Scanning I2C Bus...\n");
i2c_master_init();
xTaskCreatePinnedToCore(i2c_scan_task, "I2C scan", 2048, NULL, 1, NULL, 1);
vTaskDelay(10000 / portTICK_PERIOD_MS);
    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
    return 0;
}
