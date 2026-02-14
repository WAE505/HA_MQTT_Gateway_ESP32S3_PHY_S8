#include "ha_discovery.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace ha_discovery {

void HADiscoveryComponent::setup() {
  // Derive gateway unique ID from WiFi MAC
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char mac_hex[13];
  snprintf(mac_hex, sizeof(mac_hex), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  gateway_uid_ = mac_hex;

  // Default node_name if not set
  if (node_name_.empty()) {
    char name[32];
    snprintf(name, sizeof(name), "bthome_proxy_%02x%02x%02x", mac[3], mac[4], mac[5]);
    node_name_ = name;
  }

  // Register callback with the scanner
  scanner_->add_on_reading_callback(
      [this](ble_ext_scanner::BTHomeReading reading) { this->on_reading_(std::move(reading)); });

  ESP_LOGI(TAG, "HA Discovery initialized — gateway: %s (uid: %s)", node_name_.c_str(), gateway_uid_.c_str());
}

void HADiscoveryComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "HA MQTT Discovery:");
  ESP_LOGCONFIG(TAG, "  Gateway name: %s", node_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Gateway UID: %s", gateway_uid_.c_str());
  ESP_LOGCONFIG(TAG, "  Discovery prefix: %s", discovery_prefix_.c_str());
  ESP_LOGCONFIG(TAG, "  State prefix: %s", state_prefix_.c_str());
  ESP_LOGCONFIG(TAG, "  Config interval: %u s", config_interval_ms_ / 1000);
  ESP_LOGCONFIG(TAG, "  State interval: %u s", state_interval_ms_ / 1000);
}

std::string HADiscoveryComponent::mac_to_id_(const std::string &mac_clean) {
  // Convert to lowercase for topic/ID use
  std::string id;
  id.reserve(mac_clean.size());
  for (char c : mac_clean) {
    id += (c >= 'A' && c <= 'Z') ? (c + 32) : c;
  }
  return id;
}

void HADiscoveryComponent::on_reading_(ble_ext_scanner::BTHomeReading reading) {
  // Guard: skip if MQTT not connected
  if (!mqtt::global_mqtt_client->is_connected())
    return;

  // Publish gateway availability on first call
  if (!availability_published_) {
    publish_gateway_availability_(true);
    availability_published_ = true;
  }

  uint32_t now = millis();
  std::string mac_id = mac_to_id_(reading.mac_clean);
  auto &dev = devices_[mac_id];

  // Rate limit state publishes per device
  if (dev.last_state_publish_ms != 0 && (now - dev.last_state_publish_ms) < state_interval_ms_)
    return;

  // Refresh config if interval elapsed or never published
  if (!dev.config_published || (now - dev.last_config_publish_ms) >= config_interval_ms_) {
    publish_device_config_(mac_id, reading);
    dev.last_config_publish_ms = now;
    dev.config_published = true;
  }

  publish_device_state_(mac_id, reading);
  dev.last_state_publish_ms = now;
}

void HADiscoveryComponent::publish_device_config_(const std::string &mac,
                                                   const ble_ext_scanner::BTHomeReading &reading) {
  // Shared topics
  std::string state_topic = state_prefix_ + "/" + mac + "/state";
  std::string avail_topic = state_prefix_ + "/" + node_name_ + "/status";

  // Device block (shared across all entities for this MAC)
  // Format: "ids":["bthome_XXXX"],"name":"BTHome XXXX","via_device":"gateway_uid"
  std::string mac_upper = reading.mac_clean;
  std::string dev_block;
  {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "\"dev\":{\"ids\":[\"bthome_%s\"],\"name\":\"BTHome %s\",\"mf\":\"BTHome\","
             "\"via_device\":\"%s\"}",
             mac.c_str(), mac_upper.c_str(), node_name_.c_str());
    dev_block = buf;
  }

  // Helper lambda to publish a single entity config
  auto publish_entity = [&](const char *obj_id, const char *name, const char *dev_class,
                            const char *unit, const char *val_tpl, const char *entity_cat) {
    // Topic: {discovery_prefix}/sensor/bthome_{mac}/{obj_id}/config
    std::string topic = discovery_prefix_ + "/sensor/bthome_" + mac + "/" + obj_id + "/config";

    // Unique ID: bthome_{mac}_{obj_id}
    std::string uniq_id = "bthome_" + mac + "_" + obj_id;

    char payload[512];
    int len = snprintf(payload, sizeof(payload),
                       "{\"name\":\"%s\",\"uniq_id\":\"%s\",\"stat_t\":\"%s\","
                       "\"val_tpl\":\"%s\",\"avail_t\":\"%s\",",
                       name, uniq_id.c_str(), state_topic.c_str(), val_tpl, avail_topic.c_str());

    if (dev_class != nullptr && dev_class[0] != '\0') {
      len += snprintf(payload + len, sizeof(payload) - len, "\"dev_cla\":\"%s\",", dev_class);
    }
    if (unit != nullptr && unit[0] != '\0') {
      len += snprintf(payload + len, sizeof(payload) - len, "\"unit_of_meas\":\"%s\",", unit);
    }
    if (entity_cat != nullptr && entity_cat[0] != '\0') {
      len += snprintf(payload + len, sizeof(payload) - len, "\"ent_cat\":\"%s\",", entity_cat);
    }

    // State class for numeric sensors
    len += snprintf(payload + len, sizeof(payload) - len, "\"stat_cla\":\"measurement\",");

    // Append device block and close
    snprintf(payload + len, sizeof(payload) - len, "%s}", dev_block.c_str());

    mqtt::global_mqtt_client->publish(topic, payload, strlen(payload), 0, true);
    ESP_LOGD(TAG, "Config: %s", topic.c_str());
  };

  // Publish config for each present sensor value
  if (!std::isnan(reading.temp)) {
    publish_entity("temperature", "Temperature", "temperature",
                   "\u00b0C", "{{ value_json.temperature }}", "");
  }
  if (!std::isnan(reading.temp_avg_60min)) {
    publish_entity("temp_avg_60min", "Temp 60min avg", "temperature",
                   "\u00b0C", "{{ value_json.temp_avg_60min }}", "diagnostic");
  }
  if (!std::isnan(reading.temp_avg_4hr)) {
    publish_entity("temp_avg_4hr", "Temp 4hr avg", "temperature",
                   "\u00b0C", "{{ value_json.temp_avg_4hr }}", "diagnostic");
  }
  if (!std::isnan(reading.humidity)) {
    publish_entity("humidity", "Humidity", "humidity",
                   "%", "{{ value_json.humidity }}", "");
  }
  if (!std::isnan(reading.hum_avg_60min)) {
    publish_entity("hum_avg_60min", "Humidity 60min avg", "humidity",
                   "%", "{{ value_json.hum_avg_60min }}", "diagnostic");
  }
  if (reading.battery >= 0) {
    publish_entity("battery", "Battery", "battery",
                   "%", "{{ value_json.battery }}", "diagnostic");
  }
  if (reading.compressor_pct >= 0) {
    publish_entity("compressor_pct", "Compressor %", "",
                   "%", "{{ value_json.compressor_pct }}", "diagnostic");
  }
  // Always publish RSSI
  publish_entity("rssi", "RSSI", "signal_strength",
                 "dBm", "{{ value_json.rssi }}", "diagnostic");
}

