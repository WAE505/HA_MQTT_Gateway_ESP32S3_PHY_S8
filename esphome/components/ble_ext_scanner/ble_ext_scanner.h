#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>

#include <atomic>
#include <cmath>
#include <cstring>

namespace esphome {
namespace ble_ext_scanner {

static const char *const TAG = "ble_ext_scanner";

struct BTHomeReading {
  std::string mac_clean;
  float temp{NAN};
  float temp_avg_60min{NAN};
  float temp_avg_4hr{NAN};
  float humidity{NAN};
  float hum_avg_60min{NAN};
  int battery{-1};
  int compressor_pct{-1};
  int assignment_mode{-1};
  int rssi{0};
  uint8_t primary_phy{1};  // 1 = 1M, 3 = Coded
};

// Lock-free SPSC ring buffer for passing raw reports from the Bluedroid
// callback task to ESPHome's loop() task.
struct RawAdvReport {
  uint8_t addr[6];
  int8_t rssi;
  uint8_t primary_phy;
  uint8_t data[62];
  uint8_t data_len;
};

static const size_t RING_BUF_SIZE = 64;

class BLEExtScannerComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void set_scan_interval(uint32_t ms) { scan_interval_ms_ = ms; }
  void set_scan_window(uint32_t ms) { scan_window_ms_ = ms; }
  void set_scan_coded(bool enabled) { scan_coded_ = enabled; }
  void set_scan_1m(bool enabled) { scan_1m_ = enabled; }

  void start_scan();
  void stop_scan();
  bool is_scanning() const { return scanning_.load(); }

  void add_on_reading_callback(std::function<void(BTHomeReading)> &&callback) {
    on_reading_callbacks_.add(std::move(callback));
  }

 protected:
  static void gap_event_handler_(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void configure_ext_scan_();
  bool parse_bthome_v2_(const uint8_t *data, uint8_t len, BTHomeReading &reading);

  uint32_t scan_interval_ms_{1100};
  uint32_t scan_window_ms_{1100};
  bool scan_coded_{true};
  bool scan_1m_{true};
  std::atomic<bool> scanning_{false};
  bool bt_initialized_{false};
  bool ext_scan_supported_{true};

  CallbackManager<void(BTHomeReading)> on_reading_callbacks_;

  // SPSC ring buffer — written by GAP callback, read by loop()
  static RawAdvReport ring_buf_[RING_BUF_SIZE];
  static std::atomic<size_t> ring_head_;  // written by producer (GAP callback)
  static std::atomic<size_t> ring_tail_;  // written by consumer (loop)
  static BLEExtScannerComponent *instance_;
};

// Automation trigger
class BLEExtScannerTrigger : public Trigger<BTHomeReading> {
 public:
  explicit BLEExtScannerTrigger(BLEExtScannerComponent *parent) {
    parent->add_on_reading_callback([this](BTHomeReading reading) { this->trigger(std::move(reading)); });
  }
};

}  // namespace ble_ext_scanner
}  // namespace esphome
