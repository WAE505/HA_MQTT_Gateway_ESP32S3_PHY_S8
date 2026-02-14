#include "ble_ext_scanner.h"

#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>

namespace esphome {
namespace ble_ext_scanner {

// Static member definitions
RawAdvReport BLEExtScannerComponent::ring_buf_[RING_BUF_SIZE];
std::atomic<size_t> BLEExtScannerComponent::ring_head_{0};
std::atomic<size_t> BLEExtScannerComponent::ring_tail_{0};
BLEExtScannerComponent *BLEExtScannerComponent::instance_{nullptr};

void BLEExtScannerComponent::setup() {
  instance_ = this;

  ESP_LOGI(TAG, "Initializing BLE Extended Scanner...");

  // --- Bluedroid init sequence ---
  // 1. Release classic BT memory (ESP32-S3 is BLE-only)
  esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to release classic BT memory: %s", esp_err_to_name(err));
  }

  // 2. Init BT controller with default config
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  err = esp_bt_controller_init(&bt_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  // 3. Enable BT controller in BLE mode
  err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  // 4. Init and enable Bluedroid
  err = esp_bluedroid_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  err = esp_bluedroid_enable();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  // 5. Register our GAP callback
  err = esp_ble_gap_register_callback(gap_event_handler_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  bt_initialized_ = true;

  // 6. Configure and start extended scanning
  configure_ext_scan_();

  ESP_LOGI(TAG, "BLE Extended Scanner initialized (1M=%s, Coded=%s)",
           scan_1m_ ? "yes" : "no", scan_coded_ ? "yes" : "no");
}

void BLEExtScannerComponent::configure_ext_scan_() {
#ifdef CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  // Convert ms to 0.625ms BLE units
  uint16_t interval_units = (uint16_t)(scan_interval_ms_ / 0.625f);
  uint16_t window_units = (uint16_t)(scan_window_ms_ / 0.625f);

  // Build config mask for selected PHYs
  uint8_t cfg_mask = 0;

  esp_ble_ext_scan_cfg_t uncoded_cfg = {};
  esp_ble_ext_scan_cfg_t coded_cfg = {};

  if (scan_1m_) {
    cfg_mask |= ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK;
    uncoded_cfg.scan_type = BLE_SCAN_TYPE_PASSIVE;
    uncoded_cfg.scan_interval = interval_units;
    uncoded_cfg.scan_window = window_units;
  }

  if (scan_coded_) {
    cfg_mask |= ESP_BLE_GAP_EXT_SCAN_CFG_CODE_MASK;
    coded_cfg.scan_type = BLE_SCAN_TYPE_PASSIVE;
    coded_cfg.scan_interval = interval_units;
    coded_cfg.scan_window = window_units;
  }

  esp_ble_ext_scan_params_t ext_scan_params = {};
  ext_scan_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  ext_scan_params.filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
  ext_scan_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
  ext_scan_params.cfg_mask = cfg_mask;
  ext_scan_params.uncoded_cfg = uncoded_cfg;
  ext_scan_params.coded_cfg = coded_cfg;

  esp_err_t err = esp_ble_gap_set_ext_scan_params(&ext_scan_params);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set ext scan params: %s (0x%x)", esp_err_to_name(err), err);
    ESP_LOGE(TAG, "Extended scanning will not be available");
    ext_scan_supported_ = false;
  }
#else
  ESP_LOGW(TAG, "BLE 5.0 features not enabled in sdkconfig — using legacy scanning (1M only)");
  ext_scan_supported_ = false;
  uint16_t interval_units = (uint16_t)(scan_interval_ms_ / 0.625f);
  uint16_t window_units = (uint16_t)(scan_window_ms_ / 0.625f);
  esp_ble_scan_params_t legacy_params = {};
  legacy_params.scan_type = BLE_SCAN_TYPE_PASSIVE;
  legacy_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  legacy_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
  legacy_params.scan_interval = interval_units;
  legacy_params.scan_window = window_units;
  legacy_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
  esp_ble_gap_set_scan_params(&legacy_params);
#endif
}

void BLEExtScannerComponent::start_scan() {
  if (!bt_initialized_) return;

#ifdef CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  if (!ext_scan_supported_) return;
  // duration=0 (continuous), period=0 (no periodic)
  esp_err_t err = esp_ble_gap_start_ext_scan(0, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start ext scan: %s", esp_err_to_name(err));
  }
#else
  esp_err_t err = esp_ble_gap_start_scanning(0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start legacy scan: %s", esp_err_to_name(err));
  }
#endif
}

void BLEExtScannerComponent::stop_scan() {
  if (!bt_initialized_) return;

#ifdef CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  esp_ble_gap_stop_ext_scan();
#else
  esp_ble_gap_stop_scanning();
#endif
  scanning_.store(false);
  ESP_LOGI(TAG, "Scanning stopped");
}

void BLEExtScannerComponent::gap_event_handler_(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (instance_ == nullptr) return;

  switch (event) {
#ifdef CONFIG_BT_BLE_50_FEATURES_SUPPORTED
    case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT: {
      if (param->set_ext_scan_params.status == ESP_BT_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Extended scan params configured — call start_scan() to begin");
      } else {
        ESP_LOGE(TAG, "Extended scan params failed: status=%d", param->set_ext_scan_params.status);
      }
      break;
    }

    case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT: {
      if (param->ext_scan_start.status == ESP_BT_STATUS_SUCCESS) {
        instance_->scanning_.store(true);
        ESP_LOGI(TAG, "Extended scanning started");
      } else {
        ESP_LOGE(TAG, "Extended scan start failed: status=%d", param->ext_scan_start.status);
      }
      break;
    }

    case ESP_GAP_BLE_EXT_ADV_REPORT_EVT: {
      auto &rep = param->ext_adv_report.params;
      if (rep.adv_data_len == 0 || rep.adv_data_len > sizeof(RawAdvReport::data))
        break;

      // Enqueue to ring buffer (drop if full — better than blocking)
      size_t head = ring_head_.load(std::memory_order_relaxed);
      size_t next = (head + 1) % RING_BUF_SIZE;
      if (next == ring_tail_.load(std::memory_order_acquire)) {
        // Buffer full, drop
        break;
      }

      auto &slot = ring_buf_[head];
      memcpy(slot.addr, rep.addr, 6);
      slot.rssi = rep.rssi;
      slot.primary_phy = rep.primary_phy;
      slot.data_len = rep.adv_data_len;
      memcpy(slot.data, rep.adv_data, rep.adv_data_len);

      ring_head_.store(next, std::memory_order_release);
      break;
    }

    case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT: {
      instance_->scanning_.store(false);
      ESP_LOGI(TAG, "Extended scanning stopped (event)");
      break;
    }

#else
    // Legacy scan events (non-BLE 5.0 builds only)
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
      if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Legacy scan params set — starting scan");
        instance_->start_scan();
      }
      break;
    }

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT: {
      if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
        instance_->scanning_.store(true);
        ESP_LOGI(TAG, "Legacy scanning started");
      }
      break;
    }

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
      if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT)
        break;

      auto &rst = param->scan_rst;
      if (rst.adv_data_len == 0 || rst.adv_data_len > sizeof(RawAdvReport::data))
        break;

      size_t head = ring_head_.load(std::memory_order_relaxed);
      size_t next = (head + 1) % RING_BUF_SIZE;
      if (next == ring_tail_.load(std::memory_order_acquire))
        break;

      auto &slot = ring_buf_[head];
      memcpy(slot.addr, rst.bda, 6);
      slot.rssi = rst.rssi;
      slot.primary_phy = 1;  // Legacy = 1M
      slot.data_len = rst.adv_data_len;
      memcpy(slot.data, rst.ble_adv, rst.adv_data_len);

      ring_head_.store(next, std::memory_order_release);
      break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT: {
      instance_->scanning_.store(false);
      break;
    }
