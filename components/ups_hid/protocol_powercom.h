#pragma once

#include "ups_hid.h"
#include "data_composite.h"
#include "data_device.h"

namespace esphome {
namespace ups_hid {

/**
 * PowerCOM HID Protocol Implementation
 * 
 * Based on NUT PowerCOM HID 0.5 driver analysis
 * Supports POWERCOM WOW series UPS devices
 */
class PowercomHidProtocol : public UpsProtocolBase {
public:
    explicit PowercomHidProtocol(UpsHidComponent* parent) : UpsProtocolBase(parent) {}
    ~PowercomHidProtocol() override = default;

    // Protocol identification
    DeviceInfo::DetectedProtocol get_protocol_type() const override { 
        return DeviceInfo::PROTOCOL_POWERCOM_HID; 
    }
    std::string get_protocol_name() const override { 
        return "PowerCOM HID"; 
    }

    // Core protocol interface
    bool detect() override;
    bool initialize() override;
    bool read_data(UpsData &data) override;
    
    // Delay configuration methods
    bool set_shutdown_delay(int seconds) override;
    bool set_start_delay(int seconds) override;
    bool set_reboot_delay(int seconds) override;
    
    // Test control methods
    bool start_battery_test_quick() override;
    bool start_battery_test_deep() override;
    bool stop_battery_test() override;
    bool start_ups_test() override;
    bool stop_ups_test() override;
    
    // Beeper control
    bool beeper_enable() override;
    bool beeper_disable() override;
    bool beeper_mute() override;
    bool beeper_test() override;
    
    // Timer polling method
    bool read_timer_data(UpsData &data) override;

private:
    // Report reading helpers
    bool read_feature_report(uint8_t report_id, uint8_t* buffer, size_t& buffer_len);
    bool read_input_report(uint8_t report_id, uint8_t* buffer, size_t& buffer_len);
    
    // String descriptor reading
    bool read_string_descriptor(uint8_t index, std::string& result);
    
    // Data parsing methods
    void parse_power_summary(UpsData &data);
    void parse_battery_info(UpsData &data);
    void parse_present_status(UpsData &data);
    void parse_input_output_info(UpsData &data);
    void parse_delays(UpsData &data);
    void parse_manufacturer_info(UpsData &data);
    
    // Helper methods for specific reports
    uint16_t read_16bit_value(uint8_t report_id, uint8_t offset = 0);
    uint8_t read_8bit_value(uint8_t report_id, uint8_t offset = 0);
    bool read_bit_status(uint8_t report_id, uint8_t bit_offset);
    
    // Configuration methods
    bool write_16bit_value(uint8_t report_id, uint16_t value);
    bool write_8bit_value(uint8_t report_id, uint8_t value);
    
    // PowerCOM specific constants
    static constexpr uint8_t REPORT_ID_PRESENT_STATUS = 0x14;
    static constexpr uint8_t REPORT_ID_BATTERY_TEST = 0x15;
    static constexpr uint8_t REPORT_ID_BEEPER_CONTROL = 0x13;
    static constexpr uint8_t REPORT_ID_SHUTDOWN_DELAY = 0x0F;
    static constexpr uint8_t REPORT_ID_START_DELAY = 0x10;
    static constexpr uint8_t REPORT_ID_OUTPUT_SHUTDOWN_DELAY = 0x23;
    static constexpr uint8_t REPORT_ID_OUTPUT_START_DELAY = 0x24;
    
    // String descriptor indices
    static constexpr uint8_t STRING_ID_PRODUCT = 1;
    static constexpr uint8_t STRING_ID_SERIAL = 2;
    static constexpr uint8_t STRING_ID_MANUFACTURER = 3;
    static constexpr uint8_t STRING_ID_DEVICE_CHEMISTRY = 4;
    
    // Status bit positions (from NUT analysis)
    struct StatusBits {
        static constexpr uint8_t CHARGING = 0;
        static constexpr uint8_t DISCHARGING = 1;
        static constexpr uint8_t AC_PRESENT = 2;
        static constexpr uint8_t BATTERY_PRESENT = 3;
        static constexpr uint8_t BELOW_CAPACITY_LIMIT = 4;
        static constexpr uint8_t TIME_LIMIT_EXPIRED = 5;
        static constexpr uint8_t NEED_REPLACEMENT = 6;
        static constexpr uint8_t VOLTAGE_NOT_REGULATED = 7;
        static constexpr uint8_t SHUTDOWN_REQUESTED = 8;
        static constexpr uint8_t SHUTDOWN_IMMINENT = 9;
        static constexpr uint8_t COMMUNICATION_LOST = 10;
        static constexpr uint8_t OVERLOAD = 11;
        static constexpr uint8_t POWERCOM3 = 15;
    };
    
    // Test command values
    static constexpr uint8_t TEST_COMMAND_START = 0x01;
    static constexpr uint8_t TEST_COMMAND_STOP = 0x00;
    
    // Beeper control values
    static constexpr uint8_t BEEPER_ENABLE = 0x01;
    static constexpr uint8_t BEEPER_DISABLE = 0x00;
    static constexpr uint8_t BEEPER_MUTE = 0x02;
    static constexpr uint8_t BEEPER_TEST = 0x03;
};

} // namespace ups_hid  
} // namespace esphome