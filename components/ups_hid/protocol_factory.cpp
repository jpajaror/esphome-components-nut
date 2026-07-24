#include "protocol_factory.h"
#include "protocols.h"
#include "ups_hid.h"
#include "esphome/core/log.h"
#include "esphome/components/logger/logger.h"
#include <algorithm>

namespace esphome {
namespace ups_hid {

static const char *const FACTORY_TAG = "ups_hid.factory";

// Static registry implementations
std::unordered_map<uint16_t, std::vector<ProtocolFactory::ProtocolInfo>>& ProtocolFactory::get_vendor_registry() {
    // Durch das static wird die Map erst exakt im Moment des allerersten Eintrags im Speicher erzeugt
    static std::unordered_map<uint16_t, std::vector<ProtocolFactory::ProtocolInfo>> instance;
    return instance;
}

std::vector<ProtocolFactory::ProtocolInfo>& ProtocolFactory::get_fallback_registry() {
    static std::vector<ProtocolFactory::ProtocolInfo> instance;
    return instance;
}

void ProtocolFactory::ensure_initialized() {
    static bool initialized = false;
    if (!initialized) {
        if (esphome::logger::global_logger != nullptr) {
            ESP_LOGD(FACTORY_TAG, "Protocol factory registries initializing manually...");
        }

        // 1. APC Protokoll (Vendor 0x051D)
        ProtocolInfo apc_info;
        apc_info.name = "APC HID Protocol";
        apc_info.description = "APC Back-UPS and Smart-UPS HID protocol implementation";
        apc_info.priority = 100;
        apc_info.creator = [](UpsHidComponent* parent) {
            return std::unique_ptr<UpsProtocolBase>(std::make_unique<ApcHidProtocol>(parent));
        };
        get_vendor_registry()[0x051D].push_back(apc_info);

        // 2. CyberPower Protokoll (Vendor 0x0742)
        ProtocolInfo cyber_info;
        cyber_info.name = "CyberPower HID Protocol";
        cyber_info.description = "CyberPower HID UPS protocol implementation";
        cyber_info.priority = 100;
        cyber_info.creator = [](UpsHidComponent* parent) {
            return std::unique_ptr<UpsProtocolBase>(std::make_unique<CyberPowerProtocol>(parent));
        };
        get_vendor_registry()[0x0742].push_back(cyber_info);

        // 3. Goldenmate Protokoll (Vendor 0x1234)
        ProtocolInfo golden_info;
        golden_info.name = "Goldenmate HID Protocol";
        golden_info.description = "Goldenmate HID UPS protocol implementation";
        golden_info.priority = 100;
        golden_info.creator = [](UpsHidComponent* parent) {
            return std::unique_ptr<UpsProtocolBase>(std::make_unique<GoldenmateHidProtocol>(parent));
        };
        get_vendor_registry()[0x1234].push_back(golden_info);

        // 4. Generic Protokoll (Weltweiter Fallback)
        ProtocolInfo generic_info;
        generic_info.name = "Generic HID Protocol";
        generic_info.description = "Generic HID UPS protocol implementation";
        generic_info.priority = 10;
        generic_info.creator = [](UpsHidComponent* parent) {
            return std::unique_ptr<UpsProtocolBase>(std::make_unique<GenericHidProtocol>(parent));
        };
        get_fallback_registry().push_back(generic_info);

        initialized = true;
        if (esphome::logger::global_logger != nullptr) {
            ESP_LOGD(FACTORY_TAG, "Protocol factory registries successfully initialized!");
        }
    }
}

void ProtocolFactory::register_protocol_for_vendor(uint16_t vendor_id,const ProtocolInfo& info) {
    ensure_initialized();
    
    auto& registry = get_vendor_registry();
    registry[vendor_id].push_back(info);
    
    // Sort by priority (higher first)
    std::sort(registry[vendor_id].begin(), registry[vendor_id].end(),
              [](const ProtocolInfo& a, const ProtocolInfo& b) {
                  return a.priority > b.priority;
              });
    
    if (esphome::logger::global_logger != nullptr)
        ESP_LOGI(FACTORY_TAG, "Registered protocol '%s' for vendor 0x%04X (priority %d)",
                 info.name.c_str(), vendor_id, info.priority);
}

void ProtocolFactory::register_fallback_protocol(const ProtocolInfo& info) {
    ensure_initialized();
    
    auto& registry = get_fallback_registry();
    registry.push_back(info);
    
    // Sort by priority (higher first)
    std::sort(registry.begin(), registry.end(),
              [](const ProtocolInfo& a, const ProtocolInfo& b) {
                  return a.priority > b.priority;
              });
    
    if (esphome::logger::global_logger != nullptr)
        ESP_LOGI(FACTORY_TAG, "Registered fallback protocol '%s' (priority %d)",
                 info.name.c_str(), info.priority);
}

std::unique_ptr<UpsProtocolBase> ProtocolFactory::create_for_vendor(uint16_t vendor_id, UpsHidComponent* parent) {
    ensure_initialized();

    if (!parent) {
        if (logger::global_logger != nullptr) {
            ESP_LOGE(FACTORY_TAG, "Cannot create protocol with null parent component");
        }
        return nullptr;
    }
    
    if (logger::global_logger != nullptr) {
        ESP_LOGI(FACTORY_TAG, "--- Factory Auto-Detect Start: Looking for Vendor ID 0x%04X ---", vendor_id);
    }

    auto& vendor_registry = get_vendor_registry();
    auto vendor_it = vendor_registry.find(vendor_id);
    
    if (vendor_it != vendor_registry.end()) {
        for (const auto& info : vendor_it->second) {
            // UNSERE NEUE DEBUG-AUSGABE: Zeigt an, welches Protokoll er für DIESE Vendor-ID testet
            if (logger::global_logger != nullptr) {
                ESP_LOGI(FACTORY_TAG, "  [Vendor ID Match] Found registered profile: '%s' for Vendor 0x%04X", 
                         info.name.c_str(), vendor_id);
            }

            auto protocol = info.creator(parent);
            if (protocol) { 
                if (logger::global_logger != nullptr) {
                    ESP_LOGI(FACTORY_TAG, "  => SUCCESS: Loaded '%s' for Vendor 0x%04X", info.name.c_str(), vendor_id);
                }
                return protocol;
            }
        }
    }
    
    // Fallback-Schleife
    auto& fallback_registry = get_fallback_registry();
    for (const auto& info : fallback_registry) {
        // UNSERE NEUE DEBUG-AUSGABE: Zeigt die Fallbacks an, falls die Vendor-ID ins Leere lief
        if (logger::global_logger != nullptr) {
            ESP_LOGI(FACTORY_TAG, "  [Auto-Detect Fallback] Testing general profile: '%s'", info.name.c_str());
        }

        auto protocol = info.creator(parent);
        if (protocol) { 
            if (logger::global_logger != nullptr) {
                ESP_LOGI(FACTORY_TAG, "  => SUCCESS: Loaded fallback profile '%s'", info.name.c_str());
            }
            return protocol;
        }
    }
    
    if (logger::global_logger != nullptr) {
        ESP_LOGW(FACTORY_TAG, "--- Factory Auto-Detect End: No profile worked for 0x%04X ---", vendor_id);
    }
    return nullptr;
}


std::vector<ProtocolFactory::ProtocolInfo> 
ProtocolFactory::get_protocols_for_vendor(uint16_t vendor_id) {
    ensure_initialized();
    
    std::vector<ProtocolInfo> protocols;
    
    // Add vendor-specific protocols first
    auto& vendor_registry = get_vendor_registry();
    auto vendor_it = vendor_registry.find(vendor_id);
    
    if (vendor_it != vendor_registry.end()) {
        for (const auto& info : vendor_it->second) {
            protocols.push_back(info);
        }
    }
    
    // Add fallback protocols
    auto& fallback_registry = get_fallback_registry();
    for (const auto& info : fallback_registry) {
        protocols.push_back(info);
    }
    
    return protocols;
}

std::vector<std::pair<uint16_t, ProtocolFactory::ProtocolInfo>> 
ProtocolFactory::get_all_protocols() {
    ensure_initialized();
    
    std::vector<std::pair<uint16_t, ProtocolInfo>> all_protocols;
    
    // Add vendor-specific protocols
    auto& vendor_registry = get_vendor_registry();
    for (const auto& vendor_pair : vendor_registry) {
        uint16_t vendor_id = vendor_pair.first;
        for (const auto& info : vendor_pair.second) {
            all_protocols.emplace_back(vendor_id, info);
        }
    }
    
    // Add fallback protocols (use 0x0000 as special vendor ID for fallbacks)
    auto& fallback_registry = get_fallback_registry();
    for (const auto& info : fallback_registry) {
        all_protocols.emplace_back(0x0000, info);
    }
    
    return all_protocols;
}

bool ProtocolFactory::has_vendor_support(uint16_t vendor_id) {
    ensure_initialized();
    
    auto& vendor_registry = get_vendor_registry();
    auto it = vendor_registry.find(vendor_id);
    
    // Has support if vendor-specific protocols exist OR fallback protocols exist
    bool has_vendor_specific = (it != vendor_registry.end() && !it->second.empty());
    bool has_fallback = !get_fallback_registry().empty();
    
    return has_vendor_specific || has_fallback;
}

std::unique_ptr<UpsProtocolBase> ProtocolFactory::create_by_name(const std::string& protocol_name, UpsHidComponent* parent) {
    ensure_initialized();

    if (!parent) {
        if (logger::global_logger != nullptr) {
            ESP_LOGE(FACTORY_TAG, "Cannot create protocol with null parent component");
        }
        return nullptr;
    }

    if (logger::global_logger != nullptr) {
        ESP_LOGD(FACTORY_TAG, "Creating protocol by name: %s", protocol_name.c_str());
    }

    // Search through all registered protocols to find one with matching name
    auto& vendor_registry = get_vendor_registry();
    for (const auto& vendor_pair : vendor_registry) {
        for (const auto& info : vendor_pair.second) {

            // UNSERE NEUE DEBUG-AUSGABE: Zeigt jedes registrierte Hersteller-Protokoll an
            if (logger::global_logger != nullptr) {
                ESP_LOGI(FACTORY_TAG, "  [Vendor Loop] Checking registered protocol: '%s' against search: '%s'", 
                         info.name.c_str(), protocol_name.c_str());
            }

            // Match protocol name (case-insensitive)
            std::string info_name_lower = info.name;
            std::string protocol_name_lower = protocol_name;
            std::transform(info_name_lower.begin(), info_name_lower.end(), info_name_lower.begin(), ::tolower);
            std::transform(protocol_name_lower.begin(), protocol_name_lower.end(), protocol_name_lower.begin(), ::tolower);

            // Check if the name matches (e.g., "apc" in "apc hid protocol")
            if (info_name_lower.find(protocol_name_lower) != std::string::npos) {
                if (logger::global_logger != nullptr) {
                    ESP_LOGD(FACTORY_TAG, "Found matching protocol '%s' for name '%s'",
                             info.name.c_str(), protocol_name.c_str());
                }
                auto protocol = info.creator(parent);
                if (protocol) {
                    if (logger::global_logger != nullptr) {
                        ESP_LOGI(FACTORY_TAG, "Successfully created protocol '%s' by name",
                                 protocol->get_protocol_name().c_str());
                    }
                    return protocol;
                }
            }
        }
    }

    // Search through fallback protocols
    auto& fallback_registry = get_fallback_registry();
    for (const auto& info : fallback_registry) {

        // UNSERE NEUE DEBUG-AUSGABE: Zeigt jedes registrierte Fallback-Protokoll an
        if (logger::global_logger != nullptr) {
            ESP_LOGI(FACTORY_TAG, "  [Fallback Loop] Checking registered protocol: '%s' against search: '%s'", 
                     info.name.c_str(), protocol_name.c_str());
        }

        std::string info_name_lower = info.name;
        std::string protocol_name_lower = protocol_name;
        std::transform(info_name_lower.begin(), info_name_lower.end(), info_name_lower.begin(), ::tolower);
        std::transform(protocol_name_lower.begin(), protocol_name_lower.end(), protocol_name_lower.begin(), ::tolower);

        // Check if the name matches (e.g., "apc" in "apc hid protocol")
        if (info_name_lower.find(protocol_name_lower) != std::string::npos) {
            if (logger::global_logger != nullptr) {
                ESP_LOGD(FACTORY_TAG, "Found matching fallback protocol '%s' for name '%s'", 
                         info.name.c_str(), protocol_name.c_str());
            }
            auto protocol = info.creator(parent);
            if (protocol) {
                if (logger::global_logger != nullptr) {
                    ESP_LOGI(FACTORY_TAG, "Successfully created fallback protocol '%s' by name", 
                             protocol->get_protocol_name().c_str());
                }
                return protocol;
            }
        }
    }

    if (logger::global_logger != nullptr) {
        ESP_LOGE(FACTORY_TAG, "No protocol found with name containing '%s'", protocol_name.c_str());
    }
    return nullptr;
}

} // namespace ups_hid
} // namespace esphome
