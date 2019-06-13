/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_spi_flash.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "DEV_Config.h"
#include "GifDecoder.h"
#include "main.h"
#include <math.h>

#include "EPD_2in9b.h"

static const char *TAG = "epaper_badge";

RTC_DATA_ATTR uint8_t sleep_intervals;
RTC_DATA_ATTR uint8_t fileIndex;

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

EventGroupHandle_t render_event_group = NULL;
const int RENDER_EVENT_UPDATE_COMPLETE = BIT0;

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
    const char *szFile = foreground_files[fileIndex];
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

    update_display();

    DEV_ModuleInit();
    if(EPD_Init() != 0) {
        printf("e-Paper init failed\r\n");
    }
    EPD_Clear();
    EPD_Display(blackImage, redImage);
    EPD_Sleep();

    free(blackImage);
    blackImage = NULL;
    free(redImage);
    redImage = NULL;

    xEventGroupSetBits(render_event_group, RENDER_EVENT_UPDATE_COMPLETE);

    vTaskDelete(NULL);
}

extern "C" void keep_alive_task(void *params)
{
    // The power management controller on this board will sleep if we don't draw at least 45ma for 150ms once every 32s.
    // This task periodically pulls us into the highest wake state to prevent the sleep.

    esp_pm_lock_handle_t cpu_freq_lock_handle = NULL;
    esp_err_t ret = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "Keepalive CPU Freqency Lock", &cpu_freq_lock_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create keepalive CPU Freqency lock!\r\n");
        return;
    }
    esp_pm_lock_handle_t sleep_lock_handle = NULL;
    ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "Keepalive Sleep Lock", &sleep_lock_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create keepalive Sleep lock!\r\n");
        return;
    }
    for( ; ; ) {
        // Loop forever
        printf("Keep alive: Burning power...\r\n");
        ret = esp_pm_lock_acquire(cpu_freq_lock_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to acquire keepalive CPU Freqency lock!\r\n");
        }
        ret = esp_pm_lock_acquire(sleep_lock_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to acquire keepalive Sleep lock!\r\n");
        }
        DEV_Delay_ms(500);
        ret = esp_pm_lock_release(sleep_lock_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to release keepalive Sleep lock!\r\n");
        }
        ret = esp_pm_lock_release(cpu_freq_lock_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to release keepalive CPU Freqency lock!\r\n");
        }
        printf("Keep alive: Done.\r\n");
        DEV_Delay_ms(10000);
    }
}


#define BADGE_ADVANCE_BUTTON_PIN 37

extern "C" int app_main()
{
    printf("We're awake!\r\n");

    render_event_group = xEventGroupCreate();

    // --- input pins ---
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    //disable interrupt
    io_conf.intr_type = (gpio_int_type_t)GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = (1ULL<<BADGE_ADVANCE_BUTTON_PIN);
    //disable pull-down mode
    io_conf.pull_down_en = (gpio_pulldown_t)0;
    //disable pull-up mode
    io_conf.pull_up_en = (gpio_pullup_t)0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    esp_pm_lock_handle_t cpu_freq_lock_handle = NULL;
    esp_err_t ret = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "Keepalive CPU Freqency Lock", &cpu_freq_lock_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create keepalive CPU Freqency lock!\r\n");
    }

    ret = esp_pm_lock_acquire(cpu_freq_lock_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire keepalive CPU Freqency lock!\r\n");
    }

    bool doDisplayUpdate = false;
    if(gpio_get_level((gpio_num_t)BADGE_ADVANCE_BUTTON_PIN) == 0) {
        printf("Advance button pressed!\r\n");
        // Rotate through the images on demand
        fileIndex = (fileIndex + 1) % kForegroundCount;
        doDisplayUpdate = true;
    } else if (sleep_intervals == 0) {
        printf("Automatic display update now choose random image...\r\n");
        // Select a random gif, other than the one last displayed
        int newFileIndex = esp_random() % (kForegroundCount - 1);
        if (newFileIndex >= fileIndex) {
            newFileIndex++;
        }
        fileIndex = newFileIndex;
        doDisplayUpdate = true;
    } else {
        printf("Sleep intervals remaining: %i\r\n", sleep_intervals);
        sleep_intervals--;
    }


//    xTaskCreatePinnedToCore(keep_alive_task, "Keep Alive", 1024, NULL, 1, NULL, 1);
    if (doDisplayUpdate) {
        printf("Time to update display.\r\n");
        bool spiffs_ready = init_spiffs();
        if (spiffs_ready) {
            xTaskCreatePinnedToCore(render_task, "Render", 32768, NULL, 1, NULL, 1);
        }
        sleep_intervals = 6;
        printf("Waiting for display update...\r\n");
        xEventGroupWaitBits(render_event_group,RENDER_EVENT_UPDATE_COMPLETE ,true,true,portMAX_DELAY);
        printf("Display refresh completed...\r\n");
        destroy_spiffs();
    }

    // Ensure we are burning power for at least 500ms to prevent
    // IP5306 from going to sleep
    printf("Keep alive: Burning power...\r\n");
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    tcpip_adapter_init();

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed.\r\n");
    }
    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_storage failed.\r\n");
    }
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed.\r\n");
    }
    wifi_config_t ap_config = {};
    sprintf((char *)ap_config.ap.ssid, "KIPBADGE");
    ap_config.ap.channel = 0;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.ssid_hidden = 1;
    ap_config.ap.max_connection = 1;
    ap_config.ap.beacon_interval = 100;
 
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed.\r\n");
    }
    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_ps failed.\r\n");
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed.\r\n");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
    ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_stop failed.\r\n");
    }
    printf("Keep alive: Done.\r\n");

    ret = esp_pm_lock_release(cpu_freq_lock_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release keepalive CPU Freqency lock!\r\n");
    }

    printf("Going to sleep for 10 seconds...\r\n");
    fflush(stdout);


    ret = esp_pm_lock_delete(cpu_freq_lock_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete keepalive CPU Freqency lock!\r\n");
    }

    ret = esp_sleep_enable_ext0_wakeup((gpio_num_t)BADGE_ADVANCE_BUTTON_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_sleep_enable_ext0_wakeup failed!\r\n");
    }
    esp_sleep_enable_timer_wakeup(10 * 1000 * 1000);
    esp_deep_sleep_start();

    return 0;
}
