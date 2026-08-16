#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <Update.h>
#include "config.h"

// Minimal DNS server: answers every A query with the AP address, so any name
// the phone looks up resolves to the portal.  LibreTiny ships no DNSServer
// library, and this is all a captive portal needs.
class CaptiveDNS {
    WiFiUDP   _udp;
    IPAddress _ip;
    bool      _running = false;
    uint8_t   _buf[512];

  public:
    bool begin(IPAddress ip) {
        _ip      = ip;
        _running = _udp.begin(53) == 1;
        return _running;
    }

    void stop() {
        if (_running) {
            _udp.stop();
            _running = false;
        }
    }

    void loop() {
        if (!_running) return;
        int avail = _udp.parsePacket();
        if (avail <= 0) return;
        int n = _udp.read(_buf, sizeof(_buf));
        if (n < 12) return;
        if (_buf[2] & 0x80) return;                       // already a response
        if ((_buf[4] << 8 | _buf[5]) == 0) return;        // no question

        // Walk past the QNAME labels of the first question.
        int p = 12;
        while (p < n && _buf[p] != 0) {
            p += _buf[p] + 1;
            if (p >= n) return;                           // malformed
        }
        p += 1;                                           // terminating zero
        if (p + 4 > n) return;
        uint16_t qtype = (_buf[p] << 8) | _buf[p + 1];
        p += 4;                                           // QTYPE + QCLASS

        _buf[2] = 0x81;                                   // response
        _buf[3] = 0x80;                                   // recursion available
        _buf[8] = _buf[9] = _buf[10] = _buf[11] = 0;      // no NS, no AR

        // Only A records are answered; anything else (AAAA in particular) gets
        // an empty NOERROR so the client falls back to IPv4 instead of hanging.
        if (qtype != 1 || p + 16 > (int)sizeof(_buf)) {
            _buf[6] = _buf[7] = 0;                        // ANCOUNT = 0
            _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
            _udp.write(_buf, p);
            _udp.endPacket();
            return;
        }

        _buf[6] = 0; _buf[7] = 1;                         // ANCOUNT = 1
        uint8_t *a = _buf + p;
        *a++ = 0xC0; *a++ = 0x0C;                         // pointer to QNAME
        *a++ = 0x00; *a++ = 0x01;                         // TYPE A
        *a++ = 0x00; *a++ = 0x01;                         // CLASS IN
        *a++ = 0x00; *a++ = 0x00; *a++ = 0x00; *a++ = 0x3C;   // TTL 60 s
        *a++ = 0x00; *a++ = 0x04;                         // RDLENGTH
        *a++ = _ip[0]; *a++ = _ip[1]; *a++ = _ip[2]; *a++ = _ip[3];

        _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
        _udp.write(_buf, p + 16);
        _udp.endPacket();
    }
};

// Captive-portal provisioning server.
// Starts an open AP "EARU_CBU_EAKCB-M-T" at 10.0.0.1 and serves a web form
// on port 80 where the user selects a WiFi network and fills in all config
// fields. On save, the WiFi connection is tested; on success the config is
// written to PrefsManager and the device restarts.
class ProvisioningServer {
    PrefsManager &_ps;
    WebServer     _server;
    CaptiveDNS    _dns;

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

