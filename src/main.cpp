#include "main.h"

/*
State	                            LED
Provisioning AP active              blinks every 1000 ms
WiFi Connection in Progress	        blinks every 500 ms
WiFi Connected, MQTT in Progress	blinks every 250 ms
WiFi + MQTT Connected	            solid on
WiFi Lost (detected by check)	    off
MQTT Reconnected (by check)	        solid on
*/

/*
HARDWARE DESCRIPTION:
https://www.elektroda.com/news/news4145265.html
https://www.elektroda.com/news/news3934580.html
*/

ConfigData    config;
ESPOTADASH    dash(80);
PrefsManager  ps;
ProvisioningServer prov(ps);
ConfigPortal  portal(config, ps);

WiFiClient    wifiClient;
PubSubClient  mqttClient(wifiClient);

bl0942::BL0942 energyMeter(Serial1);

NTC temp_sensor;

Device haDevice;
Switch relaySwitch;
Sensor sensorVoltage;
Sensor sensorCurrent;
Sensor sensorPower;
Sensor sensorEnergy;
Sensor sensorFrequency;
Sensor sensorTemperature;

String ha_status_topic;
unsigned long lastPublish = 0;
unsigned long lastEnergyPublish = 0;
unsigned long lastEnergySave = 0;
float energyOffset = 0.0f;
float lastRawEnergy = 0.0f;
float temperature = 0.0f;

// Tracks the logical relay state, initialised from saved state after relaySwitch.init().
bool relayState = false;

// Pending relay command from MQTT callback (deferred to avoid calling publish inside callback).
bool pendingRelay      = false;
bool pendingRelayState = false;

// millis() timestamp until which the identify blink is active (0 = inactive).
unsigned long identifyUntil = 0;

void checkWiFiConnection();
void checkMQTTConnection();
void mqttSubscribe();
void mqttHello();
void WiFiconnect();
void mqttConnect();
void setRelay(bool active);
void driveRelay(bool active);
void handleButton();
void handleIdentify();

// =======================================================================================================================
// SETUP
// =======================================================================================================================

