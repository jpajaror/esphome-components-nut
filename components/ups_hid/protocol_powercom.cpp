#include "protocol_powercom.h"
#include "ups_hid.h"
#include "constants_hid.h"
#include "constants_ups.h"
#include "protocol_factory.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cmath>

namespace esphome {
namespace ups_hid {

static const char *const PWR_TAG = "ups_hid.powercom";

bool PowercomHidProtocol::detect() {
    ESP_LOGD(PWR_TAG, "Detecting PowerCOM HID Protocol...");
    
    // Check device connection status first
    if (!parent_->is_connected()) {
        ESP_LOGD(PWR_TAG, "Device not connected, skipping protocol detection");
        return false;
    }
    
    // Check vendor ID - PowerCOM uses 0x0D9F
    uint16_t vid = parent_->get_vendor_id();
    if (vid != usb::VENDOR_ID_POWERCOM) {
        ESP_LOGD(PWR_TAG, "Vendor ID 0x%04X is not PowerCOM (expected 0x0D9F)", vid);
        return false;
    }
    
    ESP_LOGI(PWR_TAG, "Found PowerCOM device with VID: 0x%04X", vid);
    
    // Try to read a known PowerCOM report to confirm
    uint8_t buffer[limits::MAX_HID_REPORT_SIZE];
    size_t buffer_len;
    
    // Try Report 0x14 (Present Status) which is specific to PowerCOM
    buffer_len = sizeof(buffer);
    esp_err_t ret = parent_->hid_get_report(HID_REPORT_TYPE_FEATURE, REPORT_ID_PRESENT_STATUS, 
                                           buffer, &buffer_len, parent_->get_protocol_timeout());
    
    if (ret == ESP_OK && buffer_len >= 2) {
        ESP_LOGI(PWR_TAG, "PowerCOM protocol detected (Present Status report found)");
        return true;
    }
    
    // Try Report 0x0A (Remaining Capacity) as alternative
    buffer_len = sizeof(buffer);
    ret = parent_->hid_get_report(HID_REPORT_TYPE_FEATURE, 0x0A, 
                                 buffer, &buffer_len, parent_->get_protocol_timeout());
    
    if (ret == ESP_OK && buffer_len >= 1) {
        ESP_LOGI(PWR_TAG, "PowerCOM protocol detected (Remaining Capacity report found)");
        return true;
    }
    
    ESP_LOGD(PWR_TAG, "No PowerCOM-specific reports found");
    return false;
}

bool PowercomHidProtocol::initialize() {
    ESP_LOGD(PWR_TAG, "Initializing PowerCOM HID Protocol...");
    
    // Read manufacturer information
    std::string manufacturer, model, serial;
    
    if (read_string_descriptor(STRING_ID_MANUFACTURER, manufacturer)) {
        ESP_LOGI(PWR_TAG, "Manufacturer: %s", manufacturer.c_str());
    }
    
    if (read_string_descriptor(STRING_ID_PRODUCT, model)) {
        ESP_LOGI(PWR_TAG, "Model: %s", model.c_str());
    }
    
    if (read_string_descriptor(STRING_ID_SERIAL, serial)) {
        ESP_LOGI(PWR_TAG, "Serial: %s", serial.c_str());
    }
    
    ESP_LOGI(PWR_TAG, "PowerCOM HID protocol initialized successfully");
    return true;
}

bool PowercomHidProtocol::read_data(UpsData &data) {
    ESP_LOGV(PWR_TAG, "Reading PowerCOM UPS data...");
    
    // Reset data structure
    data.reset();
    
    // Set protocol type
    data.device.detected_protocol = DeviceInfo::PROTOCOL_POWERCOM_HID;
    
    // Parse different data sections
    parse_power_summary(data);
    parse_battery_info(data);
    parse_present_status(data);
    parse_input_output_info(data);
    parse_delays(data);
    parse_manufacturer_info(data);
    
    // Set default test result if not available
    if (data.test.ups_test_result.empty()) {
        data.test.ups_test_result = test::RESULT_NO_TEST;
    }
    
    // Set beeper status if available
    uint8_t beeper_status = read_8bit_value(REPORT_ID_BEEPER_CONTROL);
    switch (beeper_status) {
        case BEEPER_ENABLE:
            data.config.beeper_status = "enabled";
            break;
        case BEEPER_DISABLE:
            data.config.beeper_status = "disabled";
            break;
        case BEEPER_MUTE:
            data.config.beeper_status = "muted";
            break;
        default:
            data.config.beeper_status = "unknown";
            break;
    }
    
    // Determine overall UPS status
    bool ac_present = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::AC_PRESENT);
    bool overload = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::OVERLOAD);
    bool battery_low = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::BELOW_CAPACITY_LIMIT);
    
    if (ac_present) {
        if (overload) {
            data.power.status = "OL OVERLOAD";
        } else {
            data.power.status = "OL";
        }
    } else {
        if (battery_low) {
            data.power.status = "OB LB";
        } else {
            data.power.status = "OB";
        }
    }
    
    // Log summary
    ESP_LOGD(PWR_TAG, "PowerCOM data read: %s, Battery: %.0f%%, Input: %.1fV, Output: %.1fV",
             data.power.status.c_str(), data.battery.level,
             data.power.input_voltage, data.power.output_voltage);
    
    return true;
}

