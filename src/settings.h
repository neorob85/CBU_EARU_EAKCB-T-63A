// =======================================================================================================================
// FIRMWARE / HARDWARE — compile-time only constants
// These describe the firmware version and hardware model; they are baked into
// the binary and are not user-configurable at runtime.
// =======================================================================================================================

const char *FIRMWARE_VERSION    = CFG_FIRMWARE_VERSION;
const char *DEVICE_VERSION      = CFG_DEVICE_VERSION;
const char *DEVICE_MANUFACTURER = CFG_DEVICE_MANUFACTURER;
const char *DEVICE_MODEL        = CFG_DEVICE_MODEL;

// Energy publish interval and NTC sensor parameters are user-configurable at
// runtime via the config portal (port 8080). They live in ConfigData and are
// loaded from PrefsManager on boot. See src/config.h.