void setup()
{
    // SERIAL SETUP
    Serial.begin(115200);

    // LOAD RUNTIME CONFIG — must happen before any code that uses config fields
    config.load(ps);

    // INIT BL0942 ENERGY METER
    Serial1.begin(4800);
    Serial1.setTimeout(200);
    bl0942::ModeConfig cfg_bl;
    cfg_bl.ac_freq = bl0942::LINE_FREQUENCY_50HZ;
    energyMeter.setup(cfg_bl);
    ps.read("cbu/energy", energyOffset, 0.0f);
    energyMeter.onDataReceived([](bl0942::SensorData &data)
                               {
        sensorVoltage.value   = String(data.voltage,              2);
        sensorCurrent.value   = String(data.current,              3);
        sensorPower.value     = String(max(0.0f, data.watt),      2);
        lastRawEnergy = data.energy;
        sensorEnergy.value    = String(energyOffset + data.energy, 4);
        sensorFrequency.value = String(data.frequency,            2); });

    // INIT ADC
    temp_sensor.init(ADC_PIN, config.ntc_rpull, (int)config.ntc_beta,
                     config.ntc_tref, config.ntc_rref, config.ntc_vref);

    // PIN SETUP
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_BLU, OUTPUT);
    pinMode(BUTTON, INPUT_PULLUP);
    pinMode(BRIDGE_FWD, OUTPUT);
    pinMode(BRIDGE_REV, OUTPUT);
    digitalWrite(LED_RED, LED_OFF);
    digitalWrite(LED_BLU, LED_OFF);
    digitalWrite(BRIDGE_FWD, LOW);
    digitalWrite(BRIDGE_REV, LOW);

    // PROVISIONING MODE — entered on first boot or after factory reset
    if (!config.isProvisioned()) {
        Serial.println("[MAIN] No config found — entering provisioning mode");
        prov.begin();

        unsigned long lastBlink    = 0;
        bool          blinkState   = false;
        bool          provRelay    = false;
        bool          lastBtnProv  = HIGH;
        unsigned long presStart    = 0;

        while (true) {
            unsigned long now = millis();
            if (now - lastBlink >= 1000) {
                lastBlink  = now;
                blinkState = !blinkState;
                digitalWrite(LED_BLU, blinkState ? LED_ON : LED_OFF);
            }

            bool btn = digitalRead(BUTTON);
            if (btn == LOW && lastBtnProv == HIGH)
                presStart = now;
            if (btn == HIGH && lastBtnProv == LOW && presStart > 0) {
                unsigned long held = now - presStart;
                if (held >= 15000) {
                    Serial.println("[BTN] Factory reset");
                    ps.eraseAll();
                    delay(200);
                    ESP.restart();
                } else if (held >= 5000) {
                    Serial.println("[BTN] Restart");
                    delay(100);
                    ESP.restart();
                } else if (held >= 50) {
                    provRelay = !provRelay;
                    driveRelay(provRelay);
                    digitalWrite(LED_RED, provRelay ? LED_ON : LED_OFF);
                }
                presStart = 0;
            }
            lastBtnProv = btn;

            prov.loop();
            yield();
        }
        // Never reached — prov calls ESP.restart() on success.
    }

    // NORMAL OPERATION -------------------------------------------------------

    // Seed RNG: MAC address (device-unique) XORed with ADC noise (thermal entropy)
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        uint32_t seed = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16)
                      | ((uint32_t)mac[4] <<  8) |  (uint32_t)mac[5];
        for (int i = 0; i < 8; i++)
            seed = (seed << 1) ^ (uint32_t)analogRead(ADC_PIN);
        randomSeed(seed);
    }

    // INIT HOME ASSISTANT DEVICE AND ENTITIES
    haDevice.init(mqttClient,
                  config.ha_prefix.c_str(),
                  DEVICE_VERSION,
                  DEVICE_MANUFACTURER,
                  DEVICE_MODEL,
                  config.device_name.c_str(),
                  config.device_sn.c_str(),
                  config.device_area.c_str(),
                  FIRMWARE_VERSION);

    relaySwitch.init(haDevice, "Relay", LED_RED);
    sensorVoltage.init(haDevice,   "Voltage",   "V",   "voltage",   "measurement",       2);
    sensorCurrent.init(haDevice,   "Current",   "A",   "current",   "measurement",       3);
    sensorPower.init(haDevice,     "Power",     "W",   "power",     "measurement",       2);
    sensorEnergy.init(haDevice,    "Energy",    "kWh", "energy",    "total_increasing",  3);
    sensorFrequency.init(haDevice, "Frequency", "Hz",  "frequency", "measurement",       2);
    sensorTemperature.init(haDevice, "Temperature", "\xC2\xB0""C", "temperature", "measurement", 2);
    ha_status_topic = haDevice.prefix + "/status";

    // Restore relay logical state from what was saved to PrefsManager / LittleFS.
    // The relay holds its mechanical position over power cycles, so no pulse is issued here.
    relayState = relaySwitch.getValue();

    // WIFI SETUP
    WiFiconnect();

    // MQTT SETUP
    mqttConnect();

    // CONFIG PORTAL on port 8080
    portal.begin();

    // SETS ESPOTADASH COMMANDS AND STARTS IT
    dash.addCommand("reboot", "Sofware reset", []()
                    {
                        delay(100);
                        ESP.restart(); });

    dash.addCommand("test-relay-on", "Force relay ON pulse (bypasses state guard)", []()
                    { driveRelay(true); });

    dash.addCommand("test-relay-off", "Force relay OFF pulse (bypasses state guard)", []()
                    { driveRelay(false); });

    dash.addCommand("reset-relay-state", "Reset saved relay state to OFF", []()
                    {
                        relayState = false;
                        relaySwitch.set("OFF");
                        relaySwitch.publish_state(); });

    dash.addCommand("identify", "Blink blue LED fast for 30 s to identify the device", []()
                    { identifyUntil = millis() + 30000UL; });

    dash.addCommand("reset-energy-counter", "Reset energy counter to zero", []()
                    {
                        energyOffset = -lastRawEnergy;
                        ps.write("cbu/energy", 0.0f);
                        sensorEnergy.value = "0.0000";
                        sensorEnergy.publish_state(); });

    dash.begin(config.dashboard_url.c_str(), haDevice.name, "", FIRMWARE_VERSION);
}