bool PowercomHidProtocol::read_feature_report(uint8_t report_id, uint8_t* buffer, size_t& buffer_len) {
    if (!parent_->is_connected()) {
        return false;
    }
    
    buffer_len = limits::MAX_HID_REPORT_SIZE;
    esp_err_t ret = parent_->hid_get_report(HID_REPORT_TYPE_FEATURE, report_id, 
                                           buffer, &buffer_len, parent_->get_protocol_timeout());
    
    return (ret == ESP_OK && buffer_len > 0);
}

bool PowercomHidProtocol::read_input_report(uint8_t report_id, uint8_t* buffer, size_t& buffer_len) {
    if (!parent_->is_connected()) {
        return false;
    }

    buffer_len = limits::MAX_HID_REPORT_SIZE;
    esp_err_t ret = parent_->hid_get_report(HID_REPORT_TYPE_INPUT, report_id, 
                                           buffer, &buffer_len, parent_->get_protocol_timeout());

    return (ret == ESP_OK && buffer_len > 0);
}

bool PowercomHidProtocol::read_string_descriptor(uint8_t index, std::string& result) {
    return (parent_->usb_get_string_descriptor(index, result) == ESP_OK);
}

uint16_t PowercomHidProtocol::read_16bit_value(uint8_t report_id, uint8_t offset) {
    uint8_t buffer[limits::MAX_HID_REPORT_SIZE];
    size_t buffer_len;

    if (read_feature_report(report_id, buffer, buffer_len)) {
        // Format: [report_id, data_low, data_high]
        if (buffer_len >= offset + 3) {
            // skip report_id (first byte)
            uint8_t data_low = buffer[offset + 1];
            uint8_t data_high = buffer[offset + 2];
            return (data_high << 8) | data_low;  // little-endian
        }
    }

    return 0;
}

uint8_t PowercomHidProtocol::read_8bit_value(uint8_t report_id, uint8_t offset) {
    uint8_t buffer[limits::MAX_HID_REPORT_SIZE];
    size_t buffer_len;

    if (read_feature_report(report_id, buffer, buffer_len)) {
        // skip report_id (first byte)
        if (buffer_len > offset + 1) {
            return buffer[offset + 1];
        }
    }

    return 0;
}

bool PowercomHidProtocol::read_bit_status(uint8_t report_id, uint8_t bit_offset) {
    uint8_t buffer[limits::MAX_HID_REPORT_SIZE];
    size_t buffer_len;

    if (read_feature_report(report_id, buffer, buffer_len) && buffer_len >= 3) {
        // skip report_id (first byte)
        uint8_t status_byte = buffer[1];
        uint8_t bit_mask = 1 << bit_offset;
        
        return (status_byte & bit_mask) != 0;
    }

    return false;
}

