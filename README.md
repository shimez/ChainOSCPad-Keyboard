# ChainOSCPad Keyboard Firmware

ChainOSCPadの基板を、USBまたはBluetooth対応の12キーマクロパッド／キーボードとして使用するためのESP32向け独自ファームウェアです。

同じChainOSCPad基板に搭載するXIAOを変更することで、ESP32-S3 / C3 / C5 / C6を利用できます。キー割り当てとロータリーエンコーダーの動作は、ブラウザのWebSerial Configuratorから変更できます。

## Features

- 12キーのキーマトリクス
- ロータリーエンコーダー
  - Clockwise
  - Counter-clockwise
  - Push
- Keyboard HID
- Consumer Control（Volume Up / Volume Down / Muteなど）
- WebSerial Configuratorによる設定
- LittleFSへの設定保存
- D10接続LEDによるHID接続状態表示
- 1つのPlatformIOプロジェクトで複数のXIAO ESP32に対応

初期設定では、12キーに `F13` ～ `F24`、エンコーダーにVolume Up / Volume Down / Muteが割り当てられています。

## Supported targets

| PlatformIO environment | Board | Keyboard connection | Configurator connection |
|---|---|---|---|
| `xiao_esp32s3_usb` | XIAO ESP32-S3 | USB HID | USB CDC / WebSerial |
| `xiao_esp32s3_ble` | XIAO ESP32-S3 | Bluetooth LE HID | USB CDC / WebSerial |
| `xiao_esp32c3_ble` | XIAO ESP32-C3 | Bluetooth LE HID | USB Serial/JTAG / WebSerial |
| `xiao_esp32c5_ble` | XIAO ESP32-C5 | Bluetooth LE HID | USB Serial/JTAG / WebSerial |
| `xiao_esp32c6_ble` | XIAO ESP32-C6 | Bluetooth LE HID | USB Serial/JTAG / WebSerial |

Bluetooth版のデバイス名は **`ChainOSCPad Keyboard`** です。

ESP32-C3 / C5 / C6の内蔵USBはUSB HIDには使用せず、Bluetooth LE HIDでキー入力を送信します。USB接続はConfigurator用です。

## Hardware

ChainOSCPadの基本配線は次のとおりです。

| XIAO pin | Function |
|---|---|
| D0 | ROW0 |
| D1 | ROW1 |
| D2 | ROW2 |
| D3 | ROW3 |
| D4 | COL0 |
| D5 | COL1 |
| D6 | COL2 |
| D7 | Encoder A |
| D8 | Encoder B |
| D9 | Encoder Push |
| D10 | Status LED |

キーマトリクスのダイオード方向は `COL ->| ROW` です。

エンコーダーA/B/Pushは内部プルアップを使用します。エンコーダーのCommonとPushのもう一方はGNDへ接続します。

Status LEDは次の配線を前提とします。

```text
D10 -> series resistor -> LED -> GND
```

LEDはActive HIGHです。

- HID未接続: ゆっくり点滅
- HID接続済み: 常時点灯

## Firmware architecture

4つのBLE環境（ESP32-S3 / C3 / C5 / C6）は、同じ `src/firmware_ble.inc` を使用します。ボードごとの差分は `src/board_ble.h` のGPIO定義だけです。

BLE共通実装は、ESP32-C5で入力応答を調査して確立した固定ROW走査方式を基準にしています。起動時に全ROWを一度だけ `OUTPUT HIGH` に設定し、スキャン中は選択ROWだけをLOWへ切り替えます。`scanMatrix()` 内では `pinMode()` を呼びません。

ESP32-S3 USB版はUSB HID backendが異なるため、現在は `src/firmware_s3.inc` として独立しています。

## Build and upload

[PlatformIO](https://platformio.org/)を使用します。

リポジトリを開き、使用するXIAOに対応するEnvironmentを選択してBuild / Uploadしてください。

例:

```bash
pio run -e xiao_esp32s3_usb
pio run -e xiao_esp32s3_usb -t upload
```

Bluetooth版の例:

```bash
pio run -e xiao_esp32c3_ble
pio run -e xiao_esp32c3_ble -t upload
```

他のボードでは、上記Environment名をSupported targets表のものへ置き換えてください。

## WebSerial Configurator

`index.html` をMicrosoft EdgeまたはGoogle ChromeなどWebSerial対応ブラウザで開きます。

1. ChainOSCPadをUSBでPCへ接続します。
2. `index.html` を開きます。
3. **接続** を押してChainOSCPadのシリアルポートを選択します。
4. 左側のSW1～SW12、またはロータリーエンコーダーを選択します。
5. キー／エンコーダーの動作を設定します。
6. **デバイスに保存** を押します。

設定はデバイスのLittleFSへ保存され、再起動後も維持されます。

Bluetooth版でもConfiguratorはUSB経由で使用します。

## Bluetooth pairing

Bluetooth版を書き込んだ場合は、PCのBluetooth設定から **`ChainOSCPad Keyboard`** を選択してペアリングしてください。

以前のファームウェアを使用していたPCでは、古いペアリング情報が残る場合があります。その場合は既存のChainOSCPadをBluetooth設定から削除して、再度ペアリングしてください。

## Default keymap

| Input | Default action |
|---|---|
| SW1 – SW12 | F13 – F24 |
| Encoder Clockwise | Volume Up |
| Encoder Counter-clockwise | Volume Down |
| Encoder Push | Mute |

割り当てはWebSerial Configuratorから変更できます。

## Configuration

設定はLittleFSの `/config.json` に保存されます。

通常の設定変更ではファイルを直接編集する必要はありません。WebSerial Configuratorを使用してください。

## Changelog

変更履歴は [CHANGELOG.md](CHANGELOG.md) を参照してください。
