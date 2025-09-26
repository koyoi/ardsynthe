#pragma once

#define AUDIO_MODE STANDARD_PLUS
#define MOZZI_CONTROL_RATE 128

// ============================================================================
//  Input hardware selection
// ============================================================================
// MCP23017 を使用する場合は何も定義しなくても良いですが、TTP229 を利用する場合は
// ビルド時に `-DKEYBOARD_DRIVER_TTP229` を指定してください (または本ファイルで
// `#define KEYBOARD_DRIVER_TTP229` を有効にしてください)。
// その際は `TTP229_SCL_PIN` / `TTP229_SDO_PIN` などのピン定義も合わせて行ってください。
// どちらの定義も同時に有効にはできません。

#if !defined(KEYBOARD_DRIVER_MCP23017) && !defined(KEYBOARD_DRIVER_TTP229)
#define KEYBOARD_DRIVER_MCP23017
#endif

#if defined(KEYBOARD_DRIVER_MCP23017) && defined(KEYBOARD_DRIVER_TTP229)
#error "KEYBOARD_DRIVER_MCP23017 と KEYBOARD_DRIVER_TTP229 は同時に定義できません"
#endif

#if defined(KEYBOARD_DRIVER_TTP229)
// TTP229 のクロック (SCL) とデータ (SDO) のピンは必ず定義してください。
#ifndef TTP229_SCL_PIN
#error "TTP229_SCL_PIN が定義されていません"
#endif
#ifndef TTP229_SDO_PIN
#error "TTP229_SDO_PIN が定義されていません"
#endif

#ifndef TTP229_KEY_COUNT
#define TTP229_KEY_COUNT 16
#endif

#ifndef TTP229_ACTIVE_STATE
#define TTP229_ACTIVE_STATE LOW
#endif

#ifndef TTP229_CLOCK_DELAY_US
#define TTP229_CLOCK_DELAY_US 5
#endif
#endif
