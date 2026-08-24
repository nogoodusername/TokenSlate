#pragma once

#include <TFT_eSPI.h>

/* Per-provider accent colors -- placeholder palette, tune once real
 * providers/branding are wired in (Milestone 5). */
#define COLOR_CODEX    0x9F0F  // muted purple-blue
#define COLOR_CLAUDE   0xFB40  // orange (matches Anthropic's palette)
#define COLOR_CURSOR   0x07E6  // cyan
#define COLOR_DEFAULT  0xFFFF  // white, unknown provider id

#define COLOR_BG       TFT_BLACK
#define COLOR_TEXT     TFT_WHITE
#define COLOR_DIM      0x7BEF  // mid-grey, secondary text

#define COLOR_STATUS_LIVE  0x07E0  // green
#define COLOR_STATUS_STALE 0xFFE0  // yellow
#define COLOR_STATUS_ERROR 0xF800  // red

#define COLOR_RING_TRACK 0x39C7  // dark grey, unfilled ring background
#define COLOR_BAR_TRACK  0x39C7

static inline uint16_t providerColor(const char *id)
{
    if (!strcmp(id, "codex"))  return COLOR_CODEX;
    if (!strcmp(id, "claude")) return COLOR_CLAUDE;
    if (!strcmp(id, "cursor")) return COLOR_CURSOR;
    return COLOR_DEFAULT;
}

/* The bridge only sends the raw CodexBar provider id (docs §7) --
 * capitalize known ones for display, fall back to the id itself. */
static inline const char *providerDisplayName(const char *id)
{
    if (!strcmp(id, "codex"))  return "Codex";
    if (!strcmp(id, "claude")) return "Claude";
    if (!strcmp(id, "cursor")) return "Cursor";
    return id;
}
