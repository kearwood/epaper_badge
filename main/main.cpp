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
#include "esp_spiffs.h"
#include "esp_log.h"
#include "GUI_Paint.h"
#include "GUI_BMPfile.h"
#include "DEV_Config.h"
#include "ImageData.h"
#include "GifDecoder.h"
#include "main.h"
#include <math.h>

#include "EPD_2in9b.h"

#include <driver/i2c.h>

static const char *TAG = "epaper_badge";

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

extern "C" void epd_demo(void *params)
{
    printf("2.9inch e-Paper b(c) demo\r\n");
    DEV_ModuleInit();

    if(EPD_Init() != 0) {
        printf("e-Paper init failed\r\n");
    }
    printf("EPD_Clear\r\n");
    EPD_Clear();
    printf("DEV_Delay_ms(500)\r\n");
    DEV_Delay_ms(500);

    //Create a new image cache named IMAGE_BW and fill it with white
    UBYTE *BlackImage, *RedImage;
    UWORD Imagesize = ((EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1)) * EPD_HEIGHT;
    if((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
        printf("Failed to apply for black memory...\r\n");
        exit(0);
    }
    if((RedImage = (UBYTE *)malloc(Imagesize)) == NULL) {
        printf("Failed to apply for red memory...\r\n");
        exit(0);
    }
    printf("NewImage:BlackImage and RedImage\r\n");
    Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_NewImage(RedImage, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);

    //Select Image
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
    Paint_SelectImage(RedImage);
    Paint_Clear(WHITE);

#if 0   // show bmp
    printf("show windows------------------------\r\n");
    printf("read black bmp\r\n");
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
    GUI_ReadBmp("./pic/100x100.bmp", 50, 10);

    printf("read red bmp\r\n");
    Paint_SelectImage(RedImage);
    Paint_Clear(WHITE);
    
    EPD_Display(BlackImage, RedImage);
    DEV_Delay_ms(2000);

    printf("show bmp------------------------\r\n");
    printf("read black bmp\r\n");
    Paint_SelectImage(BlackImage);
    GUI_ReadBmp("./pic/2in9b-b.bmp", 0, 0);
    printf("read red bmp\r\n");
    Paint_SelectImage(RedImage);
    GUI_ReadBmp("./pic/2in9b-r.bmp", 0, 0);

    EPD_Display(BlackImage, RedImage);
    DEV_Delay_ms(2000);
#endif

#if 1   // show image for array    
    printf("show image for array\r\n");    
    EPD_Display(gImage_2in9b_b, gImage_2in9b_r);
    DEV_Delay_ms(2000);
#endif

#if 1   // Drawing on the image
    /*Horizontal screen*/
    //1.Draw black image
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
    Paint_DrawPoint(10, 80, BLACK, DOT_PIXEL_1X1, DOT_STYLE_DFT);
    Paint_DrawPoint(10, 90, BLACK, DOT_PIXEL_2X2, DOT_STYLE_DFT);
    Paint_DrawPoint(10, 100, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
    Paint_DrawPoint(10, 110, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
    Paint_DrawLine(20, 70, 70, 120, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_1X1);
    Paint_DrawLine(70, 70, 20, 120, BLACK, LINE_STYLE_SOLID, DOT_PIXEL_1X1);      
    Paint_DrawRectangle(20, 70, 70, 120, BLACK, DRAW_FILL_EMPTY, DOT_PIXEL_1X1);
    Paint_DrawRectangle(80, 70, 130, 120, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawString_EN(10, 0, "waveshare", &Font16, BLACK, WHITE);    
    // Paint_DrawString_CN(130, 20,"΢ѩ???, &Font24CN, WHITE, BLACK);
    Paint_DrawNum(10, 50, 987654321, &Font16, WHITE, BLACK);
    
    //2.Draw red image
    Paint_SelectImage(RedImage);
    Paint_Clear(WHITE);
    Paint_DrawCircle(160, 95, 20, BLACK, DRAW_FILL_EMPTY, DOT_PIXEL_1X1);
    Paint_DrawCircle(210, 95, 20, BLACK, DRAW_FILL_FULL, DOT_PIXEL_1X1);
    Paint_DrawLine(85, 95, 125, 95, BLACK, LINE_STYLE_DOTTED, DOT_PIXEL_1X1);
    Paint_DrawLine(105, 75, 105, 115, BLACK, LINE_STYLE_DOTTED, DOT_PIXEL_1X1);  
    // Paint_DrawString_CN(130, 0,"???bc?ݮ?", &Font12CN, BLACK, WHITE);
    Paint_DrawString_EN(10, 20, "hello world", &Font12, WHITE, BLACK);
    Paint_DrawNum(10, 33, 123456789, &Font12, BLACK, WHITE);
    
    printf("EPD_Display\r\n");
    EPD_Display(BlackImage, RedImage);
    DEV_Delay_ms(2000);
#endif

    printf("Goto Sleep mode...\r\n");
    EPD_Sleep();
    free(BlackImage);
    BlackImage = NULL;
    free(RedImage);
    RedImage = NULL;

    vTaskDelete(NULL);
}

float seed[32];

typedef int (*DitherFunc)(float c, int x, int y);
typedef int (*RenderFunc)(int x, int y);

DitherFunc fnDither;
RenderFunc fnRender;

int dither_random(float c, int x, int y) {
    int d = floor(c);
    int r = (int)((c - (float)d) * 255.0f);
    if (r > esp_random() % 256) {
        d++;
    }
    d = d % 3;
    return d;
}

int dither_nearest(float c, int x, int y) {
    return (int)floor(c) % 3;
}

int dither_slice(float c, int x, int y) {
    int d = floor(c);
    int slice_width = (int)(64.0f * seed[31]) + 2;
    int r = (int)((c - (float)d) * slice_width);
    if (r > (x + y) % slice_width) {
        d++;
    }
    d = d % 3;
    return d;
}

int dither_circles(float c, int x, int y) {
    int d = floor(c);
    int slice_width = (int)(64.0f * seed[31]) + 2;
    float r = ((c - (float)d) * slice_width);

    float cx = seed[30] * (float)EPD_WIDTH;
    float cy = seed[29] * (float)EPD_HEIGHT;
    float dist = sqrtf(fabs(x - cx)*fabs(x - cx) + fabs(y - cy)*fabs(y - cy));

    if (r > (int)dist % slice_width) {
        d++;
    }
    d = d % 3;
    return d;
}

int render_plasma(int x, int y) {
    float scale = (float)EPD_HEIGHT;
    float tx = (float)x / scale;
    float ty = (float)y / scale;
    float ox = tx;
    float oy = ty;
    tx += sin((float)oy * (seed[10] - 0.5f) * 5.0f + seed[12]) * seed[19];
    ty += sin((float)ox * (seed[11] - 0.5f) * 5.0f + seed[13]) * seed[20];
    float height = sin(tx * 20.0f * (seed[0] - 0.5f) + ty  * 20.0f * (seed[1] - 0.5f) + seed[14] * 10.0f) * sin(tx * 20.0f * (seed[2] - 0.5f) + ty * 20.0f * (seed[3] - 0.5f) + seed[15] * 10.0f) * (seed[9] - 0.5f)
    + sin(tx / scale * 20.0f * (seed[4] - 0.5f) + ty / scale * 20.0f * (seed[5] - 0.5f) + seed[16] * 10.0f) * sin(tx * 20.0f * (seed[6] - 0.5f) + ty * 20.0f * (seed[7] - 0.5f) + seed[17] * 10.0f) * (seed[8] - 0.5f);

    int c = fnDither((height + 0.5f) * 10.0f * seed[18] + 1.0f, x, y);
    return c;
}

typedef struct {
    RenderFunc render;
    DitherFunc dither;
} effect_t;

const int kEffectCount = 3;
effect_t effects[] = {
  { render_plasma, dither_random },
  { render_plasma, dither_slice },
  { render_plasma, dither_circles },
};

FILE* gifFile = 0;
unsigned long gifFilePos = 0;

__uint8_t *blackImage = NULL;
__uint8_t *redImage = NULL;

extern "C" void gifDrawPixelCallback(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue) {
// printf("gifDrawPixelCallback\r\n");

  if (green != 0 && red == 0 && blue == 0) {
    // Hack green to be transparent
    return; 
  }
  x = EPD_HEIGHT - x;
  size_t offset = (y + x * EPD_WIDTH) / 8;
  int bit = 7 - (y % 8);

  int color = 1;
  if (red != 0) {
    color = 2;
  }
  if (blue != 0) {
    color = 3;
  }
  if (color & 0x01) {
    redImage[offset] |= (1 << bit);
  } else {
    redImage[offset] &= ~(1 << bit);
  }
  if (color & 0x02) {
    blackImage[offset] |= (1 << bit);
  } else {
    blackImage[offset] &= ~(1 << bit);
  }
}


extern "C" bool gifFileSeekCallback(unsigned long position)
{
  if (fseek(gifFile, position, SEEK_SET) == 0) {
    gifFilePos = position;
    return true;
  }
  return false;
}

extern "C" unsigned long gifFilePositionCallback(void)
{
    return gifFilePos;
}

extern "C" int gifFileReadCallback()
{
    gifFilePos++;
  return fgetc(gifFile);
}

extern "C" int gifFileReadBlockCallback(void *buffer, int numberOfBytes)
{
  size_t read = fread(buffer, numberOfBytes, 1, gifFile);
  gifFilePos += numberOfBytes;
  if (read != 1) {
    return -1;
  }
  return 0;
}

extern "C" void update_display(void *params)
{
    // Generate random seeds
    for (int i=0; i<32; i++) {
        seed[i] = (float)(esp_random() % 65536) / 65536.0f;
    }

    // Select a random effect
    effect_t effect = effects[esp_random() % kEffectCount];

    fnRender = effect.render;
    fnDither = effect.dither;

    blackImage = (__uint8_t *)malloc(EPD_WIDTH * EPD_HEIGHT / 8);
    redImage = (__uint8_t *)malloc(EPD_WIDTH * EPD_HEIGHT / 8);

    __uint8_t *blackDest = blackImage;
    __uint8_t *redDest = redImage;

    for (int y=0; y<EPD_HEIGHT; y++) {
        for (int x=0; x<EPD_WIDTH; x++) {
          if (x % 8 == 0) {
            *blackDest = 0;
            *redDest = 0;
          }
          int color=fnRender(x,y) + 1;

          if (color & 0x01) {
            *blackDest |= 1;
          }
          if (color & 0x02) {
            *redDest |= 1;
          }
          if (x % 8 == 7) {
            blackDest++;
            redDest++;
          } else {
            *blackDest <<= 1;
            *redDest <<= 1;
          }
        }
    }

    // ---- Composite GIF ----
    printf("Loading gif..\r\n");
    
    GifDecoder<EPD_HEIGHT, EPD_HEIGHT, 12> decoder;

    printf("Loading gif a..\r\n");

    decoder.setDrawPixelCallback(gifDrawPixelCallback);

    decoder.setFileSeekCallback(gifFileSeekCallback);
    decoder.setFilePositionCallback(gifFilePositionCallback);
    decoder.setFileReadCallback(gifFileReadCallback);
    decoder.setFileReadBlockCallback(gifFileReadBlockCallback);
    printf("Loading gif b..\r\n");

    gifFile = fopen("/spiffs/dino.gif", "rb");
    printf("Loading gif c..\r\n");
    gifFilePos = 0;
    decoder.startDecoding();
    printf("Loading gif cb..\r\n");
    decoder.decodeFrame();
    fclose(gifFile);
    printf("Loading gif d..\r\n");

    if(EPD_Init() != 0) {
        printf("e-Paper init failed\r\n");
    }
    EPD_Clear();
    EPD_Display(blackImage, redImage);
    DEV_Delay_ms(500);

    EPD_Sleep();
    free(blackImage);
    blackImage = NULL;
    free(redImage);
    redImage = NULL;
    vTaskDelete(NULL);
}

bool init_spiffs()
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
          .base_path = "/spiffs",
          .partition_label = NULL,
          .max_files = 5,
          .format_if_mount_failed = false
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return false;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return true;
}

void destroy_spiffs()
{
    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

extern "C" int app_main()
{
  bool spiffs_ready = init_spiffs();
/*
    printf("Hello world!\n");
    //Print chip information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
*/

    if (spiffs_ready) {
        DEV_ModuleInit();
        xTaskCreatePinnedToCore(update_display, "Update Display", 32768, NULL, 1, NULL, 1);
    }
    // xTaskCreatePinnedToCore(epd_demo, "EPD Demo", 2048, NULL, 1, NULL, 1);

/*
printf("Scanning I2C Bus...\n");
i2c_master_init();
xTaskCreatePinnedToCore(i2c_scan_task, "I2C scan", 2048, NULL, 1, NULL, 1);
*/

    vTaskDelay(20000 / portTICK_PERIOD_MS);
    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    destroy_spiffs();
    esp_restart();
    return 0;
}
