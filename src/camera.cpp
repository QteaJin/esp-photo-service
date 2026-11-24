#include "camera.h"
#include "pins.h"
#include "log.h"

static const framesize_t FULL_SIZE = FRAMESIZE_FHD;   // 1920x1080, как в main
static const framesize_t PREVIEW_SIZE = FRAMESIZE_VGA; // 640x480
static SemaphoreHandle_t camMutex = nullptr;
static ColorSettings curColor;

// Вызывать при занятом camMutex (или до старта задач). Смена разрешения может сбросить параметры сенсора.
static void applyColor(sensor_t *s) {
  s->set_brightness(s, curColor.brightness);
  s->set_contrast(s, curColor.contrast);
  s->set_saturation(s, curColor.saturation);
  s->set_ae_level(s, curColor.aeLevel);
  s->set_wb_mode(s, curColor.wbMode);
  s->set_lenc(s, curColor.lenc ? 1 : 0);
}

static int clampInt(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

ColorSettings cameraGetColor() {
  xSemaphoreTake(camMutex, portMAX_DELAY);
  ColorSettings c = curColor;
  xSemaphoreGive(camMutex);
  return c;
}

void cameraSetColor(const ColorSettings &color) {
  xSemaphoreTake(camMutex, portMAX_DELAY);
  curColor.brightness = clampInt(color.brightness, -2, 2);
  curColor.contrast = clampInt(color.contrast, -2, 2);
  curColor.saturation = clampInt(color.saturation, -2, 2);
  curColor.aeLevel = clampInt(color.aeLevel, -2, 2);
  curColor.wbMode = clampInt(color.wbMode, 0, 4);
  curColor.lenc = color.lenc;
  sensor_t *s = esp_camera_sensor_get();
  if (s) applyColor(s);
  xSemaphoreGive(camMutex);
}

bool cameraInit(const ColorSettings &color) {
  camMutex = xSemaphoreCreateMutex();
  curColor = color;

  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FULL_SIZE;
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    LOGE("camera: init failed, error 0x%x", err);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    LOG("camera: sensor PID 0x%04x (OV3660=0x3660, OV5640=0x5640)", s->id.PID);
    s->set_whitebal(s, 1);      // авто баланс белого вместо fix_color_balance
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_aec2(s, 1);
    applyColor(s);
  }
  return true;
}

camera_fb_t *cameraAcquire(bool preview) {
  xSemaphoreTake(camMutex, portMAX_DELAY);
  sensor_t *s = esp_camera_sensor_get();
  if (preview && s) {
    s->set_framesize(s, PREVIEW_SIZE);
    s->set_vflip(s, 1);    // поворот на 180
    s->set_hmirror(s, 1);
    applyColor(s);
    // выбросить устаревшие кадры после смены режима
    for (int i = 0; i < 4; i++) {
      camera_fb_t *old = esp_camera_fb_get();
      if (old) esp_camera_fb_return(old);
    }
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    LOGE("camera: esp_camera_fb_get failed (preview=%d)", preview ? 1 : 0);
    cameraRelease(nullptr, preview);
  }
  return fb;
}

void cameraRelease(camera_fb_t *fb, bool preview) {
  if (fb) esp_camera_fb_return(fb);
  if (preview) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      s->set_framesize(s, FULL_SIZE);
      s->set_vflip(s, 0);
      s->set_hmirror(s, 0);
      applyColor(s);
    }
  }
  xSemaphoreGive(camMutex);
}
