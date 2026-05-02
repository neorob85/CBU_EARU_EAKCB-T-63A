#pragma once
#include <Arduino.h>
#include <PrefsManager.h>

// All user-configurable values stored at runtime in PrefsManager namespace "cfg".
// Namespace "cfg" existing + wifi_ssid non-empty = device is provisioned.
struct ConfigData {
    String  wifi_ssid;
    String  wifi_pass;
    String  mqtt_server;
    int32_t mqtt_port   = 1883;
    int32_t mqtt_buf    = 2048;
    String  mqtt_user;
    String  mqtt_pass;
    String  dashboard_url;
    String  device_name = "EnergyMeter";
    String  device_sn   = "000000";
    String  device_area;
    String  ha_prefix   = "homeassistant";

    // Sensor / publish parameters
    int32_t energy_interval = 1000;   // ms — energy publish interval
    float   ntc_vref        = 3.3f;   // V   — NTC reference voltage
    float   ntc_rref        = 47.0f;  // kΩ  — NTC reference resistance
    float   ntc_tref        = 25.0f;  // °C  — NTC reference temperature
    int32_t ntc_beta        = 3950;   // K   — NTC beta coefficient
    float   ntc_rpull       = 200.0f; // kΩ  — NTC pull-up resistance

    void load(PrefsManager &ps) {
        ps.read("cfg/wifi_ssid",  wifi_ssid,        String(""));
        ps.read("cfg/wifi_pass",  wifi_pass,        String(""));
        ps.read("cfg/mqtt_srv",   mqtt_server,      String(""));
        ps.read("cfg/mqtt_port",  mqtt_port,        (int32_t)1883);
        ps.read("cfg/mqtt_buf",   mqtt_buf,         (int32_t)2048);
        ps.read("cfg/mqtt_user",  mqtt_user,        String(""));
        ps.read("cfg/mqtt_pass",  mqtt_pass,        String(""));
        ps.read("cfg/dash_url",   dashboard_url,    String(""));
        ps.read("cfg/dev_name",   device_name,      String("EnergyMeter"));
        ps.read("cfg/dev_sn",     device_sn,        String("000000"));
        ps.read("cfg/dev_area",   device_area,      String(""));
        ps.read("cfg/ha_prefix",  ha_prefix,        String("homeassistant"));
        ps.read("cfg/energy_int", energy_interval,  (int32_t)1000);
        ps.read("cfg/ntc_vref",   ntc_vref,         3.3f);
        ps.read("cfg/ntc_rref",   ntc_rref,         47.0f);
        ps.read("cfg/ntc_tref",   ntc_tref,         25.0f);
        ps.read("cfg/ntc_beta",   ntc_beta,         (int32_t)3950);
        ps.read("cfg/ntc_rpull",  ntc_rpull,        200.0f);
    }

    void save(PrefsManager &ps) const {
        ps.write("cfg/wifi_ssid", wifi_ssid);
        ps.write("cfg/wifi_pass", wifi_pass);
        ps.write("cfg/mqtt_srv",  mqtt_server);
        ps.write("cfg/mqtt_port", mqtt_port);
        ps.write("cfg/mqtt_buf",  mqtt_buf);
        ps.write("cfg/mqtt_user", mqtt_user);
        ps.write("cfg/mqtt_pass", mqtt_pass);
        ps.write("cfg/dash_url",  dashboard_url);
        ps.write("cfg/dev_name",  device_name);
        ps.write("cfg/dev_sn",    device_sn);
        ps.write("cfg/dev_area",  device_area);
        ps.write("cfg/ha_prefix", ha_prefix);
        ps.write("cfg/energy_int", energy_interval);
        ps.write("cfg/ntc_vref",   ntc_vref);
        ps.write("cfg/ntc_rref",   ntc_rref);
        ps.write("cfg/ntc_tref",   ntc_tref);
        ps.write("cfg/ntc_beta",   ntc_beta);
        ps.write("cfg/ntc_rpull",  ntc_rpull);
    }

    bool isProvisioned() const { return wifi_ssid.length() > 0; }
};
