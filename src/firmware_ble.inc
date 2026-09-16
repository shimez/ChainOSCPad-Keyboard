#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// ============================================================
// ChainOSCPad BLE board configuration
// ============================================================
#include "board_ble.h"

static constexpr uint8_t ROWS = 4;
static constexpr uint8_t COLS = 3;
static constexpr uint8_t KEY_COUNT = ROWS * COLS;

static constexpr uint8_t rowPins[ROWS] = {BOARD_ROW0, BOARD_ROW1, BOARD_ROW2, BOARD_ROW3};
static constexpr uint8_t colPins[COLS] = {BOARD_COL0, BOARD_COL1, BOARD_COL2};

static constexpr uint8_t ENCODER_A_PIN = BOARD_ENCODER_A;
static constexpr uint8_t ENCODER_B_PIN = BOARD_ENCODER_B;
static constexpr uint8_t ENCODER_PUSH_PIN = BOARD_ENCODER_PUSH;
static constexpr uint8_t STATUS_LED_PIN = BOARD_STATUS_LED;
static constexpr uint32_t STATUS_LED_BLINK_MS = 500;

// C5-tuned production values. Keep these shared across all BLE targets.
static constexpr uint32_t DEBOUNCE_MS = 5;
static constexpr uint8_t ROW_SETTLE_US = 5;
static constexpr size_t SERIAL_LINE_MAX = 4096;
static constexpr const char *CONFIG_PATH = "/config.json";
static constexpr uint32_t CHAINOSCPAD_CONFIG_VERSION = 1;

static constexpr char BLE_DEVICE_NAME[] = "ChainOSCPad Keyboard";
static constexpr uint8_t REPORT_ID_KEYBOARD = 1;
static constexpr uint8_t REPORT_ID_CONSUMER = 2;

// ============================================================
// BLE HID
// ============================================================

static NimBLEHIDDevice *hid = nullptr;
static NimBLECharacteristic *keyboardInput = nullptr;
static NimBLECharacteristic *consumerInput = nullptr;
static NimBLEAdvertising *advertising = nullptr;
static bool bleConnected = false;
static bool statusLedState = false;
static uint32_t statusLedLastToggleMs = 0;

static void serviceStatusLed() {
    if (bleConnected) {
        if (!statusLedState) {
            statusLedState = true;
            digitalWrite(STATUS_LED_PIN, HIGH);
        }
        return;
    }

    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - statusLedLastToggleMs) >= STATUS_LED_BLINK_MS) {
        statusLedLastToggleMs = now;
        statusLedState = !statusLedState;
        digitalWrite(STATUS_LED_PIN, statusLedState ? HIGH : LOW);
    }
}

// Keyboard report: modifiers + reserved + 6 keycodes
struct __attribute__((packed)) KeyboardReport {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
};

static KeyboardReport keyboardReport = {};
static uint8_t hidKeyRefs[256] = {0};


class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
        (void)server;
        bleConnected = true;
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override {
        (void)server;
        (void)connInfo;
        bleConnected = false;
        memset(&keyboardReport, 0, sizeof(keyboardReport));
        memset(hidKeyRefs, 0, sizeof(hidKeyRefs));

        NimBLEDevice::startAdvertising();
    }

    void onAuthenticationComplete(NimBLEConnInfo &connInfo) override {
        if (connInfo.isEncrypted()) {
        } else {
        }
    }
};

static ServerCallbacks serverCallbacks;

