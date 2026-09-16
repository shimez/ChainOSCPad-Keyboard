#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

// ============================================================
// ChainOSCPad wiring for Seeed Studio XIAO ESP32S3
// ============================================================
// XIAO D0  = GPIO1  = ROW0
// XIAO D1  = GPIO2  = ROW1
// XIAO D2  = GPIO3  = ROW2
// XIAO D3  = GPIO4  = ROW3
// XIAO D4  = GPIO5  = COL0
// XIAO D5  = GPIO6  = COL1
// XIAO D6  = GPIO43 = COL2
// XIAO D7  = GPIO44 = Encoder A
// XIAO D8  = GPIO7  = Encoder B
// XIAO D9  = GPIO8  = Encoder Push
// XIAO D10 = GPIO9  = Status LED
//
// Matrix diode direction: COL ->| ROW
// Encoder common and the other side of Encoder Push -> GND.
// ============================================================

static constexpr uint8_t ROWS = 4;
static constexpr uint8_t COLS = 3;
static constexpr uint8_t KEY_COUNT = ROWS * COLS;

static constexpr uint8_t rowPins[ROWS] = {D0, D1, D2, D3};
static constexpr uint8_t colPins[COLS] = {D4, D5, D6};

static constexpr uint8_t ENCODER_A_PIN = D7;
static constexpr uint8_t ENCODER_B_PIN = D8;
static constexpr uint8_t ENCODER_PUSH_PIN = D9;
static constexpr uint8_t STATUS_LED_PIN = D10;
static constexpr uint32_t STATUS_LED_BLINK_MS = 500;

static constexpr uint32_t DEBOUNCE_MS = 5;
static constexpr size_t SERIAL_LINE_MAX = 4096;
static constexpr const char *CONFIG_PATH = "/config.json";
static constexpr uint32_t CHAINOSCPAD_CONFIG_VERSION = 1;

// Composite USB interfaces.
// USBSerial is used by the browser Configurator via WebSerial.
// Keyboard + ConsumerControl are presented at the same time.
USBCDC USBSerial;
USBHIDKeyboard Keyboard;
USBHIDConsumerControl ConsumerControl;

// ============================================================
// USB HID status LED
// ============================================================
static volatile bool usbHidConnected = false;
static bool statusLedState = false;
static uint32_t statusLedLastToggleMs = 0;

static void usbEventCallback(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
    (void)arg;
    (void)eventData;
    if (eventBase != ARDUINO_USB_EVENTS) return;
    switch (eventId) {
        case ARDUINO_USB_STARTED_EVENT:
        case ARDUINO_USB_RESUME_EVENT:
            usbHidConnected = true;
            break;
        case ARDUINO_USB_STOPPED_EVENT:
        case ARDUINO_USB_SUSPEND_EVENT:
            usbHidConnected = false;
            break;
        default:
            break;
    }
}