void PowercomHidProtocol::parse_power_summary(UpsData &data) {
    // Read battery remaining capacity (Report 0x0A)
    uint8_t battery_capacity = read_8bit_value(0x0A);
    if (battery_capacity <= 100) {
        data.battery.level = static_cast<float>(battery_capacity);
        ESP_LOGD(PWR_TAG, "Battery capacity: %d%%", battery_capacity);
    }
    
    // Read battery runtime (Report 0x0E)
    uint16_t runtime_seconds = read_16bit_value(0x0E);
    if (runtime_seconds > 0 && runtime_seconds < 65535) {
        data.battery.runtime_minutes = static_cast<float>(runtime_seconds) / 60.0f;
        ESP_LOGD(PWR_TAG, "Battery runtime: %d seconds (%.1f minutes)", 
                 runtime_seconds, data.battery.runtime_minutes);
    }
    
    // Read warning and low capacity limits
    uint8_t warning_limit = read_8bit_value(0x0B);
    uint8_t low_limit = read_8bit_value(0x0C);
    
    data.battery.charge_warning = static_cast<float>(warning_limit);
    data.battery.charge_low = static_cast<float>(low_limit);
    
    ESP_LOGD(PWR_TAG, "Capacity limits - Warning: %d%%, Low: %d%%", 
             warning_limit, low_limit);
}

void PowercomHidProtocol::parse_battery_info(UpsData &data) {
    // Read battery voltage (Report 0x1A)
    uint16_t battery_voltage_raw = read_16bit_value(0x1A);
    if (battery_voltage_raw > 0) {
        data.battery.voltage = static_cast<float>(battery_voltage_raw);
        ESP_LOGD(PWR_TAG, "Battery voltage: %d V", battery_voltage_raw);
    }
    
    // Read nominal battery voltage (Report 0x19)
    uint16_t nominal_voltage_raw = read_16bit_value(0x19);
    if (nominal_voltage_raw > 0) {
        data.battery.voltage_nominal = static_cast<float>(nominal_voltage_raw);
        ESP_LOGD(PWR_TAG, "Nominal battery voltage: %d V", nominal_voltage_raw);
    }
    
    // Read battery chemistry (string descriptor)
    std::string chemistry;
    if (read_string_descriptor(STRING_ID_DEVICE_CHEMISTRY, chemistry)) {
        // Map to standard chemistry names
        if (chemistry == "Pb") {
            data.battery.type = battery_chemistry::LEAD_ACID;
        } else if (!chemistry.empty()) {
            data.battery.type = chemistry;
        }
        ESP_LOGD(PWR_TAG, "Battery chemistry: %s", data.battery.type.c_str());
    }
    
    // Read battery manufacturer date (Report 0x17)
    uint16_t battery_date_raw = read_16bit_value(0x17);
    if (battery_date_raw > 0) {
        // Convert from days since 1980 (MS-DOS date format)
        // This is a simplified conversion
        int days = battery_date_raw;
        int year = 1980 + (days / 365);
        int month = ((days % 365) / 30) + 1;
        int day = (days % 365) % 30 + 1;
        
        char date_str[32];
        snprintf(date_str, sizeof(date_str), "%04d/%02d/%02d", year, month, day);
        data.battery.mfr_date = date_str;
        
        ESP_LOGD(PWR_TAG, "Battery manufacture date: %s (raw: %d)", 
                 date_str, battery_date_raw);
    }
}