// Standard keyboard + consumer-control report descriptor.
// Report ID 1 = keyboard, Report ID 2 = consumer control.
static const uint8_t reportMap[] = {
    // Keyboard
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, REPORT_ID_KEYBOARD,
    0x05, 0x07,       // Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       // Usage Minimum (Left Control)
    0x29, 0xE7,       // Usage Maximum (Right GUI)
    0x15, 0x00,       // Logical Minimum (0)
    0x25, 0x01,       // Logical Maximum (1)
    0x75, 0x01,       // Report Size (1)
    0x95, 0x08,       // Report Count (8)
    0x81, 0x02,       // Input (Data,Var,Abs)
    0x95, 0x01,       // Report Count (1)
    0x75, 0x08,       // Report Size (8)
    0x81, 0x01,       // Input (Const)
    0x95, 0x06,       // Report Count (6)
    0x75, 0x08,       // Report Size (8)
    0x15, 0x00,
    0x25, 0x73,       // Logical Maximum
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x73,
    0x81, 0x00,       // Input (Data,Array)
    0xC0,             // End Collection

    // Consumer Control
    // 16-bit bitfield layout (same as proven minimal test / ESP32-BLE-Keyboard).
    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xA1, 0x01,       // Collection (Application)
    0x85, REPORT_ID_CONSUMER,

    0x05, 0x0C,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x10,

    0x09, 0xB5,       // bit 0  Next
    0x09, 0xB6,       // bit 1  Previous
    0x09, 0xB7,       // bit 2  Stop
    0x09, 0xCD,       // bit 3  Play/Pause
    0x09, 0xE2,       // bit 4  Mute
    0x09, 0xE9,       // bit 5  Volume Up
    0x09, 0xEA,       // bit 6  Volume Down
    0x0A, 0x23, 0x02,
    0x0A, 0x94, 0x01,
    0x0A, 0x92, 0x01,
    0x0A, 0x2A, 0x02,
    0x0A, 0x21, 0x02,
    0x0A, 0x26, 0x02,
    0x0A, 0x24, 0x02,
    0x0A, 0x83, 0x01,
    0x0A, 0x8A, 0x01,

    0x81, 0x02,
    0xC0
};

static void sendKeyboardReport() {
    if (!bleConnected || !keyboardInput) return;
    keyboardInput->notify(
        reinterpret_cast<const uint8_t *>(&keyboardReport),
        sizeof(keyboardReport)
    );
}

static bool isModifierUsage(uint8_t usage) {
    return usage >= 0xE0 && usage <= 0xE7;
}

static void addUsageToReport(uint8_t usage) {
    if (isModifierUsage(usage)) {
        keyboardReport.modifiers |= (1u << (usage - 0xE0));
        return;
    }

    for (uint8_t k : keyboardReport.keys) {
        if (k == usage) return;
    }

    for (uint8_t &k : keyboardReport.keys) {
        if (k == 0) {
            k = usage;
            return;
        }
    }
}

static void removeUsageFromReport(uint8_t usage) {
    if (isModifierUsage(usage)) {
        keyboardReport.modifiers &= ~(1u << (usage - 0xE0));
        return;
    }

    for (uint8_t &k : keyboardReport.keys) {
        if (k == usage) {
            k = 0;
            break;
        }
    }

    // Compact the 6-key array.
    uint8_t compact[6] = {};
    uint8_t n = 0;
    for (uint8_t k : keyboardReport.keys) {
        if (k != 0 && n < 6) compact[n++] = k;
    }
    memcpy(keyboardReport.keys, compact, sizeof(compact));
}

static void pressUsageRef(uint8_t usage) {
    if (hidKeyRefs[usage] == 0) {
        addUsageToReport(usage);
        sendKeyboardReport();
    }
    if (hidKeyRefs[usage] < 255) ++hidKeyRefs[usage];
}

static void releaseUsageRef(uint8_t usage) {
    if (hidKeyRefs[usage] == 0) return;
    --hidKeyRefs[usage];
    if (hidKeyRefs[usage] == 0) {
        removeUsageFromReport(usage);
        sendKeyboardReport();
    }
}

static void releaseAllTrackedKeys() {
    memset(hidKeyRefs, 0, sizeof(hidKeyRefs));
    memset(&keyboardReport, 0, sizeof(keyboardReport));
    sendKeyboardReport();
}

// Consumer usages
static constexpr uint16_t MEDIA_VOLUME_UP = 0x00E9;
static constexpr uint16_t MEDIA_VOLUME_DOWN = 0x00EA;
static constexpr uint16_t MEDIA_MUTE = 0x00E2;
static constexpr uint16_t MEDIA_PLAY_PAUSE = 0x00CD;
static constexpr uint16_t MEDIA_NEXT_TRACK = 0x00B5;
static constexpr uint16_t MEDIA_PREVIOUS_TRACK = 0x00B6;
static constexpr uint16_t MEDIA_STOP = 0x00B7;

static uint16_t consumerUsageToBits(uint16_t usage) {
    switch (usage) {
        case MEDIA_NEXT_TRACK:     return 1u << 0;
        case MEDIA_PREVIOUS_TRACK: return 1u << 1;
        case MEDIA_STOP:           return 1u << 2;
        case MEDIA_PLAY_PAUSE:     return 1u << 3;
        case MEDIA_MUTE:           return 1u << 4;
        case MEDIA_VOLUME_UP:      return 1u << 5;
        case MEDIA_VOLUME_DOWN:    return 1u << 6;
        default:                   return 0;
    }
}

