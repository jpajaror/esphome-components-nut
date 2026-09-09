#pragma once

#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <string>

namespace esphome {
namespace ups_hid {

// Forward declarations
class UpsProtocolBase;
class UpsHidComponent;

// Forward declarations of the per-protocol creator functions.
//
// Each protocol (protocol_apc.cpp, protocol_cyberpower.cpp, protocol_generic.cpp)
// registers itself via a file-local static object whose constructor runs at
// program start (see REGISTER_UPS_PROTOCOL_FOR_VENDOR / REGISTER_UPS_FALLBACK_PROTOCOL
// below). Nothing outside those files references any symbol they define, which
// means that under ESP-IDF's component-archive linking (each ESPHome component
// folder is built into its own static .a and linked with --gc-sections), the
// linker is free to drop those .o files entirely — including their
// self-registering constructors — since nothing in the rest of the program
// appears to need them. When that happens the protocol silently never
// registers, and both `protocol: auto` and an explicit `protocol: <name>`
// selection fail with "No protocol found with name containing '<name>'" even
// though the code compiled without any error.
//
// Declaring and referencing these functions from protocol_factory.cpp (which
// is unconditionally linked in, since UpsHidComponent calls into
// ProtocolFactory directly) forces the linker to keep each protocol's
// translation unit, and with it, its static registrar.
std::unique_ptr<UpsProtocolBase> create_apc_protocol(UpsHidComponent *parent);
std::unique_ptr<UpsProtocolBase> create_cyberpower_protocol(UpsHidComponent *parent);
std::unique_ptr<UpsProtocolBase> create_generic_protocol(UpsHidComponent *parent);

/**
 * Protocol Factory with Self-Registration Support
 * 
 * Enables protocols to register themselves automatically, following the
 * Open/Closed Principle - new protocols can be added without modifying
 * existing code.
 * 
 * Design Pattern: Factory Method + Registry Pattern
 */
class ProtocolFactory {
public:
    // Protocol creator function type
    using CreatorFunc = std::function<std::unique_ptr<UpsProtocolBase>(UpsHidComponent*)>;
    
    // Protocol metadata for better selection
    struct ProtocolInfo {
        CreatorFunc creator;
        std::string name;
        std::string description;
        std::vector<uint16_t> supported_vendors;
        int priority; // Higher priority = tried first
    };
    
    /**
     * Register a protocol with specific vendor IDs
     */
    static void register_protocol_for_vendor(uint16_t vendor_id, 
                                           const ProtocolInfo& info);
    
    /**
     * Register a fallback protocol (tried when vendor-specific fails)
     */
    static void register_fallback_protocol(const ProtocolInfo& info);
    
    /**
     * Create protocol instance for specific vendor
     */
    static std::unique_ptr<UpsProtocolBase> create_for_vendor(uint16_t vendor_id, 
                                                            UpsHidComponent* parent);
    
    /**
     * Create protocol instance by name (manual selection)
     */
    static std::unique_ptr<UpsProtocolBase> create_by_name(const std::string& protocol_name,
                                                         UpsHidComponent* parent);
    
    /**
     * Get ordered list of protocols to try for a vendor
     * Returns vendor-specific first, then fallbacks by priority
     */
    static std::vector<ProtocolInfo> get_protocols_for_vendor(uint16_t vendor_id);
    
    /**
     * Get list of all registered protocols
     */
    static std::vector<std::pair<uint16_t, ProtocolInfo>> get_all_protocols();
    
    /**
     * Check if vendor has registered protocols
     */
    static bool has_vendor_support(uint16_t vendor_id);

private:
    // Vendor-specific protocol registry
    static std::unordered_map<uint16_t, std::vector<ProtocolInfo>>& get_vendor_registry();
    
    // Fallback protocol registry (sorted by priority)
    static std::vector<ProtocolInfo>& get_fallback_registry();
    
    // Ensure registries are initialized
    static void ensure_initialized();
};

/**
 * Protocol Registration Helper Macros
 * 
 * These macros enable automatic protocol registration at startup
 */

// Forward declare for registration macros
class ProtocolFactory;

// Register protocol for specific vendor
#define REGISTER_UPS_PROTOCOL_FOR_VENDOR(vendor_id, protocol_name, creator_func, name_str, desc_str, prio) \
    namespace { \
        struct protocol_name##_registrar { \
            protocol_name##_registrar() { \
                esphome::ups_hid::ProtocolFactory::ProtocolInfo info; \
                info.creator = creator_func; \
                info.name = name_str; \
                info.description = desc_str; \
                info.supported_vendors = {vendor_id}; \
                info.priority = prio; \
                esphome::ups_hid::ProtocolFactory::register_protocol_for_vendor(vendor_id, info); \
            } \
        }; \
        static protocol_name##_registrar protocol_name##_reg; \
    }

// Register fallback protocol
#define REGISTER_UPS_FALLBACK_PROTOCOL(protocol_name, creator_func, name_str, desc_str, prio) \
    namespace { \
        struct protocol_name##_fallback_registrar { \
            protocol_name##_fallback_registrar() { \
                esphome::ups_hid::ProtocolFactory::ProtocolInfo info; \
                info.creator = creator_func; \
                info.name = name_str; \
                info.description = desc_str; \
                info.supported_vendors = {}; \
                info.priority = prio; \
                esphome::ups_hid::ProtocolFactory::register_fallback_protocol(info); \
            } \
        }; \
        static protocol_name##_fallback_registrar protocol_name##_fallback_reg; \
    }

} // namespace ups_hid
} // namespace esphome