// =======================================================================================================================
// LOOP
// =======================================================================================================================

void loop()
{
    if (pendingRelay)
    {
        pendingRelay = false;
        setRelay(pendingRelayState);
    }

    dash.loop();
    portal.loop();
    handleIdentify();
    mqttClient.loop();
    checkWiFiConnection();
    checkMQTTConnection();

    unsigned long now = millis();
    if (now - lastPublish >= 30000)
    {
        lastPublish = now;
        haDevice.online();
        relaySwitch.publish_state();
    }

    handleButton();

    if (now - lastEnergyPublish >= (unsigned long)config.energy_interval)
    {
        lastEnergyPublish = now;
        sensorVoltage.publish_state();
        sensorCurrent.publish_state();
        sensorPower.publish_state();
        sensorEnergy.publish_state();
        sensorFrequency.publish_state();
        sensorTemperature.publish_state();
    }

    if (now - lastEnergySave >= 3600000UL)
    {
        lastEnergySave = now;
        ps.write("cbu/energy", energyOffset + lastRawEnergy);
    }

    static unsigned long lastUpdate = 0;
    if (now - lastUpdate >= 1000)
    {
        lastUpdate = now;
        energyMeter.update();
        sensorTemperature.value = String(temp_sensor.getTemp(), 2);
    }
    energyMeter.loop();
}

// =======================================================================================================================
// MQTT CALLBACK
// =======================================================================================================================

void callback(char *topic, byte *payload, unsigned int length)
{
    String t = String(topic);
    String p;
    for (unsigned int i = 0; i < length; i++)
        p += (char)payload[i];

    Serial.println("MQTT <- " + t + " : " + p);

    if (t == ha_status_topic && p == "online")
    {
        delay(1000);
        mqttHello();
        return;
    }
    if (t == relaySwitch.command_topic)
    {
        pendingRelay      = true;
        pendingRelayState = (p == "ON");
    }
}

// ==================================================================================================================================
//                                                                    MQTT SUBSCRIBE
// ==================================================================================================================================

void mqttSubscribe()
{
    mqttClient.subscribe(String(haDevice.prefix + "/status").c_str());
    mqttClient.subscribe(ha_status_topic.c_str());
    mqttClient.subscribe(relaySwitch.command_topic.c_str());
}

// ==================================================================================================================================
//                                                                    MQTT HELLO
// ==================================================================================================================================

void mqttHello()
{
    relaySwitch.discovery();
    sensorVoltage.discovery();
    sensorCurrent.discovery();
    sensorPower.discovery();
    sensorEnergy.discovery();
    sensorFrequency.discovery();
    sensorTemperature.discovery();
    haDevice.online();
}

// ==================================================================================================================================
//                                                                    WIFI CHECK
// ==================================================================================================================================

void checkWiFiConnection()
{
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    if (WiFi.status() != WL_CONNECTED && millis() >= identifyUntil)
        digitalWrite(LED_BLU, LED_OFF);

    if (now - lastCheck < 60000)
        return;
    lastCheck = now;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Disconnected, reconnecting...");
        digitalWrite(LED_BLU, LED_OFF);
        WiFi.reconnect();
    }
}

// ==================================================================================================================================
//                                                                    MQTT CHECK
// ==================================================================================================================================