void PowercomHidProtocol::parse_present_status(UpsData &data) {
    // Parse charging status
    bool charging = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::CHARGING);
    bool discharging = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::DISCHARGING);
    
    if (charging) {
        data.battery.status = battery_status::CHARGING;
    } else if (discharging) {
        data.battery.status = battery_status::DISCHARGING;
    } else {
        if (data.battery.level >= 100.0f) {
            data.battery.status = battery_status::FULLY_CHARGED;
        } else {
            data.battery.status = battery_status::NOT_CHARGING;
        }
    }
    
    // Add warning suffixes if needed
    bool need_replacement = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::NEED_REPLACEMENT);
    bool below_limit = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::BELOW_CAPACITY_LIMIT);
    bool time_expired = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::TIME_LIMIT_EXPIRED);
    
    if (need_replacement) {
        data.battery.status += battery_status::REPLACE_BATTERY_SUFFIX;
    }
    if (below_limit) {
        data.battery.status += " - " + std::string(battery_status::LOW);
    }
    if (time_expired) {
        data.battery.status += battery_status::TIME_LIMIT_EXPIRED_SUFFIX;
    }
    
    ESP_LOGD(PWR_TAG, "Battery status: %s", data.battery.status.c_str());
}

void PowercomHidProtocol::parse_input_output_info(UpsData &data) {
    // Input voltage (Report 0x1D)
    uint16_t input_voltage_raw = read_16bit_value(0x1D);
    if (input_voltage_raw > 0) {
        data.power.input_voltage = static_cast<float>(input_voltage_raw);
        ESP_LOGD(PWR_TAG, "Input voltage: %d V", input_voltage_raw);
    }
    
    // Input nominal voltage (Report 0x1C)
    uint16_t input_nominal_raw = read_16bit_value(0x1C);
    if (input_nominal_raw > 0) {
        data.power.input_voltage_nominal = static_cast<float>(input_nominal_raw);
        ESP_LOGD(PWR_TAG, "Input nominal voltage: %d V", input_nominal_raw);
    }
    
    // Input frequency (Report 0x1E)
    uint8_t input_freq = read_8bit_value(0x1E);
    if (input_freq >= 47 && input_freq <= 65) {
        data.power.frequency = static_cast<float>(input_freq);
        ESP_LOGD(PWR_TAG, "Input frequency: %d Hz", input_freq);
    }
    
    // Output voltage (Report 0x21)
    uint16_t output_voltage_raw = read_16bit_value(0x21);
    if (output_voltage_raw > 0) {
        data.power.output_voltage = static_cast<float>(output_voltage_raw);
        ESP_LOGD(PWR_TAG, "Output voltage: %d V", output_voltage_raw);
    }
    
    // Output nominal voltage (Report 0x20)
    uint16_t output_nominal_raw = read_16bit_value(0x20);
    if (output_nominal_raw > 0) {
        data.power.output_voltage_nominal = static_cast<float>(output_nominal_raw);
        ESP_LOGD(PWR_TAG, "Output nominal voltage: %d V", output_nominal_raw);
    }
    
    // Output frequency (Report 0x22)
    uint8_t output_freq = read_8bit_value(0x22);
    if (output_freq >= 47 && output_freq <= 65) {
        // Use output frequency if input not available
        if (std::isnan(data.power.frequency)) {
            data.power.frequency = static_cast<float>(output_freq);
        }
        ESP_LOGD(PWR_TAG, "Output frequency: %d Hz", output_freq);
    }
    
    // Load percentage (Report 0x1F)
    uint8_t load_percent = read_8bit_value(0x1F);
    if (load_percent <= 100) {
        data.power.load_percent = static_cast<float>(load_percent);
        ESP_LOGD(PWR_TAG, "Load percentage: %d%%", load_percent);
    }
}