// Consumer-Control output is queued so encoder scanning never blocks.
// The previous implementation held each media key for 80 ms using delay(80),
// which prevented the quadrature decoder from seeing transitions while the
// encoder was being turned quickly.
static constexpr uint8_t CONSUMER_QUEUE_SIZE = 32;
static constexpr uint16_t CONSUMER_PRESS_MS = 20;

static uint16_t consumerQueue[CONSUMER_QUEUE_SIZE] = {};
static uint8_t consumerQueueHead = 0;
static uint8_t consumerQueueTail = 0;
static bool consumerPressed = false;
static uint32_t consumerReleaseAt = 0;

static bool enqueueConsumer(uint16_t usage) {
    const uint8_t next = static_cast<uint8_t>((consumerQueueHead + 1) % CONSUMER_QUEUE_SIZE);
    if (next == consumerQueueTail) {
        return false;
    }

    consumerQueue[consumerQueueHead] = usage;
    consumerQueueHead = next;
    return true;
}

static void sendConsumer(uint16_t usage) {
    if (consumerUsageToBits(usage) == 0) {
        return;
    }

    enqueueConsumer(usage);
}

static void serviceConsumerOutput() {
    if (!bleConnected || !consumerInput) {
        consumerQueueHead = 0;
        consumerQueueTail = 0;
        consumerPressed = false;
        return;
    }

    const uint32_t now = millis();

    if (consumerPressed) {
        if (static_cast<int32_t>(now - consumerReleaseAt) < 0) return;

        const uint8_t released[2] = {0x00, 0x00};
        consumerInput->notify(released, sizeof(released));
        consumerPressed = false;
    }

    if (consumerQueueTail == consumerQueueHead) return;

    const uint16_t usage = consumerQueue[consumerQueueTail];
    consumerQueueTail = static_cast<uint8_t>((consumerQueueTail + 1) % CONSUMER_QUEUE_SIZE);

    const uint16_t bits = consumerUsageToBits(usage);
    const uint8_t pressed[2] = {
        static_cast<uint8_t>(bits & 0xFF),
        static_cast<uint8_t>((bits >> 8) & 0xFF)
    };

    consumerInput->notify(pressed, sizeof(pressed));

    consumerPressed = true;
    consumerReleaseAt = now + CONSUMER_PRESS_MS;
}

static void beginBleHid() {

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, true); // bonding, no MITM, Secure Connections

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCallbacks);

    hid = new NimBLEHIDDevice(server);
    hid->setManufacturer("ChainOSCPad");
    hid->setPnp(0x02, 0x05AC, 0x820A, 0x0210);
    hid->setHidInfo(0x00, 0x01);
    hid->setReportMap(const_cast<uint8_t *>(reportMap), sizeof(reportMap));

    // Separate 0x2A4D Input Report characteristics:
    // Report ID 1 = keyboard, Report ID 2 = Consumer Control.
    keyboardInput = hid->getInputReport(REPORT_ID_KEYBOARD);
    consumerInput = hid->getInputReport(REPORT_ID_CONSUMER);

    hid->setBatteryLevel(100);

    // NimBLE-Arduino 2.x starts all services from NimBLEServer::start().
    server->start();

    advertising = NimBLEDevice::getAdvertising();
    advertising->setName(BLE_DEVICE_NAME);
    advertising->setAppearance(HID_KEYBOARD);
    advertising->addServiceUUID(hid->getHidService()->getUUID());
    advertising->enableScanResponse(false);
    advertising->start();

}

// ============================================================
// Configuration
// ============================================================

struct DeviceConfig {
    String keys[KEY_COUNT];
    String encoderCw;
    String encoderCcw;
    String encoderPush;
};

static DeviceConfig config;

static const char *DEFAULT_KEYS[KEY_COUNT] = {
    "F13", "F14", "F15",
    "F16", "F17", "F18",
    "F19", "F20", "F21",
    "F22", "F23", "F24"
};

