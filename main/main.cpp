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
#include "DEV_Config.h"
#include "GifDecoder.h"
#include "main.h"
#include <math.h>

#include "EPD_2in9b.h"

static const char *TAG = "epaper_badge";

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

const int kForegroundCount = 7;
const char* foreground_files[] = {
  "/spiffs/dino.gif",
  "/spiffs/youtube.gif",
  "/spiffs/twitter.gif",
  "/spiffs/namebottom.gif",
  "/spiffs/mozillamr.gif",
  "/spiffs/fxrlogo.gif",
  "/spiffs/github.gif"
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

extern "C" void update_display()
{
    // Generate random seeds
    for (int i=0; i<32; i++) {
        seed[i] = (float)(esp_random() % 65536) / 65536.0f;
    }

    // Select a random effect
    effect_t effect = effects[esp_random() % kEffectCount];

    fnRender = effect.render;
    fnDither = effect.dither;

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
    const char *szFile = foreground_files[esp_random() % kForegroundCount];
    printf("Loading %s..\r\n", szFile);
    
    GifDecoder<EPD_HEIGHT, EPD_HEIGHT, 12> decoder;
    decoder.setDrawPixelCallback(gifDrawPixelCallback);

    decoder.setFileSeekCallback(gifFileSeekCallback);
    decoder.setFilePositionCallback(gifFilePositionCallback);
    decoder.setFileReadCallback(gifFileReadCallback);
    decoder.setFileReadBlockCallback(gifFileReadBlockCallback);


    gifFile = fopen(szFile, "rb");
    gifFilePos = 0;
    decoder.startDecoding();
    decoder.decodeFrame();
    fclose(gifFile);
}

bool init_spiffs()
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
          .base_path = "/spiffs",
          .partition_label = NULL,
          .max_files = 10,
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

extern "C" void render_task(void *params)
{
    blackImage = (__uint8_t *)malloc(EPD_WIDTH * EPD_HEIGHT / 8);
    redImage = (__uint8_t *)malloc(EPD_WIDTH * EPD_HEIGHT / 8);

    if(EPD_Init() != 0) {
        printf("e-Paper init failed\r\n");
    }
    EPD_Clear();

    for(int i=0; i<10; i++) {
        update_display();
        EPD_Display(blackImage, redImage);
        DEV_Delay_ms(5000);
    }

    EPD_Sleep();
    free(blackImage);
    blackImage = NULL;
    free(redImage);
    redImage = NULL;
    vTaskDelete(NULL);
}

extern "C" int app_main()
{
    DEV_ModuleInit();
    bool spiffs_ready = init_spiffs();
    if (spiffs_ready) {
        xTaskCreatePinnedToCore(render_task, "Render", 32768, NULL, 1, NULL, 1);
    }

    vTaskDelay(5*60*1000 / portTICK_PERIOD_MS);
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