void PowercomHidProtocol::parse_delays(UpsData &data) {
    // Shutdown delay (Report 0x0F and 0x23)
    uint16_t shutdown_delay_raw = read_16bit_value(REPORT_ID_SHUTDOWN_DELAY);
    if (shutdown_delay_raw > 0 && shutdown_delay_raw < 65535) {
        // Convert from seconds? Check NUT output: ups.delay.shutdown: 20
        // Raw value 256 might be in 0.1s units or similar
        // Based on NUT showing 20 seconds for raw 256, it's likely 256 * 0.078125 = 20
        data.config.delay_shutdown = static_cast<int>(shutdown_delay_raw * 0.078125f);
        ESP_LOGD(PWR_TAG, "Shutdown delay: %d seconds (raw: %d)", 
                 data.config.delay_shutdown, shutdown_delay_raw);
    }
    
    // Startup delay (Report 0x10 and 0x24)
    uint16_t startup_delay_raw = read_16bit_value(REPORT_ID_START_DELAY);
    if (startup_delay_raw > 0 && startup_delay_raw < 65535) {
        // Similar conversion
        data.config.delay_start = static_cast<int>(startup_delay_raw * 0.078125f);
        ESP_LOGD(PWR_TAG, "Startup delay: %d seconds (raw: %d)", 
                 data.config.delay_start, startup_delay_raw);
    }
    
    // Output shutdown delay (Report 0x23)
    uint16_t output_shutdown_raw = read_16bit_value(REPORT_ID_OUTPUT_SHUTDOWN_DELAY);
    if (output_shutdown_raw > 0 && output_shutdown_raw < 65535) {
        // Use if main shutdown delay not available
        if (data.config.delay_shutdown <= 0) {
            data.config.delay_shutdown = static_cast<int>(output_shutdown_raw * 0.078125f);
        }
    }
    
    // Output startup delay (Report 0x24)
    uint16_t output_startup_raw = read_16bit_value(REPORT_ID_OUTPUT_START_DELAY);
    if (output_startup_raw > 0 && output_startup_raw < 65535) {
        // Use if main startup delay not available
        if (data.config.delay_start <= 0) {
            data.config.delay_start = static_cast<int>(output_startup_raw * 0.078125f);
        }
    }
}

void PowercomHidProtocol::parse_manufacturer_info(UpsData &data) {
    // Read manufacturer string
    std::string manufacturer;
    if (read_string_descriptor(STRING_ID_MANUFACTURER, manufacturer)) {
        data.device.manufacturer = manufacturer;
    }
    
    // Read model string
    std::string model;
    if (read_string_descriptor(STRING_ID_PRODUCT, model)) {
        data.device.model = model;
    }
    
    // Read serial number
    std::string serial;
    if (read_string_descriptor(STRING_ID_SERIAL, serial)) {
        data.device.serial_number = serial;
    }
    
    // Read UPS manufacture date (Report 0x0D)
    uint16_t ups_date_raw = read_16bit_value(0x0D);
    if (ups_date_raw > 0) {
        // Convert from days since 1980 (MS-DOS date format)
        int days = ups_date_raw;
        int year = 1980 + (days / 365);
        int month = ((days % 365) / 30) + 1;
        int day = (days % 365) % 30 + 1;
        
        char date_str[32];
        snprintf(date_str, sizeof(date_str), "%04d/%02d/%02d", year, month, day);
        data.device.mfr_date = date_str;
        
        ESP_LOGD(PWR_TAG, "UPS manufacture date: %s (raw: %d)", 
                 date_str, ups_date_raw);
    }
    
    ESP_LOGD(PWR_TAG, "Manufacturer info: %s, %s, S/N: %s",
             data.device.manufacturer.c_str(),
             data.device.model.c_str(),
             data.device.serial_number.c_str());
}

bool PowercomHidProtocol::write_16bit_value(uint8_t report_id, uint16_t value) {
    if (!parent_->is_connected()) {
        return false;
    }
    
    uint8_t data[3] = {report_id, static_cast<uint8_t>(value & 0xFF), 
                       static_cast<uint8_t>((value >> 8) & 0xFF)};
    
    esp_err_t ret = parent_->hid_set_report(HID_REPORT_TYPE_FEATURE, report_id,
                                           data, sizeof(data), parent_->get_protocol_timeout());
    
    return (ret == ESP_OK);
}