static void setDefaultConfig() {
    for (uint8_t i = 0; i < KEY_COUNT; ++i) config.keys[i] = DEFAULT_KEYS[i];
    config.encoderCw = "VOLUME_UP";
    config.encoderCcw = "VOLUME_DOWN";
    config.encoderPush = "MUTE";
}

// ============================================================
// Action parser
// ============================================================

struct KeyboardAction {
    bool valid = false;
    bool none = false;
    uint8_t usages[5] = {0};
    uint8_t usageCount = 0;
};

static String normalizeToken(String token) {
    token.trim();
    token.toUpperCase();
    token.replace(" ", "_");
    return token;
}

static bool isMediaAction(const String &raw, uint16_t &usage) {
    const String s = normalizeToken(raw);
    if (s == "VOLUME_UP")       { usage = MEDIA_VOLUME_UP; return true; }
    if (s == "VOLUME_DOWN")     { usage = MEDIA_VOLUME_DOWN; return true; }
    if (s == "MUTE")            { usage = MEDIA_MUTE; return true; }
    if (s == "PLAY_PAUSE")      { usage = MEDIA_PLAY_PAUSE; return true; }
    if (s == "NEXT_TRACK")      { usage = MEDIA_NEXT_TRACK; return true; }
    if (s == "PREVIOUS_TRACK")  { usage = MEDIA_PREVIOUS_TRACK; return true; }
    if (s == "STOP")            { usage = MEDIA_STOP; return true; }
    return false;
}

// USB HID Usage IDs (Keyboard/Keypad page).
static bool tokenToUsage(const String &rawToken, uint8_t &usage) {
    String token = normalizeToken(rawToken);

    if (token.length() == 1) {
        const char c = token.charAt(0);

        if (c >= 'A' && c <= 'Z') {
            usage = 0x04 + (c - 'A');
            return true;
        }

        if (c >= '1' && c <= '9') {
            usage = 0x1E + (c - '1');
            return true;
        }

        if (c == '0') {
            usage = 0x27;
            return true;
        }

        switch (c) {
            case '-': usage = 0x2D; return true;
            case '=': usage = 0x2E; return true;
            case '[': usage = 0x2F; return true;
            case ']': usage = 0x30; return true;
            case '\\': usage = 0x31; return true;
            case ';': usage = 0x33; return true;
            case '\'': usage = 0x34; return true;
            case '`': usage = 0x35; return true;
            case ',': usage = 0x36; return true;
            case '.': usage = 0x37; return true;
            case '/': usage = 0x38; return true;
        }
    }

    if (token == "CTRL" || token == "CONTROL" || token == "LCTRL") {
        usage = 0xE0; return true;
    }
    if (token == "SHIFT" || token == "LSHIFT") {
        usage = 0xE1; return true;
    }
    if (token == "ALT" || token == "LALT") {
        usage = 0xE2; return true;
    }
    if (token == "GUI" || token == "WIN" || token == "WINDOWS" ||
        token == "CMD" || token == "COMMAND" || token == "LGUI") {
        usage = 0xE3; return true;
    }

    if (token == "RCTRL")  { usage = 0xE4; return true; }
    if (token == "RSHIFT") { usage = 0xE5; return true; }
    if (token == "RALT")   { usage = 0xE6; return true; }
    if (token == "RGUI")   { usage = 0xE7; return true; }

    if (token == "ENTER" || token == "RETURN") { usage = 0x28; return true; }
    if (token == "ESC" || token == "ESCAPE")   { usage = 0x29; return true; }
    if (token == "BACKSPACE")                  { usage = 0x2A; return true; }
    if (token == "TAB")                        { usage = 0x2B; return true; }
    if (token == "SPACE")                      { usage = 0x2C; return true; }
    if (token == "CAPS_LOCK" || token == "CAPSLOCK") { usage = 0x39; return true; }

    if (token == "INSERT")    { usage = 0x49; return true; }
    if (token == "HOME")      { usage = 0x4A; return true; }
    if (token == "PAGE_UP" || token == "PAGEUP") { usage = 0x4B; return true; }
    if (token == "DELETE" || token == "DEL")   { usage = 0x4C; return true; }
    if (token == "END")       { usage = 0x4D; return true; }
    if (token == "PAGE_DOWN" || token == "PAGEDOWN") { usage = 0x4E; return true; }
    if (token == "RIGHT")     { usage = 0x4F; return true; }
    if (token == "LEFT")      { usage = 0x50; return true; }
    if (token == "DOWN")      { usage = 0x51; return true; }
    if (token == "UP")        { usage = 0x52; return true; }

    if (token.startsWith("F")) {
        const int n = token.substring(1).toInt();
        if (n >= 1 && n <= 12) {
            usage = 0x3A + (n - 1);
            return true;
        }
        if (n >= 13 && n <= 24) {
            usage = 0x68 + (n - 13);
            return true;
        }
    }

    return false;
}

