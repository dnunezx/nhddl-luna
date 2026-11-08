#ifndef LUNA_UI_CONFIG_H
#define LUNA_UI_CONFIG_H

/* Loaded once beside the launcher. Runtime settings never overwrite ui.cfg. */
typedef struct {
  unsigned start_ring_fade_ms;
  unsigned start_label_delay_ms;
  unsigned start_label_fade_ms;
  unsigned start_close_ms;
  unsigned start_orb_brightness_percent;
  unsigned start_burst_brightness_percent;
  unsigned menu_open_ms;
  unsigned menu_close_ms;
  unsigned submenu_open_ms;
  unsigned submenu_close_ms;
  unsigned menu_move_ms;
} LunaUIConfig;
extern LunaUIConfig lunaUIConfig;
void lunaUIConfigure(const char *directory);
/* Advance authored animation time; zero duration completes on the next frame. */
float lunaUITimeStep(float elapsed, unsigned duration, float authoredDuration);
float lunaUIProgress(float elapsed, unsigned duration);
#endif
