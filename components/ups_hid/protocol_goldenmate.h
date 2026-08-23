#pragma once

#include "ups_hid.h"

namespace esphome {
namespace ups_hid {

/**
 * GoldenMate / iDowell BMS Smart-Battery HID Protocol
 *
 * These units report manufacturer "-BMS-" and product "Smart-Battery" and ship
 * under two different USB vendor IDs:
 *
 *   0x075D:0x0300  iDowell, the common GoldenMate LiFePO4 / Pro units
 *   0x06DA:0xFFFF  GoldenMate 1000VA/800W LiFePO4, same firmware and HID
 *                  descriptor on the shared Phoenixtec vendor ID
 *
 * NUT reached the same conclusion for its idowell-hid subdriver: both IDs are
 * claimed, but 0x06DA is gated on the device strings because that vendor ID is
 * shared with Liebert and MGE units (see NUT issue #3501 / PR #3502). We do the
 * same in detect() so an Eaton/MGE Ellipse on 0x06DA still falls through to the
 * generic HID protocol.
 *
 * Data sources:
 *   Report 0x01 (Feature, 21 bytes) - binary status:
 *     byte 11: battery % (0-100)
 *     bytes 12-13 LE16: runtime to empty (seconds)
 *     byte 16: nominal input voltage (static)
 *     byte 20: temperature (°C, BMS internal)
 *
 *   Report 0x0C (Feature, 65 bytes) - contains packed Megatec ASCII at bytes 30-61:
 *     "LLLL FFFF OOOO BBB FFF VVV TTT SSSSSSSS"
 *     Field 1 (4 chars): Load % × 10 (e.g., "0107" = 10.7%)
 *     Field 2 (4 chars): Low voltage transfer (e.g., "1400" = 140.0V)
 *     Field 3 (4 chars): High voltage transfer (e.g., "2084" = 208.4V)
 *     Field 4 (3 chars): Battery % (e.g., "099" = 99%)
 *     Field 5 (3 chars): Frequency × 10 (e.g., "599" = 59.9 Hz)
 *     Field 6 (3 chars): Battery voltage × 100 (e.g., "205" = 2.05V)
 *     Field 7 (3 chars): Temperature × 10 (e.g., "350" = 35.0°C)
 *     Field 8 (8 chars): Status bits ("01000000" = on battery)
 *       bit 6: on battery (1 = AC lost)
 */
class GoldenMateProtocol : public UpsProtocolBase {
 public:
  GoldenMateProtocol(UpsHidComponent *parent) : UpsProtocolBase(parent) {}

  bool detect() override;
  bool initialize() override;
  bool read_data(UpsData &data) override;
  DeviceInfo::DetectedProtocol get_protocol_type() const override { return DeviceInfo::PROTOCOL_GENERIC_HID; }
  std::string get_protocol_name() const override { return "GoldenMate BMS"; }

  // iDowell: the vendor ID is unambiguous, every device on it is one of these.
  static const uint16_t VENDOR_ID_IDOWELL = 0x075D;
  // Phoenixtec: shared with Liebert / MGE, so claims here must be string-gated.
  static const uint16_t VENDOR_ID_PHOENIXTEC = 0x06DA;

 private:
  static const uint8_t REPORT_ID_STATUS = 0x01;
  static const uint8_t REPORT_ID_MEGATEC = 0x0C;

  struct HidReport {
    uint8_t report_id;
    std::vector<uint8_t> data;
    HidReport() : report_id(0) {}
  };

  // True when the USB string descriptors identify a -BMS- Smart-Battery unit.
  // Mirrors idowell_is_goldenmate() in NUT's idowell-hid.c.
  bool has_bms_strings(bool &strings_readable);
  bool vendor_gate_passes();

  bool read_feature_report(uint8_t report_id, HidReport &report);
  bool parse_binary_status(const HidReport &report, UpsData &data);
  bool parse_megatec_string(const HidReport &report, UpsData &data);
  std::string extract_packed_ascii(const uint8_t *data, size_t offset, size_t len);
};

}  // namespace ups_hid
}  // namespace esphome