static KeyboardAction parseKeyboardAction(const String &raw) {
    KeyboardAction action;
    String s = raw;
    s.trim();

    if (s.length() == 0 || normalizeToken(s) == "NONE") {
        action.valid = true;
        action.none = true;
        return action;
    }

    uint16_t mediaUsage = 0;
    if (isMediaAction(s, mediaUsage)) return action;

    int start = 0;
    while (start <= static_cast<int>(s.length())) {
        const int plus = s.indexOf('+', start);
        String token = (plus >= 0) ? s.substring(start, plus) : s.substring(start);
        token.trim();

        if (token.length() == 0 || action.usageCount >= 5) {
            return KeyboardAction();
        }

        uint8_t usage = 0;
        if (!tokenToUsage(token, usage)) {
            return KeyboardAction();
        }

        action.usages[action.usageCount++] = usage;

        if (plus < 0) break;
        start = plus + 1;
    }

    action.valid = action.usageCount > 0;
    return action;
}

static bool validateActionString(const String &raw) {
    uint16_t mediaUsage = 0;
    if (isMediaAction(raw, mediaUsage)) return true;
    return parseKeyboardAction(raw).valid;
}

static void pressAction(const String &raw) {
    uint16_t mediaUsage = 0;
    if (isMediaAction(raw, mediaUsage)) {
        sendConsumer(mediaUsage);
        return;
    }

    const KeyboardAction action = parseKeyboardAction(raw);
    if (!action.valid || action.none) return;

    for (uint8_t i = 0; i < action.usageCount; ++i) {
        pressUsageRef(action.usages[i]);
    }
}

static void releaseAction(const String &raw) {
    uint16_t mediaUsage = 0;
    if (isMediaAction(raw, mediaUsage)) return;

    const KeyboardAction action = parseKeyboardAction(raw);
    if (!action.valid || action.none) return;

    for (int i = action.usageCount - 1; i >= 0; --i) {
        releaseUsageRef(action.usages[i]);
    }
}

static void tapAction(const String &raw) {
    uint16_t mediaUsage = 0;
    if (isMediaAction(raw, mediaUsage)) {
        sendConsumer(mediaUsage);
        return;
    }

    const KeyboardAction action = parseKeyboardAction(raw);
    if (!action.valid || action.none) return;

    for (uint8_t i = 0; i < action.usageCount; ++i) {
        pressUsageRef(action.usages[i]);
    }
    delay(4);
    for (int i = action.usageCount - 1; i >= 0; --i) {
        releaseUsageRef(action.usages[i]);
    }
}

// ============================================================
// LittleFS JSON configuration
// ============================================================

static void configToJson(JsonObject obj) {
    obj["version"] = CHAINOSCPAD_CONFIG_VERSION;

    JsonArray keys = obj["keys"].to<JsonArray>();
    for (uint8_t i = 0; i < KEY_COUNT; ++i) keys.add(config.keys[i]);

    JsonObject enc = obj["encoder"].to<JsonObject>();
    enc["cw"] = config.encoderCw;
    enc["ccw"] = config.encoderCcw;
    enc["push"] = config.encoderPush;
}