void checkMQTTConnection()
{
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    if (!mqttClient.connected() && millis() >= identifyUntil)
        digitalWrite(LED_BLU, LED_OFF);

    if (now - lastCheck < 60000)
        return;
    lastCheck = now;

    if (WiFi.status() != WL_CONNECTED)
        return;

    if (!mqttClient.connected())
    {
        Serial.println("[MQTT] Disconnected, reconnecting...");
        if (mqttClient.connect(haDevice.name.c_str(),
                               config.mqtt_user.c_str(),
                               config.mqtt_pass.c_str(),
                               haDevice.availability_topic.c_str(), 2, true, "offline", true))
        {
            Serial.println("[MQTT] Reconnected");
            digitalWrite(LED_BLU, LED_ON);
            mqttSubscribe();
            mqttHello();
        }
        else
        {
            Serial.println("[MQTT] Reconnect failed, state: " + String(mqttClient.state()));
        }
    }
}

// ==================================================================================================================================
//                                                                    WIFI CONNECT
// ==================================================================================================================================

void WiFiconnect()
{
    int attempts = 0;
    bool bluState = LED_OFF;

    Serial.println("[WiFi] Connecting...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.hostname((config.device_name + "_" + haDevice.uuid + ".local").c_str());
    WiFi.begin(config.wifi_ssid.c_str(), config.wifi_pass.c_str());
    while (WiFi.status() != WL_CONNECTED && attempts < 120)
    {
        delay(500);
        bluState = (bluState == LED_ON) ? LED_OFF : LED_ON;
        digitalWrite(LED_BLU, bluState);
        attempts++;
        Serial.println("[WiFi] Attempt " + String(attempts) + "/120...");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        digitalWrite(LED_BLU, LED_ON);
        IPAddress ip = WiFi.localIP();
        Serial.println("[WiFi] Connected. IP: " + String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]) + ", RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    else
    {
        Serial.println("[WiFi] Connection failed after " + String(attempts) + " attempts");
    }
}

// ==================================================================================================================================
//                                                                    MQTT CONNECT
// ==================================================================================================================================

void mqttConnect()
{
    int attempts = 0;
    bool bluState = LED_OFF;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[MQTT] Skipped: WiFi not connected");
        return;
    }

    Serial.println("[MQTT] Connecting to " + config.mqtt_server + ":" + String(config.mqtt_port) + "...");
    mqttClient.setBufferSize((uint16_t)config.mqtt_buf);
    mqttClient.setServer(config.mqtt_server.c_str(), (uint16_t)config.mqtt_port);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE);
    mqttClient.setCallback(callback);

    while (!mqttClient.connected() && attempts <= 120)
    {
        bluState = (bluState == LED_ON) ? LED_OFF : LED_ON;
        digitalWrite(LED_BLU, bluState);
        delay(250);
        Serial.println("[MQTT] Attempt " + String(attempts + 1) + "/120...");
        if (mqttClient.connect(haDevice.name.c_str(),
                               config.mqtt_user.c_str(),
                               config.mqtt_pass.c_str(),
                               haDevice.availability_topic.c_str(), 2, true, "offline", true))
        {
            Serial.println("[MQTT] Connected");
            digitalWrite(LED_BLU, LED_ON);
            mqttSubscribe();
            mqttHello();
        }
        else
        {
            Serial.println("[MQTT] Attempt failed, state: " + String(mqttClient.state()));
            attempts++;
        }
    }

    if (!mqttClient.connected())
        Serial.println("[MQTT] Connection failed after " + String(attempts) + " attempts");
}

// =======================================================================================================================
// IDENTIFY
// =======================================================================================================================

void handleIdentify()
{
    static unsigned long lastBlink    = 0;
    static bool          blinkState   = false;
    static bool          wasActive    = false;

    unsigned long now      = millis();
    bool          active   = now < identifyUntil;

    if (!active)
    {
        if (wasActive)
        {
            bool ok = (WiFi.status() == WL_CONNECTED) && mqttClient.connected();
            digitalWrite(LED_BLU, ok ? LED_ON : LED_OFF);
            wasActive = false;
        }
        return;
    }

    wasActive = true;
    if (now - lastBlink >= 100)
    {
        lastBlink  = now;
        blinkState = !blinkState;
        digitalWrite(LED_BLU, blinkState ? LED_ON : LED_OFF);
    }
}

