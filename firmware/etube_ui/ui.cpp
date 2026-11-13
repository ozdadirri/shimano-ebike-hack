// E-TUBE Tap touchscreen UI — PORTRAIT 240x320 (LVGL 9).
// Content and colours follow shimano-ebike-hack/docs/img (dashboard.png, media-*.svg),
// re-laid out vertically. Navigation: tab bar at the bottom (tap) or swipe left/right.
//   ┌ status bar ──────────────┐  0..24
//   │ page content             │  24..280
//   └ RIDE MAP MUSIC RADIO MP3 ┘  280..320
#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"
#include "gps_track.h"
#include "sound.h"
#include "radio.h"
#include "iphone.h"

LV_FONT_DECLARE(font_speed);  // Saira Condensed Bold 72, digits only
LV_FONT_DECLARE(font_b22);    // Saira Condensed Bold 22
LV_FONT_DECLARE(font_b16);    // Saira Condensed Bold 16
LV_FONT_DECLARE(font_sb15);   // Saira Condensed SemiBold 15
LV_FONT_DECLARE(font_r15);    // Saira Condensed Regular 15
LV_FONT_DECLARE(font_r12);    // Saira Condensed Regular 12
LV_FONT_DECLARE(font_r18);    // Saira Condensed Regular 18
LV_FONT_DECLARE(font_mono);   // JetBrains Mono 11
LV_FONT_DECLARE(font_st15);   // Saira SemiBold 15 + Noto Sans SC (station names)
LV_FONT_DECLARE(font_st16);
LV_FONT_DECLARE(font_cjk16);  // Saira SemiBold 16 + ~6800 common Chinese characters   // Saira Bold 16 + Noto Sans SC (selected station)

// ---- Palette (from the design SVGs) ----
#define C_BG      0x0b0f0e
#define C_TEXT    0xe8f1ee
#define C_DIM     0x93a39e
#define C_OK      0x6cd07a
#define C_WARN    0xffc24c
#define C_ACCENT  0xff8b52
#define C_DARK    0x1a0d06
#define C_OUTLINE 0x34403c
#define C_TRACK   0x232a28
#define C_TEAL    0x3cc5ba
#define C_CARD    0x161e1b
#define C_ART     0x2c3a35
#define C_SEL     0x2a1a12

static const int SW = 240, SH = 320;
static const int TAB_Y = 280, TAB_H = SH - TAB_Y;
static const int TILE_COUNT = 5;
enum Tile { TILE_RIDE, TILE_MAP, TILE_PLAYING, TILE_RADIO, TILE_MP3 };

// ---------------------------------------------------------------- helpers
static lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                      int32_t fill = -1, int32_t stroke = -1) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  if (fill >= 0) {
    lv_obj_set_style_bg_color(o, C(fill), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  }
  if (stroke >= 0) {
    lv_obj_set_style_border_color(o, C(stroke), 0);
    lv_obj_set_style_border_width(o, 1, 0);
  }
  return o;
}

static lv_obj_t *circle(lv_obj_t *parent, int cx, int cy, int r, int32_t fill) {
  lv_obj_t *o = rect(parent, cx - r, cy - r, 2 * r, 2 * r, LV_RADIUS_CIRCLE, fill);
  return o;
}

// Text placed by its baseline (x = left edge).
static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, uint32_t color, const char *s, int x,
                      int baseline) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, C(color), 0);
  lv_label_set_text(l, s);
  lv_obj_set_pos(l, x, baseline - (f->line_height - f->base_line));
  lv_obj_set_user_data(l, (void *)(intptr_t)baseline);  // lets setTextAuto() keep the baseline
  return l;
}

// Right-aligned text: right edge at x.
static lv_obj_t *textEnd(lv_obj_t *parent, const lv_font_t *f, uint32_t color, const char *s,
                         int x, int baseline, int width = 120) {
  lv_obj_t *l = text(parent, f, color, s, x - width, baseline);
  lv_obj_set_width(l, width);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
  return l;
}

static lv_obj_t *centerText(lv_obj_t *obj, const lv_font_t *f, uint32_t color, const char *s) {
  lv_obj_t *l = lv_label_create(obj);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, C(color), 0);
  lv_label_set_text(l, s);
  lv_obj_center(l);
  return l;
}

static void ev_sound(lv_event_t *e) { sound_play((Sound)(intptr_t)lv_event_get_user_data(e)); }

// snd < 0 = silent (tabs: the page change itself plays a sound)
static void makeButton(lv_obj_t *o, lv_event_cb_t cb, intptr_t arg, int extra = 6,
                       int snd = SOUND_CLICK) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  if (snd >= 0) lv_obj_add_event_cb(o, ev_sound, LV_EVENT_CLICKED, (void *)(intptr_t)snd);
  lv_obj_add_event_cb(o, [](lv_event_t *e) {
    lv_area_t a;
    lv_obj_get_coords((lv_obj_t *)lv_event_get_target(e), &a);
    Serial.printf("click: button at %d,%d %dx%d\n", a.x1, a.y1, lv_area_get_width(&a), lv_area_get_height(&a));
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_set_ext_click_area(o, extra);
  lv_obj_set_style_opa(o, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void *)arg);
}

// Transparent round touch target with a symbol.
static void icon(lv_obj_t *parent, int cx, int cy, const char *sym, lv_event_cb_t cb,
                 intptr_t arg = 0, const lv_font_t *f = &lv_font_montserrat_20) {
  lv_obj_t *o = circle(parent, cx, cy, 20, -1);
  centerText(o, f, C_TEXT, sym);
  makeButton(o, cb, arg, 4);
}

