#include <Arduino.h>
#include <WiFi.h>

#include <PrefsManager.h>
#include <ESPOTADASH.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HASTR.h>
#include <BL0942.h>
#include <ntc.h>

#include "config.h"
#include "provisioning.h"
#include "config_portal.h"
#include "settings.h"
#include "secrets.h"

// =======================================================================================================================
// PINS DEFINITION
// =======================================================================================================================

const int LED_RED   = PIN_P9;
const int LED_BLU   = PIN_P15;
const int BUTTON    = PIN_P17;
const int BRIDGE_FWD = PIN_P24;
const int BRIDGE_REV = PIN_P26;
const int ADC_PIN   = PIN_P23;

// =======================================================================================================================
// PINS LOGIC
// LOW = on, HIGH = off
// =======================================================================================================================

// LED active-LOW: LOW = acceso, HIGH = spento
#define LED_ON  LOW
#define LED_OFF HIGH