static bool jsonToConfig(JsonObjectConst obj, DeviceConfig &out, String &error) {
    if (!obj["keys"].is<JsonArrayConst>()) {
        error = "config.keys must be an array";
        return false;
    }

    JsonArrayConst keys = obj["keys"].as<JsonArrayConst>();
    if (keys.size() != KEY_COUNT) {
        error = "config.keys must contain exactly 12 entries";
        return false;
    }

    if (!obj["encoder"].is<JsonObjectConst>()) {
        error = "config.encoder must be an object";
        return false;
    }

    DeviceConfig candidate;

    for (uint8_t i = 0; i < KEY_COUNT; ++i) {
        if (!keys[i].is<const char *>()) {
            error = "each key assignment must be a string";
            return false;
        }

        candidate.keys[i] = keys[i].as<const char *>();
        if (!validateActionString(candidate.keys[i])) {
            error = "invalid key action at SW" + String(i + 1) + ": " + candidate.keys[i];
            return false;
        }
    }

    JsonObjectConst enc = obj["encoder"].as<JsonObjectConst>();
    if (!enc["cw"].is<const char *>() ||
        !enc["ccw"].is<const char *>() ||
        !enc["push"].is<const char *>()) {
        error = "encoder.cw, encoder.ccw and encoder.push must be strings";
        return false;
    }

    candidate.encoderCw = enc["cw"].as<const char *>();
    candidate.encoderCcw = enc["ccw"].as<const char *>();
    candidate.encoderPush = enc["push"].as<const char *>();

    if (!validateActionString(candidate.encoderCw)) {
        error = "invalid encoder.cw action: " + candidate.encoderCw;
        return false;
    }
    if (!validateActionString(candidate.encoderCcw)) {
        error = "invalid encoder.ccw action: " + candidate.encoderCcw;
        return false;
    }
    if (!validateActionString(candidate.encoderPush)) {
        error = "invalid encoder.push action: " + candidate.encoderPush;
        return false;
    }

    out = candidate;
    return true;
}

static bool saveConfig() {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    configToJson(root);

    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) return false;

    const bool ok = serializeJsonPretty(doc, f) > 0;
    f.close();
    return ok;
}

static bool loadConfig() {
    if (!LittleFS.exists(CONFIG_PATH)) return false;

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) return false;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err || !doc.is<JsonObject>()) return false;

    DeviceConfig loaded;
    String error;
    if (!jsonToConfig(doc.as<JsonObjectConst>(), loaded, error)) return false;

    config = loaded;
    return true;
}

// ============================================================
// WebSerial JSON protocol over ESP32-C3 USB Serial/JTAG
// ============================================================

static void sendConfigResponse(const char *eventName = nullptr) {
    JsonDocument doc;
    doc["ok"] = true;
    if (eventName) doc["event"] = eventName;

    JsonObject cfg = doc["config"].to<JsonObject>();
    configToJson(cfg);

    serializeJson(doc, Serial);
    Serial.println();
}

static void sendError(const String &message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    serializeJson(doc, Serial);
    Serial.println();
}

static void processSerialCommand(const String &line) {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, line);

    if (err) {
        sendError(String("invalid JSON: ") + err.c_str());
        return;
    }

    const char *cmd = doc["cmd"] | "";

    if (strcmp(cmd, "get") == 0) {
        sendConfigResponse();
        return;
    }

    if (strcmp(cmd, "set") == 0) {
        if (!doc["config"].is<JsonObjectConst>()) {
            sendError("missing config object");
            return;
        }

        DeviceConfig newConfig;
        String error;
        if (!jsonToConfig(doc["config"].as<JsonObjectConst>(), newConfig, error)) {
            sendError(error);
            return;
        }

        releaseAllTrackedKeys();

        const DeviceConfig oldConfig = config;
        config = newConfig;

        if (!saveConfig()) {
            config = oldConfig;
            sendError("failed to save /config.json");
            return;
        }

        sendConfigResponse("saved");
        return;
    }

    if (strcmp(cmd, "reset") == 0) {
        releaseAllTrackedKeys();
        setDefaultConfig();

        if (!saveConfig()) {
            sendError("failed to save default config");
            return;
        }

        sendConfigResponse("reset");
        return;
    }

    sendError("unknown command");
}

static String serialLine;

static void serviceSerial() {
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());

        if (ch == '\r') continue;

        if (ch == '\n') {
            if (serialLine.length() > 0) {
                processSerialCommand(serialLine);
                serialLine = "";
            }
            continue;
        }

        if (serialLine.length() >= SERIAL_LINE_MAX) {
            serialLine = "";
            sendError("request too large");
            continue;
        }

        serialLine += ch;
    }
}

// ============================================================
// Matrix scan
// ============================================================

struct DebouncedKey {
    bool rawPressed = false;
    bool stablePressed = false;
    uint32_t lastChangeMs = 0;
};

static DebouncedKey keys[KEY_COUNT];
static DebouncedKey encoderPush;

