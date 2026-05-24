// stage31: 字體系統整併為單一 RURU_FONT_ID
// 所有舊 ID 別名都保留並指向同一個 RURU_FONT_ID，避免 433 處呼叫全部改名
// 全韌體只有一個字體實例：10pt 思源黑體 TC (source_han_sans_tc_10_regular)
#pragma once

// 唯一真實 ID（沿用原 UI_10_FONT_ID 值，serialized settings 仍能讀）
#define RURU_FONT_ID (-1246724383)

// 別名（全部指向同一個 ID 不改動 433 處呼叫）
#define UI_10_FONT_ID         RURU_FONT_ID
#define UI_12_FONT_ID         RURU_FONT_ID
#define SMALL_FONT_ID         RURU_FONT_ID
#define READER_17_FONT_ID     RURU_FONT_ID
#define BOOKERLY_12_FONT_ID   RURU_FONT_ID
#define BOOKERLY_14_FONT_ID   RURU_FONT_ID
#define BOOKERLY_16_FONT_ID   RURU_FONT_ID
#define BOOKERLY_18_FONT_ID   RURU_FONT_ID
#define NOTOSANS_12_FONT_ID   RURU_FONT_ID
#define NOTOSANS_14_FONT_ID   RURU_FONT_ID
#define NOTOSANS_16_FONT_ID   RURU_FONT_ID
#define NOTOSANS_18_FONT_ID   RURU_FONT_ID
