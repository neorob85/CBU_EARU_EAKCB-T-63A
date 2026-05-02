#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

// Config portal served on port 8080 during normal (STA) operation.
// The user navigates to http://<device-ip>:8080 to change any config value.
// Password fields are left blank by default; submitting a blank password
// field keeps the current value.
class ConfigPortal {
    ConfigData  &_config;
    PrefsManager &_ps;
    WebServer    _server;

    static String htmlEscape(const String &s) {
        String r;
        r.reserve(s.length() + 8);
        for (unsigned int i = 0; i < s.length(); i++) {
            char c = s[i];
            switch (c) {
                case '&':  r += "&amp;";  break;
                case '<':  r += "&lt;";   break;
                case '>':  r += "&gt;";   break;
                case '"':  r += "&quot;"; break;
                case '\'': r += "&#39;";  break;
                default:   r += c;
            }
        }
        return r;
    }

    static const char *CSS() {
        return
            "*{box-sizing:border-box}"
            "body{font-family:sans-serif;max-width:520px;margin:24px auto;padding:0 12px;background:#f0f2f5}"
            "h1{color:#1565c0;font-size:1.3em;margin:0 0 4px}"
            ".meta{font-size:.78em;color:#888;margin:0 0 16px}"
            ".card{background:#fff;border-radius:8px;padding:14px 16px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
            "h2{font-size:.82em;color:#555;margin:0 0 10px;padding-bottom:5px;border-bottom:1px solid #eee;text-transform:uppercase;letter-spacing:.06em}"
            "label{display:block;margin-top:8px;font-size:.8em;color:#666;font-weight:600}"
            "input{display:block;width:100%;padding:7px 9px;border:1px solid #ccc;border-radius:4px;margin-top:3px;font-size:.9em;background:#fafafa}"
            "input:focus{outline:none;border-color:#1565c0;background:#fff}"
            "button{display:block;width:100%;margin-top:16px;padding:11px;background:#1565c0;color:#fff;border:none;border-radius:6px;font-size:.95em;cursor:pointer;font-weight:600}"
            "button:hover{background:#0d47a1}"
            ".hint{font-size:.75em;color:#999;margin-top:3px}"
            ".pwd-wrap{display:flex;gap:6px;align-items:stretch;margin-top:3px}"
            ".pwd-wrap input{flex:1;margin-top:0}"
            ".pwd-toggle{display:inline-block;width:auto;margin-top:0;padding:6px 10px;font-size:.8em;background:#e8eaf6;color:#1565c0;border:1px solid #c5cae9;border-radius:4px;font-weight:600;white-space:nowrap}"
            ".pwd-toggle:hover{background:#c5cae9}";
    }

    // Helper: field with current value pre-filled
    static void appendField(String &h, const char *label, const char *name,
                            const String &value, const char *placeholder = "") {
        h += "<label>";
        h += label;
        h += "<input name='";
        h += name;
        h += "' value='";
        h += htmlEscape(value);
        h += "'";
        if (placeholder && placeholder[0]) {
            h += " placeholder='";
            h += placeholder;
            h += "'";
        }
        h += "></label>";
    }

    // Helper: password field (always blank — keep-current hint shown) with show/hide toggle
    static void appendPassword(String &h, const char *label, const char *name) {
        h += "<label>";
        h += label;
        h += "<div class='pwd-wrap'>"
             "<input type='password' name='";
        h += name;
        h += "' autocomplete='off' placeholder='Leave empty to keep current'>"
             "<button type='button' class='pwd-toggle' onclick='togglePwd(this)'>Mostra</button>"
             "</div></label>";
    }