static void updateMatrixKey(uint8_t index, bool pressed, uint32_t now) {
    DebouncedKey &key = keys[index];

    if (pressed != key.rawPressed) {
        key.rawPressed = pressed;
        key.lastChangeMs = now;
    }

    if (key.stablePressed != key.rawPressed &&
        static_cast<uint32_t>(now - key.lastChangeMs) >= DEBOUNCE_MS) {
        key.stablePressed = key.rawPressed;

        if (key.stablePressed) pressAction(config.keys[index]);
        else releaseAction(config.keys[index]);
    }
}

static void scanMatrix() {
    const uint32_t now = millis();

    for (uint8_t r = 0; r < ROWS; ++r) {
        digitalWrite(rowPins[r], LOW);
        delayMicroseconds(ROW_SETTLE_US);

        for (uint8_t c = 0; c < COLS; ++c) {
            const bool pressed = (digitalRead(colPins[c]) == LOW);
            updateMatrixKey(r * COLS + c, pressed, now);
        }

        digitalWrite(rowPins[r], HIGH);
    }
}

// ============================================================
// Rotary encoder
// ============================================================

static constexpr int8_t encoderTable[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static uint8_t encoderPrevAB = 0;
static int8_t encoderAccumulator = 0;
static int8_t encoderLastDirection = 0;

static void scanEncoderRotation() {
    const uint8_t a = digitalRead(ENCODER_A_PIN) ? 1 : 0;
    const uint8_t b = digitalRead(ENCODER_B_PIN) ? 1 : 0;
    const uint8_t ab = (a << 1) | b;

    if (ab == encoderPrevAB) return;

    const uint8_t transition = (encoderPrevAB << 2) | ab;
    encoderPrevAB = ab;

    const int8_t delta = encoderTable[transition];
    if (delta == 0) return;

    const int8_t direction = (delta > 0) ? 1 : -1;
    if (encoderLastDirection != 0 && direction != encoderLastDirection) {
        encoderAccumulator = 0;
    }
    encoderLastDirection = direction;

    encoderAccumulator += delta;

    if (encoderAccumulator >= 4) {
        encoderAccumulator = 0;
        encoderLastDirection = 0;
        tapAction(config.encoderCw);
    } else if (encoderAccumulator <= -4) {
        encoderAccumulator = 0;
        encoderLastDirection = 0;
        tapAction(config.encoderCcw);
    }
}

static void scanEncoderPush() {
    const uint32_t now = millis();
    const bool pressed = (digitalRead(ENCODER_PUSH_PIN) == LOW);

    if (pressed != encoderPush.rawPressed) {
        encoderPush.rawPressed = pressed;
        encoderPush.lastChangeMs = now;
    }

    if (encoderPush.stablePressed != encoderPush.rawPressed &&
        static_cast<uint32_t>(now - encoderPush.lastChangeMs) >= DEBOUNCE_MS) {
        encoderPush.stablePressed = encoderPush.rawPressed;
        if (encoderPush.stablePressed) tapAction(config.encoderPush);
    }
}

// ============================================================
// Arduino
// ============================================================

void setup() {
    for (uint8_t c = 0; c < COLS; ++c) pinMode(colPins[c], INPUT_PULLUP);
    // Keep every matrix ROW as OUTPUT. Non-selected rows stay HIGH.
    for (uint8_t r = 0; r < ROWS; ++r) {
        pinMode(rowPins[r], OUTPUT);
        digitalWrite(rowPins[r], HIGH);
    }

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    statusLedState = false;
    statusLedLastToggleMs = millis();

    pinMode(ENCODER_A_PIN, INPUT_PULLUP);
    pinMode(ENCODER_B_PIN, INPUT_PULLUP);
    pinMode(ENCODER_PUSH_PIN, INPUT_PULLUP);

    encoderPrevAB =
        ((digitalRead(ENCODER_A_PIN) ? 1 : 0) << 1) |
         (digitalRead(ENCODER_B_PIN) ? 1 : 0);

    Serial.begin(115200);
    delay(300);
    // Do not wait for Serial. BLE keyboard works with no browser connected.

    setDefaultConfig();
    if (LittleFS.begin(true)) {
        if (!loadConfig()) saveConfig();
    }

    serialLine.reserve(SERIAL_LINE_MAX);
    beginBleHid();
}

void loop() {
    scanMatrix();
    scanEncoderRotation();
    scanEncoderPush();
    serviceConsumerOutput();
    serviceSerial();
    serviceStatusLed();
    delay(1);
}