#endif

    default:
      break;
  }
}

void BLEExtScannerComponent::loop() {
  // Dequeue up to 10 reports per tick to avoid starving other components
  for (int i = 0; i < 10; i++) {
    size_t tail = ring_tail_.load(std::memory_order_relaxed);
    if (tail == ring_head_.load(std::memory_order_acquire))
      break;  // Empty

    auto &slot = ring_buf_[tail];

    // Format MAC address (no colons)
    char mac_buf[13];
    snprintf(mac_buf, sizeof(mac_buf), "%02X%02X%02X%02X%02X%02X",
             slot.addr[0], slot.addr[1], slot.addr[2],
             slot.addr[3], slot.addr[4], slot.addr[5]);

    BTHomeReading reading;
    reading.mac_clean = mac_buf;
    reading.rssi = slot.rssi;
    reading.primary_phy = slot.primary_phy;

    bool valid = parse_bthome_v2_(slot.data, slot.data_len, reading);

    ring_tail_.store((tail + 1) % RING_BUF_SIZE, std::memory_order_release);

    if (valid) {
      on_reading_callbacks_.call(reading);
    }
  }
}

bool BLEExtScannerComponent::parse_bthome_v2_(const uint8_t *data, uint8_t len, BTHomeReading &reading) {
  // Walk BLE AD structures to find BTHome v2 service data (UUID 0xFCD2)
  size_t pos = 0;
  while (pos + 1 < len) {
    uint8_t ad_len = data[pos];
    if (ad_len == 0 || pos + 1 + ad_len > len)
      break;

    uint8_t ad_type = data[pos + 1];
    const uint8_t *ad_data = &data[pos + 2];
    uint8_t ad_data_len = ad_len - 1;

    // Service Data - 16-bit UUID (type 0x16)
    if (ad_type == 0x16 && ad_data_len >= 3) {
      uint16_t uuid = ad_data[0] | (ad_data[1] << 8);
      if (uuid == 0xFCD2) {
        // Found BTHome v2 service data
        // Byte after UUID pair is BTHome header
        size_t obj_pos = 3;  // Skip UUID (2 bytes) + header (1 byte)

        int temp_count = 0;
        int hum8_count = 0;

        while (obj_pos < ad_data_len) {
          uint8_t obj_id = ad_data[obj_pos++];

          switch (obj_id) {
            case 0x01:  // Battery percentage (1 byte)
              if (obj_pos < ad_data_len) {
                reading.battery = ad_data[obj_pos++];
              }
              break;

            case 0x02:  // Temperature (2 bytes, signed, 0.01 precision)
              if (obj_pos + 1 < ad_data_len) {
                int16_t raw = ad_data[obj_pos] | (ad_data[obj_pos + 1] << 8);
                float val = raw * 0.01f;
                if (temp_count == 0) {
                  reading.temp = val;
                } else if (temp_count == 1) {
                  reading.temp_avg_60min = val;
                } else {
                  reading.temp_avg_4hr = val;
                }
                temp_count++;
                obj_pos += 2;
              }
              break;

            case 0x03:  // Humidity uint16 (2 bytes, 0.01 precision)
              if (obj_pos + 1 < ad_data_len) {
                int16_t raw = ad_data[obj_pos] | (ad_data[obj_pos + 1] << 8);
                reading.humidity = raw * 0.01f;
                obj_pos += 2;
              }
              break;

            case 0x2E:  // Humidity uint8 (1 byte, 1% precision)
              if (obj_pos < ad_data_len) {
                if (hum8_count == 0) {
                  if (std::isnan(reading.humidity)) {
                    reading.humidity = ad_data[obj_pos];
                  }
                } else {
                  reading.hum_avg_60min = ad_data[obj_pos];
                }
                hum8_count++;
                obj_pos++;
              }
              break;

            case 0x2F:  // Compressor run % - moisture8 (1 byte)
              if (obj_pos < ad_data_len) {
                reading.compressor_pct = ad_data[obj_pos++];
              }
              break;

            case 0x0C:  // Voltage (2 bytes) - skip
              obj_pos += 2;
              break;

            case 0x0F:  // Generic boolean (1 byte) - assignment mode
              if (obj_pos < ad_data_len) {
                reading.assignment_mode = ad_data[obj_pos++];
              }
              break;

            default:
              // Unknown object ID — skip 1 byte (best effort)
              obj_pos++;
              break;
          }
        }

        return true;  // Found and parsed BTHome data
      }
    }

    pos += 1 + ad_len;
  }

  return false;  // No BTHome service data found
}

void BLEExtScannerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "BLE Extended Scanner:");
  ESP_LOGCONFIG(TAG, "  Scan interval: %u ms", scan_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Scan window: %u ms", scan_window_ms_);
  ESP_LOGCONFIG(TAG, "  1M PHY: %s", scan_1m_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Coded PHY: %s", scan_coded_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Extended scan API: %s", ext_scan_supported_ ? "yes" : "no (legacy fallback)");
}

}  // namespace ble_ext_scanner
}  // namespace esphome