// Orange play/pause circle; returns its icon label.
static lv_obj_t *playButton(lv_obj_t *parent, int cx, int cy, int r, lv_event_cb_t cb) {
  lv_obj_t *o = circle(parent, cx, cy, r, C_ACCENT);
  lv_obj_t *l = centerText(o, r >= 20 ? &lv_font_montserrat_20 : &lv_font_montserrat_14, C_DARK,
                           LV_SYMBOL_PAUSE);
  makeButton(o, cb, 0, 6);
  return l;
}

static lv_obj_t *bar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
  lv_obj_t *b = lv_bar_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_bg_color(b, C(C_TRACK), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b, C(color), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(b, 0, 1000);
  return b;
}

static void setText(lv_obj_t *l, const char *s) {
  if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);
}

// Like setText, but switches to the Chinese-capable font when the text needs it
// (any 3-byte UTF-8 character), keeping the label on the same baseline.
static void setTextAuto(lv_obj_t *l, const char *s, const lv_font_t *latin) {
  bool wide = false;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    if (*p >= 0xE0) { wide = true; break; }
  const lv_font_t *f = wide ? &font_cjk16 : latin;
  if (lv_obj_get_style_text_font(l, LV_PART_MAIN) != f) {
    int baseline = (int)(intptr_t)lv_obj_get_user_data(l);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_y(l, baseline - (f->line_height - f->base_line));
  }
  setText(l, s);
}

