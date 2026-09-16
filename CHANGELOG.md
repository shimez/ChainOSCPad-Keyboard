# Changelog

ChainOSCPad ESP32 Unified Keyboard Firmwareの主な変更履歴です。

## v6.4

### Refactor
- ESP32-S3 BLE / C3 / C5 / C6の4環境を `src/firmware_ble.inc` へ完全共通化
- ボード固有差分を `src/board_ble.h` のGPIO定義へ集約
- ESP32-C5で確立した固定ROW走査方式をBLE共通実装の基準として維持
- `scanMatrix()` 内で `pinMode()` を行わない構成を4環境で統一
- ROW settle 5 us、debounce 5 ms、Consumer Control press 20 ms、main loop delay 1 msを維持
- C5診断版由来の不要なdiagnostic/tune残骸を削除
- 通常時の人間向けBLE起動ログを削除し、WebSerial JSONレスポンスのみ維持

## v6.3

### Documentation
- READMEをGitHub向けに全面整理
- 現在の製品概要、対応環境、基本的な使用方法を中心とした構成へ変更
- 過去の開発・調査履歴をREADMEからCHANGELOGへ移動

## v6.2

### Changed
- Bluetooth HID device nameを `ChainOSCPad KeyPad` から `ChainOSCPad Keyboard` へ変更

## v6.1

### Added
- D10に接続したLEDをHID接続ステータス表示として使用
- HID未接続時はゆっくり点滅
- HID接続済みは常時点灯
- LED処理はノンブロッキング

### Configurator
- WebSerial切断処理を改善
- デバイス設定更新時のメッセージ表示を追加
- ロータリーエンコーダー選択時にエンコーダー設定を表示
- 重複していた設定初期化ボタンを整理
- シリアル接続失敗時のエラー表示を追加

## v6.0

### Production release
- 5つの正式PlatformIO Environmentを採用
  - `xiao_esp32s3_usb`
  - `xiao_esp32s3_ble`
  - `xiao_esp32c3_ble`
  - `xiao_esp32c5_ble`
  - `xiao_esp32c6_ble`
- 全ESP32ターゲットで固定ROW走査方式を正式採用
- 起動時にROWを`OUTPUT HIGH`へ設定し、走査中の`pinMode()`切り替えを廃止
- C5用診断／runtime tuneコードを正式版から削除
- 通常時の連続Serialログを削除し、WebSerial JSON protocolは維持

Production defaults:

- Matrix debounce: 5 ms
- ROW settle: 5 us
- Consumer Control press: 20 ms
- Main loop delay: 1 ms

## v5.4

### Changed
- 全ESP32ターゲットで固定ROW output scanningを導入
- 選択ROWのみLOWにし、読み取り後HIGHへ戻す方式へ変更
- `scanMatrix()` 内でのGPIOモード変更を廃止
- ESP32-C5で問題になっていた高コストな`pinMode()`呼び出しを走査ループから除去

## v5.3.x

### Diagnostics
- ESP32-C5の入力応答問題を調査するための診断／tuning buildを追加
- loop、matrix、encoder、Consumer Control、Serial処理時間の統計取得機能を追加
- matrix GPIO処理を詳細に分解して計測
- 調査によりESP32-C5で`pinMode()`がmatrix scanの大部分を占めていることを特定

これらの診断機能は正式版v6.0では削除されています。

## v5.2

### Changed
- 通常時の人間向け連続Serialログを削除
- WebSerial Configuratorに必要なJSON-line通信は維持
- Serial Monitorの状態が入力応答へ与える影響を低減

## v5.1

### Fixed
- ESP32-C5/C6のESP-IDF Bluetooth headerにある`CONFIG_VERSION`との名前衝突を修正
- 内部定数を`CHAINOSCPAD_CONFIG_VERSION`へ変更

## v5

### Added
- XIAO ESP32-C5 BLE対応
- XIAO ESP32-C6 BLE対応
- PlatformIO Environment名をMCU + transport形式へ統一

## v4

### Added
- XIAO ESP32-S3 BLE HID対応
- Bluetooth HIDとNative USB CDC / WebSerialの併用
- C3で実績のあるNimBLE HID構成をS3 BLEへ展開

## v3

### Encoder
- A/B状態が変化しない場合の早期return
- 無効遷移をaccumulatorへ加算しない処理を追加
- 回転方向反転時に途中のaccumulatorを破棄
- 4 transition = 1 detentの判定を維持
- Consumer Control送信をノンブロッキング化

## Earlier unified versions

初期のUnified firmwareでは、XIAO ESP32-S3 USB HIDとXIAO ESP32-C3 BLE HIDを1つのPlatformIOプロジェクトへ統合しました。

この段階で以下を共通化しています。

- 12-key matrix
- rotary encoder
- LittleFS `/config.json`
- WebSerial Configurator
- JSON-line configuration protocol
- default F13–F24 keymap
- Consumer Control actions
