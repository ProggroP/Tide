// Tide -- Watchface fuer Emery und Gabbro.
//
// Grosse Stunde links, Minuten als Fortschrittsbogen am Rand, Datum klein
// unten. Grundgeruest aus pebble/tools/vorlage: zwei Farben per Clay,
// Einstellungen persistent. canvas_update ist der einzige
// Zeichen-Einstiegspunkt.

#include <pebble.h>
#include <string.h>

// ---- Farben (Voreinstellung, per Clay aenderbar) ---------------------------
#define COLOR_BG_DEFAULT     GColorBlack
#define COLOR_FG_DEFAULT     GColorWhite
#define COLOR_ACCENT_DEFAULT GColorFromHEX(0x00AAFF)
#define TRACK_MIX_PCT        30     // Bogen-Grundlinie: 30 % Akzentfarbe

// ---- Layout ----------------------------------------------------------------
#define HOUR_FONT_RES        RESOURCE_ID_FONT_HOUR_104
#define DATE_FONT_KEY        FONT_KEY_GOTHIC_24_BOLD
#define HOUR_BOX_H           80
#define HOUR_DX              -4     // optischer Ausgleich: die schmale 1 wirkt sonst rechtslastig
#define HOUR_DY              -27    // LECO 104 sitzt im Zeilenkasten zu tief, daher nach oben
#define DATE_BOX_H           28
#define DATE_BOTTOM_MARGIN   56     // Abstand Datum-Unterkante vom Bildrand
#define ARC_INSET            8      // Abstand Bogen von der Bildkante

#if defined(PBL_PLATFORM_EMERY)
  #define ARC_STROKE         6
#else
  #define ARC_STROKE         7
#endif

// ---- AppMessage ------------------------------------------------------------
// Clay schickt alle Keys in einer Nachricht; ist die Inbox zu klein, verwirft
// die Firmware die ganze Nachricht stumm. Faustformel: 1 + 7 je Key + Nutzdaten.
// Das Watchface sendet nie etwas, die Outbox bleibt minimal.
#define INBOX_SIZE         256
#define OUTBOX_SIZE        64

// ---- Einstellungen ---------------------------------------------------------
// Das Struct wird als Byte-Dump gespeichert, die Feldreihenfolge ist also Teil
// des Speicherformats. Neue Felder NUR am Ende anhaengen: alte Speicherstaende
// sind dann kuerzer, die neuen Felder behalten ihre Voreinstellung.
#define SETTINGS_KEY       1
#define SETTINGS_VERSION   2

typedef struct {
  uint8_t version;
  GColor  bg;
  GColor  fg;
  GColor  accent;
} Settings;

static Window *s_window;
static Layer *s_canvas;
static GFont s_hour_font;
static GFont s_date_font;
static Settings s_settings;

static void settings_defaults(void) {
  s_settings.version = SETTINGS_VERSION;
  s_settings.bg = COLOR_BG_DEFAULT;
  s_settings.fg = COLOR_FG_DEFAULT;
  s_settings.accent = COLOR_ACCENT_DEFAULT;
}

static void settings_load(void) {
  settings_defaults();
  if (persist_exists(SETTINGS_KEY)) {
    persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
  }
  // Gleiche Farben waeren unlesbar.
  if (gcolor_equal(s_settings.bg, s_settings.fg)) {
    s_settings.fg = gcolor_equal(s_settings.bg, GColorWhite) ? GColorBlack : GColorWhite;
  }
  if (gcolor_equal(s_settings.bg, s_settings.accent)) {
    s_settings.accent = s_settings.fg;
  }
}

