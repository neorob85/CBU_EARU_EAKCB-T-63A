// =======================================================================================================================
// ESPOTADASH
// =======================================================================================================================

const char *DASHBOARD_URL = CFG_DASHBOARD_URL;
const char *FIRMWARE_VERSION = CFG_FIRMWARE_VERSION;

// =======================================================================================================================
// MQTT
// =======================================================================================================================

const char *MQTT_SERVER = CFG_MQTT_SERVER;
const int MQTT_PORT = CFG_MQTT_PORT;
const int MQTT_BUFFER_SIZE = CFG_MQTT_BUFFER_SIZE;

// =======================================================================================================================
// HOME ASSISTANT
// =======================================================================================================================

const char *DEVICE_NAME = CFG_DEVICE_NAME;
const char *DEVICE_VERSION = CFG_DEVICE_VERSION;
const char *DEVICE_MANUFACTURER = CFG_DEVICE_MANUFACTURER;
const char *DEVICE_MODEL = CFG_DEVICE_MODEL;
const char *DEVICE_SN = CFG_DEVICE_SN;
const char *DEVICE_SUGGESTED_AREA = CFG_DEVICE_SUGGESTED_AREA;
const char *DEVICE_HA_PREFIX = CFG_DEVICE_HA_PREFIX;

// =======================================================================================================================
// ENERGY METER
// =======================================================================================================================

const unsigned long ENERGY_PUBLISH_INTERVAL = 1000; // ms

// =======================================================================================================================
// TEMPERATURE SENSOR
// =======================================================================================================================

float v_ref = 3.3;    // V
float r_ref = 47.0;   // kΩ
float t_ref = 25.0;   // °C
int beta = 3950;      // K
float r_pullup = 200.0; // kΩ
