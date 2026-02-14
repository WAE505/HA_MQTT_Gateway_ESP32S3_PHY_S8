#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/components/ble_ext_scanner/ble_ext_scanner.h"

#include <esp_mac.h>

#include <map>
#include <string>

namespace esphome {
namespace ha_discovery {

static const char *const TAG = "ha_discovery";

struct DiscoveredDevice {
  uint32_t last_config_publish_ms{0};
  uint32_t last_state_publish_ms{0};
  bool config_published{false};
};

class HADiscoveryComponent : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_scanner(ble_ext_scanner::BLEExtScannerComponent *scanner) { scanner_ = scanner; }
  void set_discovery_prefix(const std::string &prefix) { discovery_prefix_ = prefix; }
  void set_state_prefix(const std::string &prefix) { state_prefix_ = prefix; }
  void set_node_name(const std::string &name) { node_name_ = name; }
  void set_config_interval(uint32_t seconds) { config_interval_ms_ = seconds * 1000; }
  void set_state_interval(uint32_t seconds) { state_interval_ms_ = seconds * 1000; }

 protected:
  void on_reading_(ble_ext_scanner::BTHomeReading reading);
  void publish_device_config_(const std::string &mac, const ble_ext_scanner::BTHomeReading &reading);
  void publish_device_state_(const std::string &mac, const ble_ext_scanner::BTHomeReading &reading);
  void publish_gateway_availability_(bool online);

  // Format MAC for topic use: lowercase with no separators (e.g. "aabbccddeeff")
  std::string mac_to_id_(const std::string &mac_clean);

  ble_ext_scanner::BLEExtScannerComponent *scanner_{nullptr};
  std::map<std::string, DiscoveredDevice> devices_;
  std::string gateway_uid_;
  std::string node_name_;
  std::string discovery_prefix_{"homeassistant"};
  std::string state_prefix_{"bthome"};
  uint32_t config_interval_ms_{300000};
  uint32_t state_interval_ms_{10000};
  bool availability_published_{false};
};

}  // namespace ha_discovery
}  // namespace esphome