void HADiscoveryComponent::publish_device_state_(const std::string &mac,
                                                  const ble_ext_scanner::BTHomeReading &reading) {
  std::string topic = state_prefix_ + "/" + mac + "/state";

  // Build JSON with stack-allocated buffer — no heap allocation
  char buf[384];
  int len = snprintf(buf, sizeof(buf), "{\"rssi\":%d", reading.rssi);

  if (!std::isnan(reading.temp)) {
    len += snprintf(buf + len, sizeof(buf) - len, ",\"temperature\":%.2f", reading.temp);
  }
  if (!std::isnan(reading.temp_avg_60min)) {
    len += snprintf(buf + len, sizeof(buf) - len, ",\"temp_avg_60min\":%.2f", reading.temp_avg_60min);
  }
  if (!std::isnan(reading.temp_avg_4hr)) {
    len += snprintf(buf + len, sizeof(buf) - len, ",\"temp_avg_4hr\":%.2f", reading.temp_avg_4hr);
  }
  if (!std::isnan(reading.humidity)) {
    len += snprintf(buf + len, sizeof(buf) - len, ",\"humidity\":%.1f", reading.humidity);
  }
  if (!std::isnan(reading.hum_avg_60min)) {
    len += snprintf(buf + len, sizeof(buf) - len, ",\"hum_avg_60min\":%.1f", reading.hum_avg_60min);
  }
  if (reading.battery >= 0) {
    len += snprintf(buf + len, sizeof(buf) - len, ",\"battery\":%d", reading.battery);
  }
  if (reading.compressor_pct >= 0) {
    len += snprintf(buf + len, sizeof(buf) - len, ",\"compressor_pct\":%d", reading.compressor_pct);
  }

  snprintf(buf + len, sizeof(buf) - len, "}");

  mqtt::global_mqtt_client->publish(topic, buf, strlen(buf), 0, false);
  ESP_LOGV(TAG, "State: %s → %s", topic.c_str(), buf);
}

void HADiscoveryComponent::publish_gateway_availability_(bool online) {
  std::string topic = state_prefix_ + "/" + node_name_ + "/status";
  const char *payload = online ? "online" : "offline";

  mqtt::global_mqtt_client->publish(topic, payload, strlen(payload), 0, true);
  ESP_LOGI(TAG, "Gateway availability: %s → %s", topic.c_str(), payload);

  // Publish gateway device config so it appears in HA
  std::string gw_config_topic = discovery_prefix_ + "/sensor/" + node_name_ + "/status/config";
  char buf[384];
  snprintf(buf, sizeof(buf),
           "{\"name\":\"Status\",\"uniq_id\":\"%s_status\","
           "\"stat_t\":\"%s\",\"val_tpl\":\"{{ value }}\","
           "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
           "\"mf\":\"BTHome Proxy\",\"mdl\":\"ESP32-S3\"}}",
           node_name_.c_str(), topic.c_str(),
           node_name_.c_str(), node_name_.c_str());

  mqtt::global_mqtt_client->publish(gw_config_topic, buf, strlen(buf), 0, true);
}

}  // namespace ha_discovery
}  // namespace esphome