static void fmtTime(char *buf, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  snprintf(buf, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void show(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------- widgets
static lv_obj_t *tv, *tiles[TILE_COUNT];
static lv_obj_t *st_bus, *st_right;
static lv_obj_t *tab_btn[TILE_COUNT], *tab_txt[TILE_COUNT], *tab_line[TILE_COUNT];

static lv_obj_t *r_speed, *r_bat, *r_batBar, *r_range, *r_trip, *r_light, *r_mode[3], *r_modeTxt[3];
static lv_obj_t *r_title, *r_artist, *r_prog, *r_play, *r_hr;

// map
static const int MAP_X = 6, MAP_Y = 48, MAP_W = 228, MAP_H = 186, MAP_DRAW_MAX = 240;
static lv_obj_t *mp_area, *mp_line, *mp_head, *mp_pos, *mp_start, *mp_scale, *mp_scaleTxt;
static lv_obj_t *mp_hdr, *mp_gps, *mp_spd, *mp_hr, *mp_time, *mp_wait;
static lv_point_precise_t mp_pts[MAP_DRAW_MAX + 1], mp_headPts[2], mp_scalePts[2];

static lv_obj_t *p_hdr, *p_link, *p_title, *p_artist, *p_album, *p_vol, *p_prog, *p_knob, *p_pos, *p_len, *p_play;
static const int P_PROG_X = 10, P_PROG_W = 220;

static lv_obj_t *rd_row[STATION_COUNT], *rd_name[STATION_COUNT], *rd_fmt[STATION_COUNT],
    *rd_live[STATION_COUNT];
static lv_obj_t *rd_buf, *rd_vol, *rd_play, *rd_status, *rd_info[STATION_COUNT];

static const int MP3_MAX_ROWS = 400;
static lv_obj_t *m_hdr, *m_list, *m_empty, *m_time, *m_prog;
static lv_obj_t *m_row[MP3_MAX_ROWS], *m_num[MP3_MAX_ROWS];
static int m_rows = -1, m_sel = -1, m_gen = -1;
static lv_obj_t *m_shuffle, *m_repeat, *m_play;

// ---------------------------------------------------------------- events
static void goTile(int i) { lv_tileview_set_tile_by_index(tv, i, 0, LV_ANIM_ON); }
static void ev_tab(lv_event_t *e) { goTile((int)(intptr_t)lv_event_get_user_data(e)); }
static void ev_mode(lv_event_t *e) {
  if (!ride.simulated) return;  // live bike data: the link is read-only, use the bike's switch
  ride.assist = (Assist)(intptr_t)lv_event_get_user_data(e);
  ui_update();
}
static void ev_openPlaying(lv_event_t *) { goTile(TILE_PLAYING); }
// MUSIC tab buttons send Apple Media Service commands to the iPhone
static void ev_phonePlay(lv_event_t *) { iphone_cmd(AMS_TOGGLE); }
static void ev_phonePrev(lv_event_t *) { iphone_cmd(AMS_PREV); }
static void ev_phoneNext(lv_event_t *) { iphone_cmd(AMS_NEXT); }
static void ev_vol(lv_event_t *e) {
  iphone_cmd((intptr_t)lv_event_get_user_data(e) > 0 ? AMS_VOL_UP : AMS_VOL_DOWN);
}
static bool radioActive() {
  return media.radioState == RADIO_PLAYING || media.radioState == RADIO_CONNECTING;
}
static void ev_station(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i == media.station && radioActive()) radio_stop();
  else radio_play(i);
  media.station = i;
  ui_update();
}
static void ev_radioPlay(lv_event_t *) {
  if (radioActive()) radio_stop();
  else radio_play(media.station);
  ui_update();
}
static void ev_radioStop(lv_event_t *) {
  radio_stop();
  ui_update();
}
static void ev_radioVol(lv_event_t *e) {
  media.radioVolume = constrain(media.radioVolume + (int)(intptr_t)lv_event_get_user_data(e), 0, RADIO_VOL_MAX);
  radio_volume(media.radioVolume);
  ui_update();
}
static void ev_radioNext(lv_event_t *) {
  media.station = (media.station + 1) % STATION_COUNT;
  radio_play(media.station);
  ui_update();
}
static void ev_mp3Row(lv_event_t *e) {
  mp3_play((int)(intptr_t)lv_event_get_user_data(e));
  ui_update();
}
static void ev_mp3Play(lv_event_t *) {
  if (mp3_count() == 0) return;
  mp3_toggle();
  ui_update();
}
static void ev_mp3Step(lv_event_t *e) {
  if (mp3_count() == 0) return;
  mp3_step((int)(intptr_t)lv_event_get_user_data(e));
  ui_update();
}
static void ev_shuffle(lv_event_t *) { media.shuffle = !media.shuffle; ui_update(); }
static void ev_repeat(lv_event_t *) { media.repeat = !media.repeat; ui_update(); }
static void ev_tile(lv_event_t *) {
  static int last = -1;
  // opening MP3 with no card found: look again (card may have been inserted)
  if (lv_tileview_get_tile_active(tv) == tiles[TILE_MP3] && media.sdScanned && !media.sdOk) mp3_rescan();
  lv_obj_t *a = lv_tileview_get_tile_active(tv);
  if (a != (last >= 0 ? tiles[last] : nullptr)) sound_play(SOUND_TAB);
  for (int i = 0; i < TILE_COUNT; i++)
    if (tiles[i] == a) last = i;
  ui_update();
}

// ---------------------------------------------------------------- chrome
static void buildStatusBar() {
  lv_obj_t *top = lv_layer_top();
  rect(top, 0, 0, SW, 24, 0, C_BG);
  st_bus = text(top, &font_mono, C_OK, "", 10, 16);
  st_right = textEnd(top, &font_mono, C_DIM, "", 230, 16);
}

static void buildTabBar() {
  lv_obj_t *top = lv_layer_top();
  rect(top, 0, TAB_Y, SW, TAB_H, 0, C_CARD);
  rect(top, 0, TAB_Y, SW, 1, 0, C_OUTLINE);
  static const char *names[TILE_COUNT] = {"RIDE", "MAP", "MUSIC", "RADIO", "MP3"};
  for (int i = 0; i < TILE_COUNT; i++) {
    tab_btn[i] = rect(top, i * 48, TAB_Y, 48, TAB_H, 0);
    tab_txt[i] = centerText(tab_btn[i], &font_sb15, C_DIM, names[i]);
    tab_line[i] = rect(tab_btn[i], 8, 0, 32, 3, 1, C_ACCENT);
    makeButton(tab_btn[i], ev_tab, i, 0, -1);
  }
}

// ---------------------------------------------------------------- pages
static void buildRide(lv_obj_t *t) {
  // speed, right-aligned in a fixed box so "km/h" never moves
  r_speed = textEnd(t, &font_speed, C_TEXT, "0.0", 158, 96, 150);
  text(t, &font_r18, C_DIM, "km/h", 164, 96);
  r_trip = text(t, &font_mono, C_DIM, "", 12, 116);
  r_light = textEnd(t, &font_mono, C_DIM, "", 230, 116, 90);  // live bike data only

  static const char *names[3] = {"ECO", "NORMAL", "HIGH"};
  for (int i = 0; i < 3; i++) {
    r_mode[i] = rect(t, 10 + i * 75, 126, 70, 36, 6, -1, C_OUTLINE);
    r_modeTxt[i] = centerText(r_mode[i], &font_sb15, C_DIM, names[i]);
    lv_obj_set_style_text_letter_space(r_modeTxt[i], 1, 0);
    makeButton(r_mode[i], ev_mode, i, 3);
  }

  text(t, &font_mono, C_DIM, "BATTERY", 12, 184);
  r_bat = text(t, &font_b22, C_TEXT, "", 12, 208);
  r_batBar = bar(t, 12, 214, 64, 6, C_TEAL);
  text(t, &font_mono, C_DIM, "RANGE", 90, 184);
  r_range = text(t, &font_b22, C_TEXT, "", 90, 208);
  text(t, &font_mono, C_DIM, "HEART", 166, 184);
  r_hr = text(t, &font_b22, C_DIM, "--", 166, 208);

  // mini player card — tap it to open MUSIC
  lv_obj_t *card = rect(t, 8, 228, 224, 48, 8, C_CARD);
  makeButton(card, ev_openPlaying, 0, 0);
  lv_obj_set_style_opa(card, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_t *art = rect(card, 6, 6, 36, 36, 5, C_ART);
  centerText(art, &lv_font_montserrat_14, C_DIM, LV_SYMBOL_AUDIO);
  r_title = text(card, &font_b16, C_TEXT, "", 50, 20);
  lv_obj_set_width(r_title, 100);
  lv_label_set_long_mode(r_title, LV_LABEL_LONG_DOT);
  r_artist = text(card, &font_r12, C_DIM, "", 50, 35);
  lv_obj_set_width(r_artist, 100);
  lv_label_set_long_mode(r_artist, LV_LABEL_LONG_DOT);
  r_prog = bar(card, 50, 40, 100, 3, C_ACCENT);
  r_play = playButton(card, 172, 24, 15, ev_phonePlay);
  icon(card, 206, 24, LV_SYMBOL_NEXT, ev_phoneNext, 0, &lv_font_montserrat_14);
}

static lv_obj_t *line(lv_obj_t *parent, int width, uint32_t color) {
  lv_obj_t *l = lv_line_create(parent);
  lv_obj_set_pos(l, 0, 0);
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_color(l, C(color), 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  return l;
}

static void buildMap(lv_obj_t *t) {
  mp_hdr = text(t, &font_mono, C_ACCENT, "", 10, 40);
  mp_gps = textEnd(t, &font_mono, C_WARN, "", 230, 40, 90);

  mp_area = rect(t, MAP_X, MAP_Y, MAP_W, MAP_H, 8, C_CARD);  // children are clipped to it
  mp_line = line(mp_area, 3, C_ACCENT);
  mp_head = line(mp_area, 3, C_TEXT);
  mp_start = rect(mp_area, 0, 0, 8, 8, LV_RADIUS_CIRCLE, C_TEAL);
  mp_pos = rect(mp_area, 0, 0, 12, 12, LV_RADIUS_CIRCLE, C_TEXT);
  lv_obj_set_style_border_color(mp_pos, C(C_ACCENT), 0);
  lv_obj_set_style_border_width(mp_pos, 3, 0);
  mp_scale = line(mp_area, 2, C_DIM);
  mp_scaleTxt = text(mp_area, &font_mono, C_DIM, "", 10, MAP_H - 14);
  lv_obj_t *n = text(mp_area, &font_mono, C_DIM, "N", MAP_W - 18, 30);
  lv_obj_t *up = lv_label_create(mp_area);
  lv_obj_set_style_text_font(up, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(up, C(C_DIM), 0);
  lv_label_set_text(up, LV_SYMBOL_UP);
  lv_obj_align_to(up, n, LV_ALIGN_OUT_TOP_MID, 0, 2);
  mp_wait = centerText(mp_area, &font_r15, C_DIM, "waiting for GPS fix...");

  text(t, &font_mono, C_DIM, "SPEED", 10, 252);
  mp_spd = text(t, &font_b22, C_TEXT, "", 10, 276);
  text(t, &font_mono, C_DIM, "HEART", 90, 252);
  mp_hr = text(t, &font_b22, C_DIM, "--", 90, 276);
  text(t, &font_mono, C_DIM, "TIME", 166, 252);
  mp_time = text(t, &font_b22, C_TEXT, "", 166, 276);
}

static void buildPlaying(lv_obj_t *t) {
  p_hdr = text(t, &font_mono, C_ACCENT, "NOW PLAYING", 10, 40);
  lv_obj_set_width(p_hdr, 165);
  lv_label_set_long_mode(p_hdr, LV_LABEL_LONG_DOT);
  p_vol = textEnd(t, &font_mono, C_DIM, "", 230, 40, 60);

  lv_obj_t *art = rect(t, 10, 50, 90, 90, 8, C_ART);
  centerText(art, &lv_font_montserrat_28, C_DIM, LV_SYMBOL_AUDIO);
  p_title = text(t, &font_b22, C_TEXT, "", 110, 74);
  lv_obj_set_width(p_title, 122);
  lv_label_set_long_mode(p_title, LV_LABEL_LONG_DOT);
  lv_label_set_long_mode(p_title, LV_LABEL_LONG_DOT);
  p_artist = text(t, &font_r15, C_DIM, "", 110, 96);
  lv_obj_set_width(p_artist, 122);
  lv_label_set_long_mode(p_artist, LV_LABEL_LONG_DOT);
  p_album = text(t, &font_mono, C_DIM, "", 110, 116);
  lv_obj_set_width(p_album, 122);
  lv_label_set_long_mode(p_album, LV_LABEL_LONG_DOT);
  p_link = text(t, &font_mono, C_DIM, "", 110, 134);
  lv_obj_set_width(p_link, 125);
  lv_label_set_long_mode(p_link, LV_LABEL_LONG_DOT);

  p_prog = bar(t, P_PROG_X, 154, P_PROG_W, 5, C_ACCENT);
  p_knob = circle(t, P_PROG_X, 156, 6, C_ACCENT);
  p_pos = text(t, &font_mono, C_DIM, "", 10, 178);
  p_len = textEnd(t, &font_mono, C_DIM, "", 230, 178, 60);

  icon(t, 56, 214, LV_SYMBOL_PREV, ev_phonePrev);
  p_play = playButton(t, 120, 214, 26, ev_phonePlay);
  icon(t, 184, 214, LV_SYMBOL_NEXT, ev_phoneNext);

  lv_obj_t *b = rect(t, 10, 244, 105, 32, 8, -1, C_OUTLINE);
  centerText(b, &font_sb15, C_TEXT, "VOL  -");
  makeButton(b, ev_vol, -10, 2);
  b = rect(t, 125, 244, 105, 32, 8, -1, C_OUTLINE);
  centerText(b, &font_sb15, C_TEXT, "VOL  +");
  makeButton(b, ev_vol, 10, 2);
}

static void buildRadio(lv_obj_t *t) {
  text(t, &font_mono, C_ACCENT, "INTERNET RADIO", 10, 40);
  rd_status = textEnd(t, &font_mono, C_DIM, "", 230, 40, 130);
  // scrolling station list
  lv_obj_t *list = lv_obj_create(t);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 10, 48);
  lv_obj_set_size(list, 220, 136);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  for (int i = 0; i < STATION_COUNT; i++) {
    rd_row[i] = rect(list, 0, i * 46, 214, 42, 6, C_CARD);
    lv_obj_set_style_border_color(rd_row[i], C(C_ACCENT), 0);
    makeButton(rd_row[i], ev_station, i, 0);
    rd_name[i] = text(rd_row[i], &font_st15, C_TEXT, STATIONS[i].name, 10, 19);
    lv_obj_set_width(rd_name[i], 150);
    lv_label_set_long_mode(rd_name[i], LV_LABEL_LONG_DOT);
    rd_info[i] = text(rd_row[i], &font_mono, C_DIM, STATIONS[i].info, 10, 35);
    lv_obj_set_width(rd_info[i], 198);
    lv_label_set_long_mode(rd_info[i], LV_LABEL_LONG_DOT);
    lv_obj_set_width(rd_name[i], 136);
    rd_fmt[i] = textEnd(rd_row[i], &font_mono, C_DIM, STATIONS[i].format, 206, 17, 60);
    rd_live[i] = rect(rd_row[i], 162, 6, 44, 16, 8, C_ACCENT);
    centerText(rd_live[i], &font_mono, C_DARK, "LIVE");
  }

  lv_obj_t *card = rect(t, 10, 188, 220, 88, 10, C_CARD);
  // volume row
  lv_obj_t *b = rect(card, 8, 6, 52, 30, 8, -1, C_OUTLINE);
  centerText(b, &lv_font_montserrat_20, C_TEXT, LV_SYMBOL_VOLUME_MID " -");
  makeButton(b, ev_radioVol, -1, 4);
  rd_vol = lv_label_create(card);
  lv_obj_set_style_text_font(rd_vol, &font_b16, 0);
  lv_obj_set_style_text_color(rd_vol, C(C_TEXT), 0);
  lv_obj_align(rd_vol, LV_ALIGN_TOP_MID, 0, 10);
  b = rect(card, 160, 6, 52, 30, 8, -1, C_OUTLINE);
  centerText(b, &lv_font_montserrat_20, C_TEXT, LV_SYMBOL_VOLUME_MAX " +");
  makeButton(b, ev_radioVol, 1, 4);
  // transport row
  icon(card, 50, 60, LV_SYMBOL_STOP, ev_radioStop, 0, &lv_font_montserrat_14);
  rd_play = playButton(card, 110, 60, 20, ev_radioPlay);
  icon(card, 170, 60, LV_SYMBOL_NEXT, ev_radioNext);
  // stream buffer as a thin line along the bottom
  rd_buf = bar(card, 12, 82, 196, 3, C_TEAL);
}

static void buildMp3(lv_obj_t *t) {
  m_hdr = text(t, &font_mono, C_ACCENT, "MP3 \xC2\xB7 reading SD...", 10, 40);

  // scrolling track list (rows are added once the SD scan has finished)
  m_list = lv_obj_create(t);
  lv_obj_remove_style_all(m_list);
  lv_obj_set_pos(m_list, 10, 48);
  lv_obj_set_size(m_list, 220, 130);
  lv_obj_set_flex_flow(m_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(m_list, 4, 0);
  lv_obj_set_scroll_dir(m_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(m_list, LV_SCROLLBAR_MODE_ACTIVE);
  m_empty = centerText(m_list, &font_r15, C_DIM, "");

  lv_obj_t *card = rect(t, 10, 182, 220, 94, 10, C_CARD);
  m_shuffle = text(card, &font_mono, C_DIM, "SHUFFLE", 12, 22);
  makeButton(m_shuffle, ev_shuffle, 0, 10, SOUND_TOGGLE);
  m_time = lv_label_create(card);
  lv_obj_set_style_text_font(m_time, &font_mono, 0);
  lv_obj_set_style_text_color(m_time, C(C_DIM), 0);
  lv_obj_align(m_time, LV_ALIGN_TOP_MID, 0, 11);
  m_repeat = textEnd(card, &font_mono, C_DIM, "REPEAT", 208, 22, 50);
  makeButton(m_repeat, ev_repeat, 0, 10, SOUND_TOGGLE);
  icon(card, 24, 60, LV_SYMBOL_VOLUME_MID, ev_radioVol, -1, &lv_font_montserrat_14);
  icon(card, 66, 60, LV_SYMBOL_PREV, ev_mp3Step, -1);
  m_play = playButton(card, 110, 60, 20, ev_mp3Play);
  icon(card, 154, 60, LV_SYMBOL_NEXT, ev_mp3Step, 1);
  icon(card, 196, 60, LV_SYMBOL_VOLUME_MAX, ev_radioVol, 1, &lv_font_montserrat_14);
  m_prog = bar(card, 12, 86, 196, 3, C_ACCENT);
}

// Build one row per MP3 file (called once, when the SD scan is done).
static void buildMp3Rows() {
  const int n = min(mp3_count(), MP3_MAX_ROWS);
  char name[96], num[8];
  for (int i = 0; i < n; i++) {
    lv_obj_t *r = lv_obj_create(m_list);
    lv_obj_remove_style_all(r);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(r, 214, 28);
    lv_obj_set_style_radius(r, 5, 0);
    lv_obj_set_style_bg_color(r, C(C_CARD), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(r, C(C_ACCENT), 0);
    makeButton(r, ev_mp3Row, i, 0);
    snprintf(num, sizeof num, "%02d", i + 1);
    m_num[i] = text(r, &font_mono, C_DIM, num, 8, 18);
    lv_obj_t *l = text(r, &font_r15, C_TEXT, "", 36, 19);
    lv_obj_set_width(l, 172);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    mp3_name(i, name, sizeof name);
    setTextAuto(l, name, &font_r15);
    m_row[i] = r;
  }
  m_rows = n;
  show(m_empty, n == 0);
  if (n == 0) {
    char msg[120];
    if (media.sdOk)
      snprintf(msg, sizeof msg, "No .mp3 files on the card");
    else snprintf(msg, sizeof msg, "Insert a microSD card");
    setText(m_empty, msg);
    lv_obj_set_style_text_align(m_empty, LV_TEXT_ALIGN_CENTER, 0);
  }
}

void ui_create() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C(C_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  tv = lv_tileview_create(scr);
  lv_obj_set_style_bg_color(tv, C(C_BG), 0);
  lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < TILE_COUNT; i++) {
    lv_dir_t dir = (lv_dir_t)((i > 0 ? LV_DIR_LEFT : 0) | (i < TILE_COUNT - 1 ? LV_DIR_RIGHT : 0));
    tiles[i] = lv_tileview_add_tile(tv, i, 0, dir);
  }
  lv_obj_add_event_cb(tv, ev_tile, LV_EVENT_VALUE_CHANGED, nullptr);

  buildRide(tiles[TILE_RIDE]);
  buildMap(tiles[TILE_MAP]);
  buildPlaying(tiles[TILE_PLAYING]);
  buildRadio(tiles[TILE_RADIO]);
  buildMp3(tiles[TILE_MP3]);
  buildStatusBar();
  buildTabBar();
  ui_update();
}

// ---------------------------------------------------------------- update
static int activeTile() {
  lv_obj_t *a = lv_tileview_get_tile_active(tv);
  for (int i = 0; i < TILE_COUNT; i++)
    if (tiles[i] == a) return i;
  return 0;
}

static void setPlay(lv_obj_t *iconLabel, bool playing) {
  setText(iconLabel, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static uint32_t hrColor(int bpm) {
  if (bpm <= 0) return C_DIM;
  if (bpm < 110) return C_TEXT;
  if (bpm < 130) return C_TEAL;
  if (bpm < 150) return C_OK;
  if (bpm < 170) return C_ACCENT;
  return 0xff5252;
}

static void setHr(lv_obj_t *l) {
  char buf[8];
  if (heart.bpm > 0) snprintf(buf, sizeof buf, "%d", heart.bpm);
  else strcpy(buf, "--");
  setText(l, buf);
  lv_obj_set_style_text_color(l, C(hrColor(heart.bpm)), 0);
}

static void updateMap() {
  char buf[40];
  snprintf(buf, sizeof buf, "TRACK \xC2\xB7 %.2f km", gps.trackKm);
  setText(mp_hdr, buf);
  if (gps.simulated) {
    setText(mp_gps, "SIM GPS");
    lv_obj_set_style_text_color(mp_gps, C(C_WARN), 0);
  } else if (gps.fix) {
    snprintf(buf, sizeof buf, "GPS %d sat", gps.sats);
    setText(mp_gps, buf);
    lv_obj_set_style_text_color(mp_gps, C(C_OK), 0);
  } else {
    setText(mp_gps, "NO FIX");
    lv_obj_set_style_text_color(mp_gps, C(C_ACCENT), 0);
  }

  const int n = gps_trackCount();
  const bool empty = n == 0;
  show(mp_wait, empty);
  show(mp_line, !empty);
  show(mp_head, !empty);
  show(mp_pos, !empty);
  show(mp_start, !empty);
  show(mp_scale, !empty);
  show(mp_scaleTxt, !empty);
  if (empty) return;

  // fit the whole track (north up), at least 150 m across
  float cx, cy, x, y;
  gps_position(cx, cy);
  float minX = cx, maxX = cx, minY = cy, maxY = cy;
  for (int i = 0; i < n; i++) {
    gps_trackPoint(i, x, y);
    minX = min(minX, x); maxX = max(maxX, x);
    minY = min(minY, y); maxY = max(maxY, y);
  }
  const float pad = 16;
  const float scale = min((MAP_W - 2 * pad) / max(maxX - minX, 150.0f),
                          (MAP_H - 2 * pad) / max(maxY - minY, 150.0f));
  const float midX = (minX + maxX) / 2, midY = (minY + maxY) / 2;
  auto sx = [&](float v) { return MAP_W / 2 + (v - midX) * scale; };
  auto sy = [&](float v) { return MAP_H / 2 - (v - midY) * scale; };

  const int step = n / MAP_DRAW_MAX + 1;
  int k = 0;
  for (int i = 0; i < n; i += step) {
    gps_trackPoint(i, x, y);
    mp_pts[k].x = sx(x);
    mp_pts[k].y = sy(y);
    k++;
  }
  mp_pts[k].x = sx(cx);  // always end at the current position
  mp_pts[k].y = sy(cy);
  k++;
  lv_line_set_points(mp_line, mp_pts, k);

  gps_trackPoint(0, x, y);
  lv_obj_set_pos(mp_start, sx(x) - 4, sy(y) - 4);
  const float px = sx(cx), py = sy(cy);
  lv_obj_set_pos(mp_pos, px - 6, py - 6);
  const float h = gps.headingDeg * DEG_TO_RAD;
  mp_headPts[0].x = px;
  mp_headPts[0].y = py;
  mp_headPts[1].x = px + 18 * sinf(h);
  mp_headPts[1].y = py - 18 * cosf(h);
  lv_line_set_points(mp_head, mp_headPts, 2);

  // scale bar: largest "nice" distance that fits in ~70 px
  static const int nice[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
  int m = nice[0];
  for (int v : nice)
    if (v * scale <= 70) m = v;
  mp_scalePts[0].x = 10;
  mp_scalePts[0].y = MAP_H - 10;
  mp_scalePts[1].x = 10 + m * scale;
  mp_scalePts[1].y = MAP_H - 10;
  lv_line_set_points(mp_scale, mp_scalePts, 2);
  if (m >= 1000) snprintf(buf, sizeof buf, "%d km", m / 1000);
  else snprintf(buf, sizeof buf, "%d m", m);
  setText(mp_scaleTxt, buf);
}

void ui_update() {
  char buf[48];
  const int tile = activeTile();

  // status bar
  if (ride.simulated) {
    setText(st_bus, "SIM DATA");
    lv_obj_set_style_text_color(st_bus, C(C_WARN), 0);
  } else {
    setText(st_bus, ride.busOk ? "BUS OK" : "NO BUS");
    lv_obj_set_style_text_color(st_bus, C(ride.busOk ? C_OK : C_ACCENT), 0);
  }
  const char *link = tile == TILE_RADIO ? "Wi-Fi" : tile == TILE_MP3 ? "SD" : tile == TILE_MAP ? "GPS" : "BLE";
  if (ride.clockValid) snprintf(buf, sizeof buf, "%s \xE2\x97\x8F %02u:%02u", link, ride.hour, ride.minute);
  else snprintf(buf, sizeof buf, "%s \xE2\x97\x8F --:--", link);
  setText(st_right, buf);

  // tab bar
  for (int i = 0; i < TILE_COUNT; i++) {
    lv_obj_set_style_text_color(tab_txt[i], C(i == tile ? C_ACCENT : C_DIM), 0);
    show(tab_line[i], i == tile);
  }

  // ride
  snprintf(buf, sizeof buf, "%.1f", ride.speedKph);
  setText(r_speed, buf);
  snprintf(buf, sizeof buf, "%d %%", ride.batteryPct);
  setText(r_bat, buf);
  lv_bar_set_value(r_batBar, ride.batteryPct * 10, LV_ANIM_OFF);
  if (ride.rangeKm < 0) snprintf(buf, sizeof buf, "-- km");  // assist OFF: no limit
  else snprintf(buf, sizeof buf, "%d km", ride.rangeKm);
  setText(r_range, buf);
  if (ride.simulated) {
    setText(r_light, "");
  } else {
    setText(r_light, ride.lightOn ? "LIGHT ON" : "LIGHT OFF");
    lv_obj_set_style_text_color(r_light, C(ride.lightOn ? C_WARN : C_DIM), 0);
  }
  snprintf(buf, sizeof buf, "TRIP %.1f km", ride.tripKm);
  setText(r_trip, buf);
  setHr(r_hr);

  // map page (redrawn only while visible, at most once a second)
  static uint32_t lastMap = 0;
  if (tile == TILE_MAP && millis() - lastMap >= 1000) {
    lastMap = millis();
    updateMap();
  }
  snprintf(buf, sizeof buf, "%.1f", ride.speedKph);
  setText(mp_spd, buf);
  setHr(mp_hr);
  const uint32_t secs = millis() / 1000;
  snprintf(buf, sizeof buf, "%lu:%02lu", (unsigned long)(secs / 3600), (unsigned long)(secs / 60 % 60));
  setText(mp_time, buf);
  for (int i = 0; i < 3; i++) {
    bool on = (int)ride.assist == i;
    lv_obj_set_style_bg_color(r_mode[i], C(C_ACCENT), 0);
    lv_obj_set_style_bg_opa(r_mode[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r_mode[i], on ? 0 : 1, 0);
    lv_obj_set_style_text_color(r_modeTxt[i], C(on ? C_DARK : C_DIM), 0);
  }

  // iPhone music (Apple Media Service)
  const bool hasTrack = phone.ams && phone.title[0];
  float posS = phone.elapsedS;
  if (phone.playing) posS += (millis() - phone.elapsedAtMs) / 1000.0f * (phone.rate > 0 ? phone.rate : 1);
  if (phone.durationS > 0) posS = min(posS, phone.durationS);
  const int permille = phone.durationS > 0 ? (int)(posS * 1000 / phone.durationS) : 0;
  const char *title = hasTrack ? phone.title
                      : phone.ams      ? "Nothing playing"
                      : phone.connected ? "Pairing..."
                                        : "iPhone not connected";
  const char *artist = hasTrack ? phone.artist
                       : phone.connected ? "accept the pairing request"
                                         : "Settings > Bluetooth > E-TUBE Tap";
  setTextAuto(r_title, title, &font_b16);
  if (hasTrack && phone.player[0]) snprintf(buf, sizeof buf, "%s \xC2\xB7 %s", phone.artist, phone.player);
  else snprintf(buf, sizeof buf, "%s", artist);
  setTextAuto(r_artist, buf, &font_r12);
  lv_bar_set_value(r_prog, permille, LV_ANIM_OFF);
  setPlay(r_play, phone.playing);

  // now playing
  snprintf(buf, sizeof buf, "NOW PLAYING%s%s", phone.player[0] ? " \xC2\xB7 " : "", phone.player);
  for (char *c = buf; *c; c++) *c = toupper(*c);
  setText(p_hdr, buf);
  setTextAuto(p_title, title, &font_b22);
  setTextAuto(p_artist, artist, &font_r15);
  setTextAuto(p_album, hasTrack ? phone.album : "", &font_mono);
  setText(p_link, phone.ams ? "iPhone \xC2\xB7 Bluetooth" : phone.connected ? "pairing..." : "not connected");
  lv_obj_set_style_text_color(p_link, C(phone.ams ? C_OK : C_DIM), 0);
  if (phone.volumePct >= 0) snprintf(buf, sizeof buf, "VOL %d%%", phone.volumePct);
  else buf[0] = 0;
  setText(p_vol, buf);
  lv_bar_set_value(p_prog, permille, LV_ANIM_OFF);
  lv_obj_set_x(p_knob, P_PROG_X + P_PROG_W * permille / 1000 - 6);
  fmtTime(buf, sizeof buf, (uint32_t)(posS * 1000));
  setText(p_pos, hasTrack ? buf : "");
  fmtTime(buf, sizeof buf, (uint32_t)(phone.durationS * 1000));
  setText(p_len, hasTrack ? buf : "");
  setPlay(p_play, phone.playing);

  // radio
  for (int i = 0; i < STATION_COUNT; i++) {
    const bool sel = i == media.station, live = sel && media.radioState == RADIO_PLAYING;
    // the playing row shows the song title (if the station sends one) instead of its info
    setTextAuto(rd_info[i], live && media.radioTitle[0] ? media.radioTitle : STATIONS[i].info, &font_mono);
    lv_obj_set_style_text_color(rd_info[i], C(live && media.radioTitle[0] ? C_TEXT : C_DIM), 0);
    lv_obj_set_style_bg_color(rd_row[i], C(sel ? C_SEL : C_CARD), 0);
    lv_obj_set_style_border_width(rd_row[i], sel ? 1 : 0, 0);
    lv_obj_set_style_text_font(rd_name[i], sel ? &font_st16 : &font_st15, 0);
    show(rd_live[i], live);
    show(rd_fmt[i], !live);  // the LIVE pill takes the format's place
  }
  lv_bar_set_value(rd_buf, media.bufferPct * 10, LV_ANIM_OFF);
  if (media.radioVolume > 21) snprintf(buf, sizeof buf, "BOOST +%d", (media.radioVolume - 21) * 2);
  else snprintf(buf, sizeof buf, "VOL %d", media.radioVolume);
  setText(rd_vol, buf);
  lv_obj_set_style_text_color(rd_vol, C(media.radioVolume > 21 ? C_ACCENT : C_TEXT), 0);
  lv_obj_align(rd_vol, LV_ALIGN_TOP_MID, 0, 10);
  setPlay(rd_play, radioActive());
  static const char *const stateTxt[] = {"stopped", "no Wi-Fi", "connecting...", "playing", "stream error"};
  const char *st = !media.wifiOk && media.radioState == RADIO_IDLE ? "Wi-Fi..."
                   : media.radioState == RADIO_ERROR && media.radioOffline ? "station offline"
                                                                            : stateTxt[media.radioState];
  setText(rd_status, st);
  lv_obj_set_style_text_color(rd_status, C(media.radioState == RADIO_PLAYING ? C_OK
                                           : media.radioState >= RADIO_ERROR ? C_ACCENT : C_DIM), 0);

  // mp3
  if (media.sdScanned && media.libGen != m_gen) {  // new scan (boot, card inserted, upload)
    m_gen = media.libGen;
    lv_obj_clean(m_list);
    m_empty = centerText(m_list, &font_r15, C_DIM, "");
    m_sel = -1;
    buildMp3Rows();
  }
  if (!media.sdScanned) setText(m_hdr, "MP3 \xC2\xB7 reading SD...");
  else if (!media.sdOk) setText(m_hdr, "MP3 \xC2\xB7 no SD card");
  else {
    snprintf(buf, sizeof buf, "MP3 \xC2\xB7 microSD \xC2\xB7 %d TRACKS", media.mp3LibraryCount);
    setText(m_hdr, buf);
  }
  if (m_rows > 0 && media.mp3Track != m_sel && media.mp3Track < m_rows) {
    if (m_sel >= 0 && m_sel < m_rows) {  // un-highlight the old row
      lv_obj_set_style_bg_color(m_row[m_sel], C(C_CARD), 0);
      lv_obj_set_style_border_width(m_row[m_sel], 0, 0);
      lv_obj_set_style_text_color(m_num[m_sel], C(C_DIM), 0);
    }
    m_sel = media.mp3Track;
    lv_obj_set_style_bg_color(m_row[m_sel], C(C_SEL), 0);
    lv_obj_set_style_border_width(m_row[m_sel], 1, 0);
    lv_obj_set_style_text_color(m_num[m_sel], C(C_ACCENT), 0);
    lv_obj_scroll_to_view(m_row[m_sel], LV_ANIM_ON);
  }
  if (media.mp3Playing) {
    char a[8], b[8];
    fmtTime(a, sizeof a, media.mp3PosS * 1000);
    fmtTime(b, sizeof b, media.mp3DurS * 1000);
    snprintf(buf, sizeof buf, "%s / %s", a, b);
  } else if (media.radioVolume > 21) snprintf(buf, sizeof buf, "BOOST +%d dB", (media.radioVolume - 21) * 2);
  else snprintf(buf, sizeof buf, "VOL %d", media.radioVolume);
  setText(m_time, buf);
  lv_obj_align(m_time, LV_ALIGN_TOP_MID, 0, 11);
  lv_bar_set_value(m_prog, media.mp3DurS ? (int)(media.mp3PosS * 1000 / media.mp3DurS) : 0, LV_ANIM_OFF);
  lv_obj_set_style_text_color(m_shuffle, C(media.shuffle ? C_ACCENT : C_DIM), 0);
  lv_obj_set_style_text_color(m_repeat, C(media.repeat ? C_ACCENT : C_DIM), 0);
  setPlay(m_play, media.mp3Playing && !media.mp3Paused);
}