    // Mobile-first stylesheet.  Two rules are load-bearing on iOS and must not
    // be "tidied": every input/select stays at 16px (Safari zooms the whole
    // page when a focused field is smaller), and every control keeps a 44px
    // minimum height, the Apple HIG touch target.
    static const char *CSS() {
        return
            "*{box-sizing:border-box}"
            ":root{color-scheme:light dark}"
            "html{-webkit-text-size-adjust:100%}"
            "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;"
            "font-size:16px;line-height:1.4;margin:0 auto;max-width:560px;"
            "padding:20px 16px calc(28px + env(safe-area-inset-bottom));"
            "background:#f0f2f5;color:#1c1e21;-webkit-font-smoothing:antialiased}"
            "h1{font-size:20px;font-weight:700;color:#1565c0;margin:0 0 4px}"
            ".card{background:#fff;border-radius:12px;padding:16px;margin:14px 0;box-shadow:0 1px 2px rgba(0,0,0,.08)}"
            "h2{font-size:12px;font-weight:700;color:#606770;margin:0 0 4px;padding-bottom:8px;"
            "border-bottom:1px solid #e4e6eb;text-transform:uppercase;letter-spacing:.06em}"
            "label{display:block;margin-top:14px;font-size:13px;font-weight:600;color:#606770}"
            // 16px prevents the iOS focus zoom; 44px is the minimum touch target.
            "input,select{display:block;width:100%;font-family:inherit;font-size:16px;min-height:44px;"
            "padding:10px 12px;margin-top:6px;border:1px solid #ccd0d5;border-radius:8px;"
            "background:#fff;color:inherit;-webkit-appearance:none;appearance:none}"
            "select{padding-right:40px;background-repeat:no-repeat;background-position:right 14px center;"
            "background-image:url(\"data:image/svg+xml;charset=utf-8,%3Csvg%20xmlns='http://www.w3.org/2000/svg'"
            "%20width='12'%20height='8'%3E%3Cpath%20fill='%23606770'%20d='M6%208L0%200h12z'/%3E%3C/svg%3E\")}"
            "input:focus,select:focus{outline:none;border-color:#1565c0;box-shadow:0 0 0 3px rgba(21,101,192,.15)}"
            "button{font-family:inherit;-webkit-appearance:none;appearance:none;cursor:pointer}"
            "button[type=submit]{display:block;width:100%;min-height:50px;margin-top:22px;padding:14px;"
            "font-size:17px;font-weight:600;background:#1565c0;color:#fff;border:none;border-radius:10px}"
            "button[type=submit]:active{background:#0d47a1}"
            ".hint{font-size:13px;color:#8a8d91;margin:0 0 4px}"
            ".pwd-wrap{display:flex;gap:8px;align-items:stretch;margin-top:6px}"
            ".pwd-wrap input{flex:1;margin-top:0}"
            ".pwd-toggle{flex:0 0 auto;min-width:68px;min-height:44px;padding:0 12px;font-size:14px;font-weight:600;"
            "background:#e8eaf6;color:#1565c0;border:1px solid #c5cae9;border-radius:8px;white-space:nowrap}"
            ".pwd-toggle:active{background:#c5cae9}"
            // Dark mode: iOS honours this system-wide, and a white page at
            // night in a dark utility room is unpleasant to use.
            "@media(prefers-color-scheme:dark){"
            "body{background:#18191a;color:#e4e6eb}"
            ".card{background:#242526;box-shadow:none}"
            "h1{color:#8ab4f8}"
            "h2{color:#b0b3b8;border-bottom-color:#3a3b3c}"
            "label{color:#b0b3b8}"
            "input,select{background:#3a3b3c;border-color:#4e4f50;color:#e4e6eb}"
            "select{background-image:url(\"data:image/svg+xml;charset=utf-8,%3Csvg%20xmlns='http://www.w3.org/2000/svg'"
            "%20width='12'%20height='8'%3E%3Cpath%20fill='%23b0b3b8'%20d='M6%208L0%200h12z'/%3E%3C/svg%3E\")}"
            ".pwd-toggle{background:#3a3b3c;border-color:#4e4f50;color:#8ab4f8}"
            ".hint{color:#8a8d91}"
            "}";
    }