bool PowercomHidProtocol::write_8bit_value(uint8_t report_id, uint8_t value) {
    if (!parent_->is_connected()) {
        return false;
    }
    
    uint8_t data[2] = {report_id, value};
    
    esp_err_t ret = parent_->hid_set_report(HID_REPORT_TYPE_FEATURE, report_id,
                                           data, sizeof(data), parent_->get_protocol_timeout());
    
    return (ret == ESP_OK);
}

// Delay configuration methods
bool PowercomHidProtocol::set_shutdown_delay(int seconds) {
    ESP_LOGI(PWR_TAG, "Setting shutdown delay to %d seconds", seconds);
    
    if (seconds < 0 || seconds > 7200) {
        ESP_LOGW(PWR_TAG, "Shutdown delay %d seconds out of range (0-7200)", seconds);
        return false;
    }
    
    // Convert to raw value (reverse of parse: raw = seconds / 0.078125)
    uint16_t raw_value = static_cast<uint16_t>(seconds / 0.078125f);
    
    // Try both shutdown delay reports
    bool success = write_16bit_value(REPORT_ID_SHUTDOWN_DELAY, raw_value);
    if (!success) {
        success = write_16bit_value(REPORT_ID_OUTPUT_SHUTDOWN_DELAY, raw_value);
    }
    
    if (success) {
        ESP_LOGI(PWR_TAG, "Shutdown delay set to %d seconds (raw: %d)", seconds, raw_value);
    } else {
        ESP_LOGW(PWR_TAG, "Failed to set shutdown delay");
    }
    
    return success;
}

bool PowercomHidProtocol::set_start_delay(int seconds) {
    ESP_LOGI(PWR_TAG, "Setting startup delay to %d seconds", seconds);
    
    if (seconds < 0 || seconds > 7200) {
        ESP_LOGW(PWR_TAG, "Startup delay %d seconds out of range (0-7200)", seconds);
        return false;
    }
    
    // Convert to raw value
    uint16_t raw_value = static_cast<uint16_t>(seconds / 0.078125f);
    
    // Try both startup delay reports
    bool success = write_16bit_value(REPORT_ID_START_DELAY, raw_value);
    if (!success) {
        success = write_16bit_value(REPORT_ID_OUTPUT_START_DELAY, raw_value);
    }
    
    if (success) {
        ESP_LOGI(PWR_TAG, "Startup delay set to %d seconds (raw: %d)", seconds, raw_value);
    } else {
        ESP_LOGW(PWR_TAG, "Failed to set startup delay");
    }
    
    return success;
}

bool PowercomHidProtocol::set_reboot_delay(int seconds) {
    ESP_LOGI(PWR_TAG, "Setting reboot delay to %d seconds", seconds);
    
    // For PowerCOM, reboot involves both shutdown and startup delays
    bool shutdown_ok = set_shutdown_delay(seconds);
    bool startup_ok = set_start_delay(seconds);
    
    if (shutdown_ok && startup_ok) {
        ESP_LOGI(PWR_TAG, "Reboot delay set to %d seconds", seconds);
        return true;
    } else {
        ESP_LOGW(PWR_TAG, "Failed to set reboot delay (shutdown: %s, startup: %s)",
                 shutdown_ok ? "OK" : "FAIL", startup_ok ? "OK" : "FAIL");
        return false;
    }
}

// Test control methods
bool PowercomHidProtocol::start_battery_test_quick() {
    ESP_LOGI(PWR_TAG, "Starting PowerCOM quick battery test");
    return write_8bit_value(REPORT_ID_BATTERY_TEST, TEST_COMMAND_START);
}

bool PowercomHidProtocol::start_battery_test_deep() {
    ESP_LOGI(PWR_TAG, "Starting PowerCOM deep battery test");
    // PowerCOM might use same command for both quick and deep tests
    return write_8bit_value(REPORT_ID_BATTERY_TEST, TEST_COMMAND_START);
}

bool PowercomHidProtocol::stop_battery_test() {
    ESP_LOGI(PWR_TAG, "Stopping PowerCOM battery test");
    return write_8bit_value(REPORT_ID_BATTERY_TEST, TEST_COMMAND_STOP);
}

