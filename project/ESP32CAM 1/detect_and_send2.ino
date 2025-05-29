#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEBeacon.h>


#include <card_detector2_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "esp_camera.h"

//––––––––––––––––––––––––––––––––––––––––––––
//——— BLE + iBeacon definitions ————————
#define DEVICE_NAME           "ESP32-Blackjack"
#define SERVICE_UUID          "18EE1516-016B-4BEC-AD96-BCB96D166E97"
#define CHARACTERISTIC_UUID   SERVICE_UUID
#define BEACON_UUID_REV       "976E166D-B9BC-96AD-EC4B-6B011615EE18"

BLEServer        *pServer         = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
BLEAdvertising   *pAdvertising    = nullptr;
bool               deviceConnected = false;
uint8_t            notifyValue     = 0;
uint16_t           lastMajor       = 0;

// Server connect/disconnect callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect (BLEServer*)    override {
    deviceConnected = true;
    Serial.println("Client connected");
  }
  void onDisconnect (BLEServer* s) override {
    deviceConnected = false;
    Serial.println("Client disconnected, restarting adv");
    pAdvertising->start();
  }
};

// Characteristic write callback (optional)
class MyCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if (v.length()) {
      Serial.print("→ Received via GATT: ");
      Serial.println(v);
    }
  }
};

void init_ble_service() {
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  pAdvertising = pServer->getAdvertising();
  pAdvertising->stop();

  auto* svc = pServer->createService(BLEUUID(SERVICE_UUID));
  pCharacteristic = svc->createCharacteristic(
    BLEUUID(CHARACTERISTIC_UUID),
    BLECharacteristic::PROPERTY_READ   |
    BLECharacteristic::PROPERTY_WRITE  |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->setCallbacks(new MyCharCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pAdvertising->addServiceUUID(BLEUUID(SERVICE_UUID));

  svc->start();
  pAdvertising->start();
}

void update_beacon_major(uint16_t major) {
  pAdvertising->stop();

  BLEBeacon beacon;
  beacon.setManufacturerId(0x4c00);       // Apple
  beacon.setProximityUUID(BLEUUID(BEACON_UUID_REV));
  beacon.setMajor(major);
  beacon.setMinor(88);
  beacon.setSignalPower(0xC5);            // calibrated RSSI

  BLEAdvertisementData advData;
  advData.setFlags(0x1A);
  advData.setManufacturerData(beacon.getData());

  pAdvertising->setAdvertisementData(advData);
  pAdvertising->start();

  Serial.printf("🃏 Beacon major set to %u\n", major);
}

//––––––––––––––––––––––––––––––––––––––––––––
//——— Camera + Edge Impulse definitions ————
#define CAMERA_MODEL_AI_THINKER
// AI-Thinker ESP32-CAM pinout:
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

#define EI_CAMERA_RAW_FRAME_BUFFER_COLS 320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS 240
#define EI_CAMERA_FRAME_BYTE_SIZE       3

static bool debug_nn       = false;
static bool is_initialised = false;
static uint8_t *snapshot_buf;

// QVGA-JPEG in PSRAM
static camera_config_t camera_config = {
  .pin_pwdn       = PWDN_GPIO_NUM,
  .pin_reset      = RESET_GPIO_NUM,
  .pin_xclk       = XCLK_GPIO_NUM,
  .pin_sscb_sda   = SIOD_GPIO_NUM,
  .pin_sscb_scl   = SIOC_GPIO_NUM,
  .pin_d7         = Y9_GPIO_NUM,
  .pin_d6         = Y8_GPIO_NUM,
  .pin_d5         = Y7_GPIO_NUM,
  .pin_d4         = Y6_GPIO_NUM,
  .pin_d3         = Y5_GPIO_NUM,
  .pin_d2         = Y4_GPIO_NUM,
  .pin_d1         = Y3_GPIO_NUM,
  .pin_d0         = Y2_GPIO_NUM,
  .pin_vsync      = VSYNC_GPIO_NUM,
  .pin_href       = HREF_GPIO_NUM,
  .pin_pclk       = PCLK_GPIO_NUM,
  .xclk_freq_hz   = 20000000,
  .ledc_timer     = LEDC_TIMER_0,
  .ledc_channel   = LEDC_CHANNEL_0,
  .pixel_format   = PIXFORMAT_JPEG,
  .frame_size     = FRAMESIZE_QVGA,
  .jpeg_quality   = 12,
  .fb_count       = 1,
  .fb_location    = CAMERA_FB_IN_PSRAM,
  .grab_mode      = CAMERA_GRAB_WHEN_EMPTY,
};

bool ei_camera_init() {
  if (is_initialised) return true;
  if (esp_camera_init(&camera_config) != ESP_OK) {
    Serial.println("Camera init failed");
    return false;
  }
  is_initialised = true;
  return true;
}

bool ei_camera_capture(uint32_t img_w, uint32_t img_h, uint8_t *out_buf) {
  if (!is_initialised) return false;
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;

  bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, out_buf);
  esp_camera_fb_return(fb);
  if (!ok) return false;

  if (img_w != EI_CAMERA_RAW_FRAME_BUFFER_COLS ||
      img_h != EI_CAMERA_RAW_FRAME_BUFFER_ROWS) {
    ei::image::processing::crop_and_interpolate_rgb888(
      out_buf,
      EI_CAMERA_RAW_FRAME_BUFFER_COLS,
      EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
      out_buf,
      img_w, img_h
    );
  }
  return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
  size_t ix = offset * 3;
  for (size_t i = 0; i < length; i++, ix += 3) {
    out_ptr[i] = (snapshot_buf[ix+2] << 16)
               | (snapshot_buf[ix+1] << 8)
               |  snapshot_buf[ix+0];
  }
  return 0;
}

//––––––––––––––––––––––––––––––––––––––––––––
//——— Arduino setup & loop ————————————

void setup() {
  Serial.begin(115200);
  while (!Serial);

  init_ble_service();
  update_beacon_major(0);  // initial beacon

  if (!ei_camera_init()) {
    Serial.println("Camera failed");
    while (1) delay(1000);
  }
  Serial.println("Blackjack assistant ready!");
}

void loop() {
  if (ei_sleep(2000) != EI_IMPULSE_OK) return;

  // Allocate image buffer
  snapshot_buf = (uint8_t*)malloc(
    EI_CAMERA_RAW_FRAME_BUFFER_COLS *
    EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
    EI_CAMERA_FRAME_BYTE_SIZE
  );
  if (!snapshot_buf) {
    Serial.println("Mem alloc failed");
    return;
  }

  // Prepare EI signal
  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data     = &ei_camera_get_data;

  if (!ei_camera_capture(
        EI_CLASSIFIER_INPUT_WIDTH,
        EI_CLASSIFIER_INPUT_HEIGHT,
        snapshot_buf
      )) {
    Serial.println("Capture failed");
    free(snapshot_buf);
    return;
  }

  // Run the model
  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
  if (err != EI_IMPULSE_OK) {
    ei_printf("ERR: Failed to run classifier (%d)\r\n", err);
    free(snapshot_buf);
    return;
  }

  // Print EI predictions
  ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.):\n",
            result.timing.dsp,
            result.timing.classification,
            result.timing.anomaly);

  const char *top_label = nullptr;

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
  ei_printf("Object detection bounding boxes:\n");
  float   best_bb_val = 0.f;
  uint32_t best_bb_ix = 0;
  for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
    auto &bb = result.bounding_boxes[i];
    if (bb.value <= 0) continue;
    ei_printf("  %s (%f) [ x:%u, y:%u, w:%u, h:%u ]\n",
              bb.label, bb.value, bb.x, bb.y, bb.width, bb.height);
    if (bb.value > best_bb_val) {
      best_bb_val = bb.value;
      best_bb_ix  = i;
    }
  }
  if (best_bb_val > 0) {
    top_label = result.bounding_boxes[best_bb_ix].label;
  }