    static const char *authName(WiFiAuthMode m) {
        switch (m) {
            case WIFI_AUTH_OPEN:            return "OPEN";
            case WIFI_AUTH_WEP:             return "WEP";
            case WIFI_AUTH_WPA_PSK:         return "WPA";
            case WIFI_AUTH_WPA2_PSK:        return "WPA2";
            case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
            case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENTERPRISE (unsupported)";
            case WIFI_AUTH_WPA3_PSK:        return "WPA3 (unsupported)";
            case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3 mixed (may fail)";
            default:                        return "OTHER/UNKNOWN";
        }
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
            // The auth mode matters: this chip does WPA/WPA2 only, so a WPA3 or
            // WPA2/WPA3-mixed AP is a likely cause of a connection that never
            // completes despite a correct password.
            Serial.printf("[PROV] scan: \"%s\" ch=%ld %d dBm auth=%s\r\n",
                          ssid.c_str(), (long)WiFi.channel(i), (int)WiFi.RSSI(i),
                          authName(WiFi.encryptionType(i)));
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
        h.reserve(8192);   // CSS alone is ~2.5 kB; avoids repeated reallocation
        h  = "<!DOCTYPE html><html lang='it'><head>"
             "<meta charset='utf-8'>"
             // viewport-fit=cover lets the safe-area padding work on notched iPhones.
             "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
             "<meta name='theme-color' content='#1565c0'>"
             "<meta name='format-detection' content='telephone=no'>"
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
             "<input type='password' name='wifi_pass' autocomplete='off' "
             "autocapitalize='off' autocorrect='off' spellcheck='false' placeholder='";
        h += wifiPassPlaceholder;
        h += "'>"
             "<button type='button' class='pwd-toggle' onclick='togglePwd(this)'>Show</button>"
             "</div></label></div>"
             "<div class='card'><h2>MQTT</h2>"
             // inputmode='url', not 'decimal': the decimal keypad shows the
             // locale's separator, which on an Italian iPhone is a comma, so
             // the dots of an IP address cannot be typed at all.  The URL
             // keyboard puts a real '.' on the main row.
             "<label>Server<input name='mqtt_server' inputmode='url' autocapitalize='off' "
             "autocorrect='off' spellcheck='false' value='";
        h += htmlEscape(cfg.mqtt_server);
        h += "' placeholder='<IP>' required></label>"
             "<label>Port<input type='number' inputmode='numeric' name='mqtt_port' value='";
        h += String(cfg.mqtt_port);
        h += "' min='1' max='65535'></label>"
             "<label>MQTT User<input name='mqtt_user' autocapitalize='off' autocorrect='off' "
             "spellcheck='false' value='";
        h += htmlEscape(cfg.mqtt_user);
        h += "'></label>"
             "<label>MQTT Password<div class='pwd-wrap'>"
             "<input type='password' name='mqtt_pass' autocomplete='off' autocapitalize='off' "
             "autocorrect='off' spellcheck='false' placeholder='Leave empty to retain the saved password'>"
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
             "<label>ESPOTADASH URL<input name='dashboard_url' inputmode='url' autocapitalize='off' "
             "autocorrect='off' spellcheck='false' value='";
        h += htmlEscape(cfg.dashboard_url);
        h += "' placeholder='<IP>:<PORT>'></label>"
             "<label>HA Discovery Prefix<input name='ha_prefix' autocapitalize='off' autocorrect='off' "
             "spellcheck='false' value='";
        h += htmlEscape(cfg.ha_prefix);
        h += "'></label>"
             "<label>MQTT Buffer (byte)<input type='number' inputmode='numeric' name='mqtt_buf' value='";
        h += String(cfg.mqtt_buf);
        h += "' min='256'></label>"
             "</div>"
             "<button type='submit'>Save and Restart</button>"
             "</form>"
             "<script>function togglePwd(b){var f=b.previousElementSibling;"
             "f.type=f.type==='password'?'text':'password';"
             "b.textContent=f.type==='password'?'Show':'Hide';}</script>"
             "</body></html>";

        // The OS captive probes hit this same handler.  Without no-store iOS
        // can reuse a cached answer on the next join and never re-run its
        // check, so the assistant does not pop up a second time.
        _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        _server.sendHeader("Pragma", "no-cache");
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

        // Safety net for address fields: a comma is never valid in an IP or in
        // host:port, and a localised keyboard (or a paste) can still slip one
        // in.  Treat it as the dot the user meant.
        mqtt_server.replace(',', '.');
        dashboard_url.replace(',', '.');

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
    static const char *wlStatusName(int s) {
        switch (s) {
            case WL_IDLE_STATUS:     return "IDLE";
            case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (AP not seen)";
            case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
            case WL_CONNECTED:       return "CONNECTED";
            case WL_CONNECT_FAILED:  return "CONNECT_FAILED (auth rejected)";
            case WL_CONNECTION_LOST: return "CONNECTION_LOST";
            case WL_DISCONNECTED:    return "DISCONNECTED";
            default:                 return "UNKNOWN";
        }
    }

    bool testWiFi(const String &ssid, const String &pass) {
        // A plain WiFi.begin() from a fresh boot connects to this network, so
        // the difference here is the radio state: we arrive with a SoftAP that
        // has been running and a scan already performed.  Tear the radio all
        // the way down (AP off, STA off, WIFI_OFF) before bringing STA back up,
        // to approximate the clean post-boot state the Beken SDK expects.
        WiFi.softAPdisconnect(true);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(1000);
        WiFi.mode(WIFI_STA);
        delay(500);
        // Log lengths, never the password itself: a trailing space or a
        // mangled character from the form shows up as an unexpected length.
        Serial.printf("[PROV] SSID \"%s\" (%u chars), password %u chars\r\n",
                      ssid.c_str(), ssid.length(), pass.length());
        // Three attempts of 12 s each.  A single begin() left to run for 35 s
        // does not recover if the first association attempt is lost, which is
        // common on a weak link; a fresh begin() restarts the handshake.
        for (int attempt = 1; attempt <= 3; attempt++) {
            WiFi.begin(ssid.c_str(), pass.c_str());
            Serial.printf("[PROV] Attempt %d/3\r\n", attempt);

            int last = -1;
            for (int i = 0; i < 120; i++) {   // 120 × 100 ms = 12 s
                int st = WiFi.status();
                if (st == WL_CONNECTED) {
                    Serial.printf("[PROV] Connected on attempt %d, RSSI %ld dBm\r\n",
                                  attempt, (long)WiFi.RSSI());
                    return true;
                }
                if (st != last || i % 50 == 0) {
                    Serial.printf("[PROV] ...status=%d %s\r\n", st, wlStatusName(st));
                    last = st;
                }
                delay(100);
            }
            Serial.printf("[PROV] Attempt %d failed (status=%d %s)\r\n",
                          attempt, WiFi.status(), wlStatusName(WiFi.status()));
            WiFi.disconnect(false);
            delay(500);
        }
        return false;
    }

public:
    explicit ProvisioningServer(PrefsManager &ps) : _ps(ps), _server(80) {}

    void begin() {
        // AP only: on this single-radio chip a leftover STA association attempt
        // (credentials still in the Beken NVS) makes the radio hop channels, so
        // clients see the beacon but fail to associate.  scanNetworks() calls
        // enableSTA() by itself, so the network list in the form still works.
        WiFi.mode(WIFI_AP);
        WiFi.disconnect(false);
        WiFi.softAPConfig(IPAddress(10, 0, 0, 1), IPAddress(10, 0, 0, 1), IPAddress(255, 255, 255, 0));
        bool apOk = WiFi.softAP("EARU_CBU_EAKCB-M-T");
        Serial.printf("[PROV] AP start %s: EARU_CBU_EAKCB-M-T  IP: 10.0.0.1\r\n",
                      apOk ? "OK" : "FAILED");

        Serial.printf("[PROV] Captive DNS %s\r\n",
                      _dns.begin(IPAddress(10, 0, 0, 1)) ? "started" : "FAILED");

        _server.on("/", HTTP_GET,  [this]() { handleRoot(); });
        _server.on("/save", HTTP_POST, [this]() { handleSave(); });
        _server.on("/update", HTTP_GET,  [this]() { handleOTAGet(); });
        _server.on("/update", HTTP_POST, [this]() { handleOTAPost(); },
                                         [this]() { handleOTAUpload(); });

        // Captive-portal detection endpoints.  Each OS probes a well-known URL
        // and decides it is behind a portal when the answer is not what it
        // expects.  Serving the form itself (rather than a redirect) is what
        // makes iOS open its Captive Network Assistant straight on the page.
        _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });  // iOS / macOS
        _server.on("/library/test/success.html", HTTP_GET, [this]() { handleRoot(); });
        _server.on("/generate_204", HTTP_GET, [this]() { handleRoot(); });         // Android
        _server.on("/gen_204", HTTP_GET, [this]() { handleRoot(); });
        _server.on("/ncsi.txt", HTTP_GET, [this]() { handleRoot(); });             // Windows
        _server.on("/connecttest.txt", HTTP_GET, [this]() { handleRoot(); });
        _server.on("/canonical.html", HTTP_GET, [this]() { handleRoot(); });       // Firefox

        // Anything else: absolute redirect to the portal.  The Location must be
        // absolute, since the phone asked a different host entirely.
        _server.onNotFound([this]() {
            _server.sendHeader("Location", "http://10.0.0.1/", true);
            _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            _server.send(302, "text/plain", "");
        });
        _server.begin();
        Serial.println("[PROV] Web server on port 80 (OTA at /update)");
    }

    void loop() {
        _dns.loop();
        _server.handleClient();
    }
};