bool PowercomHidProtocol::start_ups_test() {
    ESP_LOGI(PWR_TAG, "Starting PowerCOM UPS test");
    // Try report 0x15 for UPS test (same as battery test)
    return write_8bit_value(REPORT_ID_BATTERY_TEST, TEST_COMMAND_START);
}

bool PowercomHidProtocol::stop_ups_test() {
    ESP_LOGI(PWR_TAG, "Stopping PowerCOM UPS test");
    return write_8bit_value(REPORT_ID_BATTERY_TEST, TEST_COMMAND_STOP);
}

// Beeper control methods
bool PowercomHidProtocol::beeper_enable() {
    ESP_LOGI(PWR_TAG, "Enabling PowerCOM beeper");
    return write_8bit_value(REPORT_ID_BEEPER_CONTROL, BEEPER_ENABLE);
}

bool PowercomHidProtocol::beeper_disable() {
    ESP_LOGI(PWR_TAG, "Disabling PowerCOM beeper");
    return write_8bit_value(REPORT_ID_BEEPER_CONTROL, BEEPER_DISABLE);
}

bool PowercomHidProtocol::beeper_mute() {
    ESP_LOGI(PWR_TAG, "Muting PowerCOM beeper");
    return write_8bit_value(REPORT_ID_BEEPER_CONTROL, BEEPER_MUTE);
}

bool PowercomHidProtocol::beeper_test() {
    ESP_LOGI(PWR_TAG, "Testing PowerCOM beeper");
    return write_8bit_value(REPORT_ID_BEEPER_CONTROL, BEEPER_TEST);
}

// Timer polling method
bool PowercomHidProtocol::read_timer_data(UpsData &data) {
    ESP_LOGV(PWR_TAG, "Reading PowerCOM timer data");
    
    // Check if shutdown is imminent
    bool shutdown_imminent = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::SHUTDOWN_IMMINENT);
    bool shutdown_requested = read_bit_status(REPORT_ID_PRESENT_STATUS, StatusBits::SHUTDOWN_REQUESTED);
    
    if (shutdown_imminent || shutdown_requested) {
        // Check Report 0x27 for shutdown imminent timer
        uint8_t shutdown_timer = read_8bit_value(0x27);
        if (shutdown_timer > 0 && shutdown_timer < 255) {
            data.test.timer_shutdown = static_cast<int>(shutdown_timer);
            ESP_LOGD(PWR_TAG, "Shutdown timer: %d seconds", data.test.timer_shutdown);
        }
    }
    
    // Check for other timers if available
    // Report 0x28 might contain timer information
    uint8_t powercom1 = read_8bit_value(0x28);
    if (powercom1 > 0) {
        // Interpret based on known patterns
        ESP_LOGV(PWR_TAG, "POWERCOM1 register: 0x%02X", powercom1);
    }
    
    return (data.test.timer_shutdown > 0 || data.test.timer_start > 0 || data.test.timer_reboot > 0);
}

}  // namespace ups_hid
}  // namespace esphome

// Protocol Factory Self-Registration
#include "protocol_factory.h"

namespace esphome {
namespace ups_hid {

// Creator function for PowerCOM protocol
std::unique_ptr<UpsProtocolBase> create_powercom_protocol(UpsHidComponent* parent) {
    return std::make_unique<PowercomHidProtocol>(parent);
}

} // namespace ups_hid
} // namespace esphome

// Register PowerCOM protocol for vendor ID 0x0D9F
REGISTER_UPS_PROTOCOL_FOR_VENDOR(
    0x0D9F,                         // vendor_id
    powercom_protocol,             // protocol_name (для внутреннего использования)
    esphome::ups_hid::create_powercom_protocol, // creator_func
    "PowerCOM HID",                // name_str
    "PowerCOM WOW series UPS protocol with HID support", // desc_str
    80                             // priority
);