static void settings_save(void) {
  s_settings.version = SETTINGS_VERSION;
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

// Mischt zwei Farben im 2-Bit-Raster des Pebble-Farbraums. Gerundet, damit
// die Grundlinie auch auf schwarzem Hintergrund sichtbar bleibt.
static GColor blend_color(GColor fg, GColor bg, uint8_t fg_pct) {
  GColor8 out;
  out.a = 3;
  out.r = (fg.r * fg_pct + bg.r * (100 - fg_pct) + 50) / 100;
  out.g = (fg.g * fg_pct + bg.g * (100 - fg_pct) + 50) / 100;
  out.b = (fg.b * fg_pct + bg.b * (100 - fg_pct) + 50) / 100;
  return out;
}

// Punkt auf dem Kreis; 0 Grad liegt oben, positive Winkel laufen im Uhrzeigersinn.
static GPoint polar_point(GPoint c, int32_t angle, int16_t radius) {
  return GPoint(c.x + (int16_t)(sin_lookup(angle) * radius / TRIG_MAX_RATIO),
                c.y - (int16_t)(cos_lookup(angle) * radius / TRIG_MAX_RATIO));
}

static void copy_hour(char *buf, size_t len, const struct tm *t) {
  if (clock_is_24h_style()) {
    strftime(buf, len, "%H", t);
  } else {
    strftime(buf, len, "%I", t);
    if (buf[0] == '0') {
      memmove(buf, buf + 1, strlen(buf));
    }
  }
}

static void draw_minute_arc(GContext *ctx, GRect bounds, const struct tm *t) {
  GRect arc_rect = GRect(bounds.origin.x + ARC_INSET,
                         bounds.origin.y + ARC_INSET,
                         bounds.size.w - 2 * ARC_INSET,
                         bounds.size.h - 2 * ARC_INSET);
  GPoint center = grect_center_point(&arc_rect);
  int16_t radius = (arc_rect.size.w < arc_rect.size.h ? arc_rect.size.w
                                                      : arc_rect.size.h) / 2;
  int32_t minute_angle = (int32_t)TRIG_MAX_ANGLE * t->tm_min / 60;

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_width(ctx, ARC_STROKE);

  // Grundlinie des Bogens, dezent zwischen Akzent- und Hintergrundfarbe.
  graphics_context_set_stroke_color(ctx,
      blend_color(s_settings.accent, s_settings.bg, TRACK_MIX_PCT));
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  // Fortschritt der laufenden Stunde.
  graphics_context_set_stroke_color(ctx, s_settings.accent);
  if (minute_angle > 0) {
    graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle, 0, minute_angle);
  }

  // Markierung an der aktuellen Minute, damit auch Minute 0 sichtbar ist.
  GPoint marker = polar_point(center, minute_angle, radius);
  graphics_context_set_fill_color(ctx, s_settings.accent);
  graphics_fill_circle(ctx, marker, ARC_STROKE / 2 + 1);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, s_settings.bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  static char hour_buf[4];
  static char date_buf[16];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  copy_hour(hour_buf, sizeof(hour_buf), t);
  strftime(date_buf, sizeof(date_buf), "%a %d", t);

  draw_minute_arc(ctx, b, t);

  graphics_context_set_text_color(ctx, s_settings.fg);

  GRect hour_box = GRect(HOUR_DX, (b.size.h - HOUR_BOX_H) / 2 + HOUR_DY,
                         b.size.w, HOUR_BOX_H);
  graphics_draw_text(ctx, hour_buf, s_hour_font, hour_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  GRect date_box = GRect(0, b.size.h - DATE_BOTTOM_MARGIN, b.size.w, DATE_BOX_H);
  graphics_draw_text(ctx, date_buf, s_date_font, date_box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  layer_mark_dirty(s_canvas);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_BG))) {
    s_settings.bg = GColorFromHEX(t->value->int32);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_FG))) {
    s_settings.fg = GColorFromHEX(t->value->int32);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_ACCENT))) {
    s_settings.accent = GColorFromHEX(t->value->int32);
  }
  settings_save();
  settings_load();
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  settings_load();
  s_hour_font = fonts_load_custom_font(resource_get_handle(HOUR_FONT_RES));
  s_date_font = fonts_get_system_font(DATE_FONT_KEY);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received);
  AppMessageResult r = app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  if (r != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "app_message_open: %d", (int)r);
  }
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  fonts_unload_custom_font(s_hour_font);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