    void handleRoot() {
        String h;
        h.reserve(4096);
        h  = "<!DOCTYPE html><html lang='it'><head>"
             "<meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>EARU Configuration</title>"
             "<style>";
        h += CSS();
        h += "</style></head><body>"
             "<h1>EARU &mdash; Configuration</h1>"
             "<p class='meta'>IP: ";
        IPAddress ip = WiFi.localIP();
        h += String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
        h += " &mdash; fw ";
        h += CFG_FIRMWARE_VERSION;
        h += "</p>"
             "<form method='POST' action='/save'>"

             "<div class='card'><h2>WiFi</h2>";
        appendField(h, "SSID", "wifi_ssid", _config.wifi_ssid);
        appendPassword(h, "WiFi Password", "wifi_pass");
        h += "</div>"

             "<div class='card'><h2>MQTT</h2>";
        appendField(h, "Server", "mqtt_server", _config.mqtt_server, "<IP>");
        h += "<label>Port<input type='number' name='mqtt_port' value='";
        h += String(_config.mqtt_port);
        h += "' min='1' max='65535'></label>";
        appendField(h, "MQTT User", "mqtt_user", _config.mqtt_user);
        appendPassword(h, "MQTT Password", "mqtt_pass");
        h += "</div>"

             "<div class='card'><h2>Device</h2>";
        appendField(h, "Name", "device_name", _config.device_name);
        appendField(h, "Serial Number", "device_sn", _config.device_sn);
        appendField(h, "Area (Home Assistant)", "device_area", _config.device_area, "e.g., Basement");
        h += "</div>"

             "<div class='card'><h2>Advanced</h2>";
        appendField(h, "ESPOTADASH URL", "dashboard_url", _config.dashboard_url, "http://<IP>:<PORT>");
        appendField(h, "HA Discovery Prefix", "ha_prefix", _config.ha_prefix);
        h += "<label>MQTT Buffer (byte)<input type='number' name='mqtt_buf' value='";
        h += String(_config.mqtt_buf);
        h += "' min='256'></label>";
        h += "</div>"

             "<div class='card'><h2>Sensor</h2>"
             "<label>Publication Interval (ms)"
             "<input type='number' name='energy_interval' value='";
        h += String(_config.energy_interval);
        h += "' min='100'></label>"
             "<label>NTC &mdash; Reference Voltage (V)"
             "<input type='number' name='ntc_vref' step='0.01' value='";
        h += String(_config.ntc_vref, 2);
        h += "'></label>"
             "<label>NTC &mdash; Reference Resistance (k&Omega;) at Reference Temperature"
             "<input type='number' name='ntc_rref' step='0.1' value='";
        h += String(_config.ntc_rref, 1);
        h += "'></label>"
             "<label>NTC &mdash; Reference Temperature (&deg;C)"
             "<input type='number' name='ntc_tref' step='0.1' value='";
        h += String(_config.ntc_tref, 1);
        h += "'></label>"
             "<label>NTC &mdash; Beta (K)"
             "<input type='number' name='ntc_beta' value='";
        h += String(_config.ntc_beta);
        h += "'></label>"
             "<label>NTC &mdash; Reference Resistance pull-up (k&Omega;)"
             "<input type='number' name='ntc_rpull' step='0.1' value='";
        h += String(_config.ntc_rpull, 1);
        h += "'></label>"
             "</div>"

             "<button type='submit'>Save and Restart</button>"
             "</form>"
             "<script>function togglePwd(b){var f=b.previousElementSibling;"
             "f.type=f.type==='password'?'text':'password';"
             "b.textContent=f.type==='password'?'Show':'Hide';}</script>"
             "</body></html>";

        _server.send(200, "text/html", h);
    }

    void handleSave() {
        // WiFi credentials: keep current if field left blank
        String ssid = _server.arg("wifi_ssid");
        String pass = _server.arg("wifi_pass");
        if (ssid.length() > 0) _config.wifi_ssid = ssid;
        if (pass.length() > 0) _config.wifi_pass = pass;

        // MQTT
        String srv = _server.arg("mqtt_server");
        if (srv.length() > 0) _config.mqtt_server = srv;

        int port = _server.arg("mqtt_port").toInt();
        if (port > 0 && port <= 65535) _config.mqtt_port = (int32_t)port;

        int buf = _server.arg("mqtt_buf").toInt();
        if (buf >= 256) _config.mqtt_buf = (int32_t)buf;

        _config.mqtt_user = _server.arg("mqtt_user");

        String mpass = _server.arg("mqtt_pass");
        if (mpass.length() > 0) _config.mqtt_pass = mpass;

        // Device
        String dn = _server.arg("device_name");
        if (dn.length() > 0) _config.device_name = dn;
        _config.device_sn   = _server.arg("device_sn");
        _config.device_area = _server.arg("device_area");

        // Advanced
        _config.dashboard_url = _server.arg("dashboard_url");
        String hp = _server.arg("ha_prefix");
        if (hp.length() > 0) _config.ha_prefix = hp;

        // Sensor / publish parameters
        int ei = _server.arg("energy_interval").toInt();
        if (ei >= 100) _config.energy_interval = (int32_t)ei;

        float vref = _server.arg("ntc_vref").toFloat();
        if (vref > 0.0f) _config.ntc_vref = vref;

        float rref = _server.arg("ntc_rref").toFloat();
        if (rref > 0.0f) _config.ntc_rref = rref;

        float tref = _server.arg("ntc_tref").toFloat();
        if (tref > -273.0f) _config.ntc_tref = tref;

        int beta = _server.arg("ntc_beta").toInt();
        if (beta > 0) _config.ntc_beta = (int32_t)beta;

        float rpull = _server.arg("ntc_rpull").toFloat();
        if (rpull > 0.0f) _config.ntc_rpull = rpull;

        _config.save(_ps);

        _server.send(200, "text/html",
            "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
            "<h2>Configuration saved</h2>"
            "<p>The device will restart with the new settings.</p>"
            "</body></html>");

        delay(800);
        ESP.restart();
    }

public:
    ConfigPortal(ConfigData &config, PrefsManager &ps)
        : _config(config), _ps(ps), _server(8080) {}

    void begin() {
        _server.on("/", HTTP_GET,  [this]() { handleRoot(); });
        _server.on("/save", HTTP_POST, [this]() { handleSave(); });
        _server.begin();
        Serial.println("[PORTAL] Config portal on port 8080");
    }

    void loop() { _server.handleClient(); }
};