#else
  float   best_score = 0.f;
  uint32_t best_ix    = 0;
  for (uint32_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    ei_printf("  %s: %.5f\n",
              ei_classifier_inferencing_categories[i],
              result.classification[i].value);
    if (result.classification[i].value > best_score) {
      best_score = result.classification[i].value;
      best_ix    = i;
    }
  }
  top_label = ei_classifier_inferencing_categories[best_ix];
#endif

  free(snapshot_buf);

  // Map label → Blackjack value
  uint16_t major = 0;
  if      (top_label && strcmp(top_label, "ace")   == 0) major = 11;
  else if (top_label && strcmp(top_label, "two")   == 0) major = 2;
  else if (top_label && strcmp(top_label, "three") == 0) major = 3;
  else if (top_label && strcmp(top_label, "four")  == 0) major = 4;
  else if (top_label && strcmp(top_label, "five")  == 0) major = 5;
  else if (top_label && strcmp(top_label, "six")   == 0) major = 6;
  else if (top_label && strcmp(top_label, "seven") == 0) major = 7;
  else if (top_label && strcmp(top_label, "eight") == 0) major = 8;
  else if (top_label && strcmp(top_label, "nine")  == 0) major = 9;
  else if (top_label && (strcmp(top_label, "ten")   == 0 ||
                         strcmp(top_label, "jack")  == 0 ||
                         strcmp(top_label, "queen") == 0 ||
                         strcmp(top_label, "king")  == 0))
    major = 10;

  Serial.printf("Detected \"%s\" → %u\n", top_label ? top_label : "?", major);

  // Update beacon only when it changes
  if (major != lastMajor) {
    update_beacon_major(major);
    lastMajor = major;
  }

  // (Optional) GATT notify
  notifyValue = (uint8_t)major;
  if (deviceConnected) {
    pCharacteristic->setValue(&notifyValue, 1);
    pCharacteristic->notify();
    Serial.printf("Notified central: %u\n", notifyValue);
  }

   delay(1000);
}
