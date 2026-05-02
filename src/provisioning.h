#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "config.h"

// Captive-portal provisioning server.
// Starts an open AP "EARU_CBU_EAKCB-M-T" at 10.0.0.1 and serves a web form
// on port 80 where the user selects a WiFi network and fills in all config
// fields. On save, the WiFi connection is tested; on success the config is
// written to PrefsManager and the device restarts.
class ProvisioningServer {
    PrefsManager &_ps;
    WebServer     _server;

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
            "h1{color:#1565c0;font-size:1.3em;margin:0 0 16px}"
            ".card{background:#fff;border-radius:8px;padding:14px 16px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
            "h2{font-size:.82em;color:#555;margin:0 0 10px;padding-bottom:5px;border-bottom:1px solid #eee;text-transform:uppercase;letter-spacing:.06em}"
            "label{display:block;margin-top:8px;font-size:.8em;color:#666;font-weight:600}"
            "input,select{display:block;width:100%;padding:7px 9px;border:1px solid #ccc;border-radius:4px;margin-top:3px;font-size:.9em;background:#fafafa}"
            "input:focus,select:focus{outline:none;border-color:#1565c0;background:#fff}"
            "button{display:block;width:100%;margin-top:16px;padding:11px;background:#1565c0;color:#fff;border:none;border-radius:6px;font-size:.95em;cursor:pointer;font-weight:600}"
            "button:hover{background:#0d47a1}"
            ".hint{font-size:.75em;color:#999;margin-top:3px}"
            ".pwd-wrap{display:flex;gap:6px;align-items:stretch;margin-top:3px}"
            ".pwd-wrap input{flex:1;margin-top:0}"
            ".pwd-toggle{display:inline-block;width:auto;margin-top:0;padding:6px 10px;font-size:.8em;background:#e8eaf6;color:#1565c0;border:1px solid #c5cae9;border-radius:4px;font-weight:600;white-space:nowrap}"
            ".pwd-toggle:hover{background:#c5cae9}";
    }

    void handleRoot() {
        // Load any previously saved values to pre-fill the form.
        ConfigData cfg;
        cfg.load(_ps);

        int n = WiFi.scanNetworks();
        String nets;
        bool storedFound = false;
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            bool sel = (cfg.wifi_ssid.length() > 0 && ssid == cfg.wifi_ssid);
            if (sel) storedFound = true;
            nets += "<option value='";
            nets += htmlEscape(ssid);
            nets += "'";
            if (sel) nets += " selected";
            nets += ">";
            nets += htmlEscape(ssid);
            nets += " (";
            nets += String(WiFi.RSSI(i));
            nets += " dBm)</option>";
        }
        // If the stored SSID was not found in the scan, prepend it so it stays selected.
        if (cfg.wifi_ssid.length() > 0 && !storedFound) {
            String extra = "<option value='";
            extra += htmlEscape(cfg.wifi_ssid);
            extra += "' selected>";
            extra += htmlEscape(cfg.wifi_ssid);
            extra += " (not found in area)</option>";
            nets = extra + nets;
        }
        if (nets.length() == 0)
            nets = "<option value=''>No networks found — refresh the page</option>";

        String wifiPassPlaceholder = cfg.wifi_pass.length() > 0
            ? "Leave empty to retain the saved password"
            : "Leave empty for open networks";

        String h;
        h.reserve(4096);
        h  = "<!DOCTYPE html><html lang='it'><head>"
             "<meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>EARU Setup</title>"
             "<style>";
        h += CSS();
        h += "</style></head><body>"
             "<h1>EARU &mdash; Initial Configuration</h1>"
             "<p class='hint'>Firmware v";
        h += CFG_FIRMWARE_VERSION;
        h += "</p>"
             "<form method='POST' action='/save'>"
             "<div class='card'><h2>WiFi Network</h2>"
             "<label>Available Network"
             "<select name='wifi_ssid'>";
        h += nets;
        h += "</select></label>"
             "<label>WiFi Password"
             "<div class='pwd-wrap'>"
             "<input type='password' name='wifi_pass' autocomplete='off' placeholder='";
        h += wifiPassPlaceholder;
        h += "'>"
             "<button type='button' class='pwd-toggle' onclick='togglePwd(this)'>Show</button>"
             "</div></label></div>"
             "<div class='card'><h2>MQTT</h2>"
             "<label>Server<input name='mqtt_server' value='";
        h += htmlEscape(cfg.mqtt_server);
        h += "' placeholder='<IP>' required></label>"
             "<label>Port<input type='number' name='mqtt_port' value='";
        h += String(cfg.mqtt_port);
        h += "' min='1' max='65535'></label>"
             "<label>MQTT User<input name='mqtt_user' value='";
        h += htmlEscape(cfg.mqtt_user);
        h += "'></label>"
             "<label>MQTT Password<div class='pwd-wrap'>"
             "<input type='password' name='mqtt_pass' autocomplete='off' placeholder='Leave empty to retain the saved password'>"
             "<button type='button' class='pwd-toggle' onclick='togglePwd(this)'>Show</button>"
             "</div></label>"
             "</div>"
             "<div class='card'><h2>Device</h2>"
             "<label>Name<input name='device_name' value='";
        h += htmlEscape(cfg.device_name);
        h += "' required></label>"
             "<label>Serial Number<input name='device_sn' value='";
        h += htmlEscape(cfg.device_sn);
        h += "'></label>"
             "<label>Area (Home Assistant)<input name='device_area' value='";
        h += htmlEscape(cfg.device_area);
        h += "' placeholder='e.g., Basement'></label>"
             "</div>"
             "<div class='card'><h2>Advanced</h2>"
             "<label>ESPOTADASH URL<input name='dashboard_url' value='";
        h += htmlEscape(cfg.dashboard_url);
        h += "' placeholder='<IP>:<PORT>'></label>"
             "<label>HA Discovery Prefix<input name='ha_prefix' value='";
        h += htmlEscape(cfg.ha_prefix);
        h += "'></label>"
             "<label>MQTT Buffer (byte)<input type='number' name='mqtt_buf' value='";
        h += String(cfg.mqtt_buf);
        h += "' min='256'></label>"
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
        // *** Capture ALL args immediately before any send/yield. ***
        // After _server.send() the connection is closed and _server.arg()
        // returns empty strings — reading args later was the root cause of
        // the "resets but goes back to provisioning" bug.
        String ssid          = _server.arg("wifi_ssid");
        String pass          = _server.arg("wifi_pass");
        String mqtt_server   = _server.arg("mqtt_server");
        String mqtt_port_s   = _server.arg("mqtt_port");
        String mqtt_buf_s    = _server.arg("mqtt_buf");
        String mqtt_user     = _server.arg("mqtt_user");
        String mqtt_pass_s   = _server.arg("mqtt_pass");
        String dashboard_url = _server.arg("dashboard_url");
        String device_name   = _server.arg("device_name");
        String device_sn     = _server.arg("device_sn");
        String device_area   = _server.arg("device_area");
        String ha_prefix     = _server.arg("ha_prefix");

        if (ssid.length() == 0) {
            _server.send(400, "text/html",
                "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
                "<h2>Error</h2><p>Select a WiFi network.</p>"
                "<a href='/'>Back</a></body></html>");
            return;
        }

        // Build cfg from existing saved values, then overlay the form submission.
        // Password fields left blank keep their stored value.
        ConfigData cfg;
        cfg.load(_ps);

        cfg.wifi_ssid     = ssid;
        if (pass.length() > 0)         cfg.wifi_pass   = pass;
        if (mqtt_server.length() > 0)  cfg.mqtt_server = mqtt_server;
        int port = mqtt_port_s.toInt();
        if (port > 0 && port <= 65535) cfg.mqtt_port   = (int32_t)port;
        int buf = mqtt_buf_s.toInt();
        if (buf >= 256)                cfg.mqtt_buf    = (int32_t)buf;
        cfg.mqtt_user     = mqtt_user;
        if (mqtt_pass_s.length() > 0)  cfg.mqtt_pass   = mqtt_pass_s;
        cfg.dashboard_url = dashboard_url;
        if (device_name.length() > 0)  cfg.device_name = device_name;
        cfg.device_sn     = device_sn;
        cfg.device_area   = device_area;
        if (ha_prefix.length() > 0)    cfg.ha_prefix   = ha_prefix;

        if (cfg.mqtt_port <= 0)            cfg.mqtt_port   = 1883;
        if (cfg.mqtt_buf < 256)            cfg.mqtt_buf    = 2048;
        if (cfg.ha_prefix.length() == 0)   cfg.ha_prefix   = "homeassistant";
        if (cfg.device_name.length() == 0) cfg.device_name = "EnergyMeter";

        // Send "testing" page — after this the connection is closed.
        _server.sendHeader("Connection", "close");
        _server.send(200, "text/html",
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta http-equiv='refresh' content='40;url=/'>"
            "</head><body>"
            "<h2>Testing connection...</h2>"
            "<p>Testing connection to <b>" + htmlEscape(ssid) + "</b>.<br>"
            "The connection to the device will be temporarily interrupted.<br>"
            "Please wait up to 35 seconds. If the page does not reload, please reconnect to <b>EARU_CBU_EAKCB-M-T</b> and try again.</p>"
            "</body></html>");

        delay(200);

        Serial.println("[PROV] Testing WiFi: " + ssid);
        bool ok = testWiFi(ssid, cfg.wifi_pass);

        if (!ok) {
            Serial.println("[PROV] WiFi test failed, restarting AP");
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAPConfig(IPAddress(10, 0, 0, 1), IPAddress(10, 0, 0, 1), IPAddress(255, 255, 255, 0));
            WiFi.softAP("EARU_CBU_EAKCB-M-T");
            return;
        }

        Serial.println("[PROV] WiFi OK, saving config");
        cfg.save(_ps);
        delay(1000);
        ESP.restart();
    }

    void handleOTAGet() {
        String h;
        h.reserve(1024);
        h  = "<!DOCTYPE html><html lang='it'><head>"
             "<meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>OTA Update</title><style>";
        h += CSS();
        h += "</style></head><body>"
             "<h1>EARU &mdash; Firmware Update</h1>"
             "<form method='POST' action='/update' enctype='multipart/form-data'>"
             "<div class='card'><h2>Firmware</h2>"
             "<label>File .bin<input type='file' name='firmware' accept='.bin' required></label></div>"
             "<button type='submit'>Update</button>"
             "</form></body></html>";
        _server.send(200, "text/html", h);
    }

    void handleOTAPost() {
        bool ok = !Update.hasError();
        _server.sendHeader("Connection", "close");
        _server.send(200, "text/html",
            ok ? "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
                 "<h2>Update completed</h2><p>Restarting...</p></body></html>"
               : "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
                 "<h2>Error updating</h2><a href='/update'>Try again</a></body></html>");
        if (ok) {
            delay(500);
            ESP.restart();
        }
    }

    void handleOTAUpload() {
        HTTPUpload &upload = _server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[OTA] Start: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true))
                Serial.printf("[OTA] Success: %u bytes\n", upload.totalSize);
            else
                Update.printError(Serial);
        }
    }

    // BK7231N is a single-radio chip: AP and STA share the same radio and must be
    // on the same channel.  If the router is on a different channel than the AP the
    // connection never completes.  Stopping the AP first removes the channel conflict.
    bool testWiFi(const String &ssid, const String &pass) {
        WiFi.softAPdisconnect(false);
        WiFi.mode(WIFI_STA);
        delay(200);
        WiFi.begin(ssid.c_str(), pass.c_str());
        for (int i = 0; i < 350; i++) {   // 350 × 100 ms = 35 s max
            if (WiFi.status() == WL_CONNECTED) return true;
            delay(100);
        }
        WiFi.disconnect(false);
        return false;
    }

public:
    explicit ProvisioningServer(PrefsManager &ps) : _ps(ps), _server(80) {}

    void begin() {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(IPAddress(10, 0, 0, 1), IPAddress(10, 0, 0, 1), IPAddress(255, 255, 255, 0));
        WiFi.softAP("EARU_CBU_EAKCB-M-T");
        Serial.println("[PROV] AP started: EARU_CBU_EAKCB-M-T  IP: 10.0.0.1");

        _server.on("/", HTTP_GET,  [this]() { handleRoot(); });
        _server.on("/save", HTTP_POST, [this]() { handleSave(); });
        _server.on("/update", HTTP_GET,  [this]() { handleOTAGet(); });
        _server.on("/update", HTTP_POST, [this]() { handleOTAPost(); },
                                         [this]() { handleOTAUpload(); });
        _server.onNotFound([this]() {
            _server.sendHeader("Location", "/");
            _server.send(302);
        });
        _server.begin();
        Serial.println("[PROV] Web server on port 80 (OTA at /update)");
    }

    void loop() { _server.handleClient(); }
};