static void serviceStatusLed() {
    if (usbHidConnected) {
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
    for (uint8_t i = 0; i < KEY_COUNT; ++i) {
        config.keys[i] = DEFAULT_KEYS[i];
    }
    config.encoderCw = "VOLUME_UP";
    config.encoderCcw = "VOLUME_DOWN";
    config.encoderPush = "MUTE";
}

// ============================================================
// HID action parser
// ============================================================

struct KeyboardAction {
    bool valid = false;
    bool none = false;
    uint8_t codes[5] = {0};
    uint8_t codeCount = 0;
};

static String normalizeToken(String token) {
    token.trim();
    token.toUpperCase();
    token.replace(" ", "_");
    return token;
}

static bool isMediaAction(const String &raw, uint16_t &usage) {
    String s = normalizeToken(raw);

    if (s == "VOLUME_UP") {
        usage = CONSUMER_CONTROL_VOLUME_INCREMENT;
        return true;
    }
    if (s == "VOLUME_DOWN") {
        usage = CONSUMER_CONTROL_VOLUME_DECREMENT;
        return true;
    }
    if (s == "MUTE") {
        usage = CONSUMER_CONTROL_MUTE;
        return true;
    }
    if (s == "PLAY_PAUSE") {
        usage = CONSUMER_CONTROL_PLAY_PAUSE;
        return true;
    }
    if (s == "NEXT_TRACK") {
        usage = CONSUMER_CONTROL_SCAN_NEXT;
        return true;
    }
    if (s == "PREVIOUS_TRACK") {
        usage = CONSUMER_CONTROL_SCAN_PREVIOUS;
        return true;
    }
    if (s == "STOP") {
        usage = CONSUMER_CONTROL_STOP;
        return true;
    }

    return false;
}

static bool tokenToKeyCode(const String &rawToken, uint8_t &code) {
    String token = normalizeToken(rawToken);

    if (token.length() == 1) {
        const char c = token.charAt(0);

        if (c >= 'A' && c <= 'Z') {
            code = static_cast<uint8_t>(c - 'A' + 'a');
            return true;
        }

        if (c >= '0' && c <= '9') {
            code = static_cast<uint8_t>(c);
            return true;
        }

        switch (c) {
            case '-': case '=': case '[': case ']':
            case '\\': case ';': case '\'': case ',':
            case '.': case '/': case '`':
                code = static_cast<uint8_t>(c);
                return true;
        }
    }

    if (token == "CTRL" || token == "CONTROL" || token == "LCTRL") {
        code = KEY_LEFT_CTRL;
        return true;
    }
    if (token == "SHIFT" || token == "LSHIFT") {
        code = KEY_LEFT_SHIFT;
        return true;
    }
    if (token == "ALT" || token == "LALT") {
        code = KEY_LEFT_ALT;
        return true;
    }
    if (token == "GUI" || token == "WIN" || token == "WINDOWS" ||
        token == "CMD" || token == "COMMAND" || token == "LGUI") {
        code = KEY_LEFT_GUI;
        return true;
    }

    if (token == "RCTRL") {
        code = KEY_RIGHT_CTRL;
        return true;
    }
    if (token == "RSHIFT") {
        code = KEY_RIGHT_SHIFT;
        return true;
    }
    if (token == "RALT") {
        code = KEY_RIGHT_ALT;
        return true;
    }
    if (token == "RGUI") {
        code = KEY_RIGHT_GUI;
        return true;
    }

    if (token == "ENTER" || token == "RETURN") {
        code = KEY_RETURN;
        return true;
    }
    if (token == "ESC" || token == "ESCAPE") {
        code = KEY_ESC;
        return true;
    }
    if (token == "BACKSPACE") {
        code = KEY_BACKSPACE;
        return true;
    }
    if (token == "TAB") {
        code = KEY_TAB;
        return true;
    }
    if (token == "SPACE") {
        code = KEY_SPACE;
        return true;
    }
    if (token == "CAPS_LOCK" || token == "CAPSLOCK") {
        code = KEY_CAPS_LOCK;
        return true;
    }

    if (token == "UP") {
        code = KEY_UP_ARROW;
        return true;
    }
    if (token == "DOWN") {
        code = KEY_DOWN_ARROW;
        return true;
    }
    if (token == "LEFT") {
        code = KEY_LEFT_ARROW;
        return true;
    }
    if (token == "RIGHT") {
        code = KEY_RIGHT_ARROW;
        return true;
    }

    if (token == "INSERT") {
        code = KEY_INSERT;
        return true;
    }
    if (token == "DELETE" || token == "DEL") {
        code = KEY_DELETE;
        return true;
    }
    if (token == "HOME") {
        code = KEY_HOME;
        return true;
    }
    if (token == "END") {
        code = KEY_END;
        return true;
    }
    if (token == "PAGE_UP" || token == "PAGEUP") {
        code = KEY_PAGE_UP;
        return true;
    }
    if (token == "PAGE_DOWN" || token == "PAGEDOWN") {
        code = KEY_PAGE_DOWN;
        return true;
    }

    if (token.startsWith("F")) {
        const int n = token.substring(1).toInt();
        switch (n) {
            case 1:  code = KEY_F1;  return true;
            case 2:  code = KEY_F2;  return true;
            case 3:  code = KEY_F3;  return true;
            case 4:  code = KEY_F4;  return true;
            case 5:  code = KEY_F5;  return true;
            case 6:  code = KEY_F6;  return true;
            case 7:  code = KEY_F7;  return true;
            case 8:  code = KEY_F8;  return true;
            case 9:  code = KEY_F9;  return true;
            case 10: code = KEY_F10; return true;
            case 11: code = KEY_F11; return true;
            case 12: code = KEY_F12; return true;
            case 13: code = KEY_F13; return true;
            case 14: code = KEY_F14; return true;
            case 15: code = KEY_F15; return true;
            case 16: code = KEY_F16; return true;
            case 17: code = KEY_F17; return true;
            case 18: code = KEY_F18; return true;
            case 19: code = KEY_F19; return true;
            case 20: code = KEY_F20; return true;
            case 21: code = KEY_F21; return true;
            case 22: code = KEY_F22; return true;
            case 23: code = KEY_F23; return true;
            case 24: code = KEY_F24; return true;
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
    if (isMediaAction(s, mediaUsage)) {
        return action;
    }

    int start = 0;
    while (start <= static_cast<int>(s.length())) {
        const int plus = s.indexOf('+', start);
        String token = (plus >= 0) ? s.substring(start, plus) : s.substring(start);
        token.trim();

        if (token.length() == 0 || action.codeCount >= 5) {
            return KeyboardAction();
        }

        uint8_t code = 0;
        if (!tokenToKeyCode(token, code)) {
            return KeyboardAction();
        }

        action.codes[action.codeCount++] = code;

        if (plus < 0) break;
        start = plus + 1;
    }

    action.valid = action.codeCount > 0;
    return action;
}

// Reference counts prevent a shared modifier from being released too early.
static uint8_t keyCodeRefs[256] = {0};

static void pressCodeRef(uint8_t code) {
    if (keyCodeRefs[code] == 0) {
        Keyboard.press(code);
    }
    if (keyCodeRefs[code] < 255) {
        ++keyCodeRefs[code];
    }
}

static void releaseCodeRef(uint8_t code) {
    if (keyCodeRefs[code] == 0) return;

    --keyCodeRefs[code];
    if (keyCodeRefs[code] == 0) {
        Keyboard.release(code);
    }
}

static void tapMedia(uint16_t usage) {
    ConsumerControl.press(usage);
    delay(2);
    ConsumerControl.release();
}

static void pressKeyboardAction(const String &raw) {
    uint16_t mediaUsage = 0;
    if (isMediaAction(raw, mediaUsage)) {
        tapMedia(mediaUsage);
        return;
    }

    KeyboardAction action = parseKeyboardAction(raw);
    if (!action.valid || action.none) return;

    for (uint8_t i = 0; i < action.codeCount; ++i) {
        pressCodeRef(action.codes[i]);
    }
}

static void releaseKeyboardAction(const String &raw) {
    uint16_t mediaUsage = 0;
    if (isMediaAction(raw, mediaUsage)) return;

    KeyboardAction action = parseKeyboardAction(raw);
    if (!action.valid || action.none) return;

    for (int i = action.codeCount - 1; i >= 0; --i) {
        releaseCodeRef(action.codes[i]);
    }
}

static void tapAction(const String &raw) {
    uint16_t mediaUsage = 0;
    if (isMediaAction(raw, mediaUsage)) {
        tapMedia(mediaUsage);
        return;
    }

    KeyboardAction action = parseKeyboardAction(raw);
    if (!action.valid || action.none) return;

    for (uint8_t i = 0; i < action.codeCount; ++i) {
        pressCodeRef(action.codes[i]);
    }

    delay(2);

    for (int i = action.codeCount - 1; i >= 0; --i) {
        releaseCodeRef(action.codes[i]);
    }
}

static bool validateActionString(const String &raw) {
    uint16_t mediaUsage = 0;
    if (isMediaAction(raw, mediaUsage)) return true;

    KeyboardAction action = parseKeyboardAction(raw);
    return action.valid;
}

// ============================================================
// LittleFS JSON configuration
// ============================================================

static void configToJson(JsonObject obj) {
    obj["version"] = CHAINOSCPAD_CONFIG_VERSION;

    JsonArray keys = obj["keys"].to<JsonArray>();
    for (uint8_t i = 0; i < KEY_COUNT; ++i) {
        keys.add(config.keys[i]);
    }

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
// WebSerial JSON protocol
// ============================================================

static void sendConfigResponse(const char *eventName = nullptr) {
    JsonDocument doc;
    doc["ok"] = true;
    if (eventName) doc["event"] = eventName;

    JsonObject cfg = doc["config"].to<JsonObject>();
    configToJson(cfg);

    serializeJson(doc, USBSerial);
    USBSerial.println();
}

static void sendError(const String &message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    serializeJson(doc, USBSerial);
    USBSerial.println();
}

static void releaseAllTrackedKeys() {
    Keyboard.releaseAll();
    memset(keyCodeRefs, 0, sizeof(keyCodeRefs));
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
    while (USBSerial.available() > 0) {
        const char ch = static_cast<char>(USBSerial.read());

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

static void setAllRowsHighZ() {
    for (uint8_t r = 0; r < ROWS; ++r) {
        pinMode(rowPins[r], INPUT);
    }
}

static void updateMatrixKey(uint8_t index, bool pressed, uint32_t now) {
    DebouncedKey &key = keys[index];

    if (pressed != key.rawPressed) {
        key.rawPressed = pressed;
        key.lastChangeMs = now;
    }

    if (key.stablePressed != key.rawPressed &&
        static_cast<uint32_t>(now - key.lastChangeMs) >= DEBOUNCE_MS) {
        key.stablePressed = key.rawPressed;

        if (key.stablePressed) {
            pressKeyboardAction(config.keys[index]);
        } else {
            releaseKeyboardAction(config.keys[index]);
        }
    }
}

static void scanMatrix() {
    const uint32_t now = millis();

    for (uint8_t r = 0; r < ROWS; ++r) {
        digitalWrite(rowPins[r], LOW);
        delayMicroseconds(5);

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

        if (encoderPush.stablePressed) {
            tapAction(config.encoderPush);
        }
    }
}

// ============================================================
// Arduino
// ============================================================

void setup() {
    for (uint8_t c = 0; c < COLS; ++c) {
        pinMode(colPins[c], INPUT_PULLUP);
    }

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

    setDefaultConfig();

    if (LittleFS.begin(true)) {
        if (!loadConfig()) {
            saveConfig();
        }
    }

    serialLine.reserve(SERIAL_LINE_MAX);

    // Build a composite native USB device:
    // CDC (Configurator) + keyboard HID + consumer-control HID.
    USB.onEvent(usbEventCallback);
    USBSerial.begin();
    Keyboard.begin();
    ConsumerControl.begin();
    USB.begin();

    // Do not wait for USB/CDC. The keyboard must work without the Configurator.
}

void loop() {
    scanMatrix();
    scanEncoderRotation();
    scanEncoderPush();
    serviceSerial();
    serviceStatusLed();

    delay(1);
}