// =======================================================================================================================
// BUTTON HANDLER
// =======================================================================================================================
//
//  Zone 0  (  0 –  5 s)  no feedback   → release: toggle relay
//  Zone 1  (  5 – 10 s)  slow blink    → release: restart device
//  Zone 2  ( 10 – 15 s)  fast blink    → release: reset WiFi config + restart (captive portal)
//  Zone 3  (≥ 15 s)      LED off       → release: erase all namespaces (factory reset) + restart
//                                         On next boot the device enters provisioning mode.
//
void handleButton()
{
    static bool          lastBtn    = HIGH;
    static unsigned long pressStart = 0;
    static unsigned long lastBlink  = 0;
    static bool          blinkState = false;

    bool btn = digitalRead(BUTTON);
    unsigned long now = millis();

    if (btn == LOW && lastBtn == HIGH)
    {
        pressStart = now;
        lastBlink  = now;
        blinkState = false;
    }

    if (btn == LOW && pressStart > 0)
    {
        unsigned long held = now - pressStart;

        if (held >= 15000)
        {
            digitalWrite(LED_BLU, LED_OFF);
        }
        else if (held >= 10000)
        {
            if (now - lastBlink >= 100)
            {
                lastBlink  = now;
                blinkState = !blinkState;
                digitalWrite(LED_BLU, blinkState ? LED_ON : LED_OFF);
            }
        }
        else if (held >= 5000)
        {
            if (now - lastBlink >= 500)
            {
                lastBlink  = now;
                blinkState = !blinkState;
                digitalWrite(LED_BLU, blinkState ? LED_ON : LED_OFF);
            }
        }
    }

    if (btn == HIGH && lastBtn == LOW && pressStart > 0)
    {
        unsigned long held = now - pressStart;

        if (held >= 15000)
        {
            Serial.println("[BTN] Factory reset — erasing all namespaces");
            ps.eraseAll();
            delay(200);
            ESP.restart();
        }
        else if (held >= 10000)
        {
            Serial.println("[BTN] WiFi reset — clearing credentials, entering captive portal on next boot");
            ps.write("cfg/wifi_ssid", String(""));
            ps.write("cfg/wifi_pass", String(""));
            delay(200);
            ESP.restart();
        }
        else if (held >= 5000)
        {
            Serial.println("[BTN] Restart");
            delay(100);
            ESP.restart();
        }
        else if (held >= 50)
        {
            setRelay(!relayState);
        }

        pressStart = 0;
        bool ok = (WiFi.status() == WL_CONNECTED) && mqttClient.connected();
        digitalWrite(LED_BLU, ok ? LED_ON : LED_OFF);
    }

    lastBtn = btn;
}

// =======================================================================================================================
// RELAY DRIVER - IC MD7620 (Shanghai-Mingda-Microelectronics)
// =======================================================================================================================

// Issues a 150 ms pulse on BRIDGE_FWD (active=true) or BRIDGE_REV (active=false).
// Never drives both pins simultaneously.
void driveRelay(bool active)
{
    if (active)
    {
        digitalWrite(BRIDGE_REV, LOW);
        digitalWrite(BRIDGE_FWD, HIGH);
        delay(50); // usally is 150 ms, but in same devices this pulse time can be reset the cbu. So I reduce it to 50 ms to avoid unexpected reset.
        digitalWrite(BRIDGE_FWD, LOW);
    }
    else
    {
        digitalWrite(BRIDGE_FWD, LOW);
        digitalWrite(BRIDGE_REV, HIGH);
        delay(50); // usally is 150 ms, but in same devices this pulse time can be reset the cbu. So I reduce it to 50 ms to avoid unexpected reset.
        digitalWrite(BRIDGE_REV, LOW);
    }
}

// Sets relay to the requested state. No-op if already in that state.
// Drives the bridge, updates LED_RED via the Switch entity, and publishes MQTT state.
void setRelay(bool active)
{
    if (active == relayState)
        return;
    relayState = active;
    driveRelay(active);
    relaySwitch.set(active ? "ON" : "OFF");
    relaySwitch.publish_state();
}
