#include "esp_camera.h"
#include <WiFi.h>
#include "Arduino.h"
// Time
#include "time.h"
#include "lwip/err.h"
#include "lwip/apps/sntp.h"
// MicroSD
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

// Edit ssid, password, capture_interval:
const char* ssid = "NSA";
const char* password = "orange";
int capture_interval = 5000; // milliseconds between captures
//

long current_millis;
long last_capture_millis = 0;
static esp_err_t cam_err;
static esp_err_t card_err;
char strftime_buf[64];
int file_number = 0;
bool internet_connected = false;
struct tm timeinfo;
time_t now;


// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
  Serial.begin(115200);

  if (init_wifi()) { // Connected to WiFi
    internet_connected = true;
    Serial.println("Internet connected");
    init_time();
    time(&now);
    setenv("TZ", "GMT0BST,M3.5.0/01,M10.5.0/02", 1);
    tzset();
  }

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  cam_err = esp_camera_init(&config);
  if (cam_err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", cam_err);
    return;
  }
  // SD camera init
  card_err = init_sdcard();
  if (card_err != ESP_OK) {
    Serial.printf("SD Card init failed with error 0x%x", card_err);
    return;
  }
}

bool init_wifi()
{
  int connAttempts = 0;
  Serial.println("\r\nConnecting to: " + String(ssid));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED ) {
    delay(500);
    Serial.print(".");
    if (connAttempts > 10) return false;
    connAttempts++;
  }
  return true;
}

void init_time()
{
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_init();
  // wait for time to be set
  time_t now = 0;
  timeinfo = { 0 };
  int retry = 0;
  const int retry_count = 10;
  while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
    Serial.printf("Waiting for system time to be set... (%d/%d)\n", retry, retry_count);
    delay(2000);
    time(&now);
    localtime_r(&now, &timeinfo);
  }
}

static esp_err_t init_sdcard()
{
  esp_err_t ret = ESP_FAIL;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 1,
  };
  sdmmc_card_t *card;

  Serial.println("Mounting SD card...");
  ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

  if (ret == ESP_OK) {
    Serial.println("SD card mount successfully!");
  }  else  {
    Serial.printf("Failed to mount SD card VFAT filesystem. Error: %s", esp_err_to_name(ret));
  }
}

static esp_err_t save_photo_numbered()
{
  file_number++;
  Serial.print("Taking picture: ");
  Serial.print(file_number);
  camera_fb_t *fb = esp_camera_fb_get();

  //char *filename = (char*)malloc(21 + sizeof(int));
  char *filename = (char*)malloc(21 + sizeof(file_number));
  sprintf(filename, "/sdcard/capture_%d.jpg", file_number);

  Serial.println(filename);
  FILE *file = fopen(filename, "w");
  if (file != NULL)  {
    size_t err = fwrite(fb->buf, 1, fb->len, file);
    Serial.printf("File saved: %s\n", filename);
  }  else  {
    Serial.println("Could not open file");
  }
  fclose(file);
  esp_camera_fb_return(fb);
  free(filename);
}

static esp_err_t save_photo_dated()
{
  Serial.println("Taking picture...");
  camera_fb_t *fb = esp_camera_fb_get();

  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%F_%H_%M_%S", &timeinfo);

  char *filename = (char*)malloc(21 + sizeof(strftime_buf));
  sprintf(filename, "/sdcard/capture_%s.jpg", strftime_buf);

  Serial.println(filename);
  FILE *file = fopen(filename, "w");
  if (file != NULL)  {
    size_t err = fwrite(fb->buf, 1, fb->len, file);
    Serial.printf("File saved: %s\n", filename);

  }  else  {
    Serial.println("Could not open file");
  }
  fclose(file);
  esp_camera_fb_return(fb);
  free(filename);
}

void save_photo()
{
  if (timeinfo.tm_year < (2016 - 1900) || internet_connected == false) { // if no internet or time not set
    save_photo_numbered(); // filenames in numbered order
  } else {
    save_photo_dated(); // filenames with date and time
  }
}

void loop()
{
  current_millis = millis();
  if (current_millis - last_capture_millis > capture_interval) { // Take another picture
    last_capture_millis = millis();
    save_photo();
  }
}
