/* Each profile has independent overrides; editing an inactive profile leaves
 * the current presentation alone. Saving uses Appearance's verified record. */
static const struct {
  const char *label;
  LunaAnimation timing;
} appearanceAnimationTimings[] = {
  {"Start entrance", LUNA_ANIM_START_ENTRY},
  {"Start departure", LUNA_ANIM_START_EXIT},
  {"Start selection", LUNA_ANIM_START_MOVE},
  {"Menu entrance", LUNA_ANIM_MENU_ENTRY},
  {"Menu departure", LUNA_ANIM_MENU_EXIT},
  {"Menu selection", LUNA_ANIM_MENU_MOVE},
  {"View transitions", LUNA_ANIM_VIEW_CHANGE},
  {"Dialogs and overlays", LUNA_ANIM_DIALOG},
};
enum {
  AP_ANIM_EFFECTS = sizeof(appearanceAnimationTimings) / sizeof(*appearanceAnimationTimings),
  AP_ANIM_BLINK,
  AP_ANIM_RESTORE,
  AP_ANIM_ROWS
};

static const char *appearanceAnimationLabel(AppearanceEditor *s,int row) {
  static const char *const labels[]={"Decorative effects","Pulsing and blinking","Restore defaults"};
  if(!s->animationEditor)return (const char *const[]){"Animation Mode","Customize Cinematic","Customize Instant"}[row];
  return row<AP_ANIM_EFFECTS?appearanceAnimationTimings[row].label:labels[row-AP_ANIM_EFFECTS];
}
static const char *appearanceAnimationValue(AppearanceEditor *s,int row) {
  if(!s->animationEditor)return row?"Open":s->pending.animations.mode==LUNA_ANIMATION_CINEMATIC?"Cinematic":"Instant";
  const LunaAnimationProfile *p=&s->pending.animations.profiles[s->animationEditor-1];
  if(row<AP_ANIM_EFFECTS)return (const char *const[]){"Instant","25%","50%","75%","Original","150%","200%"}[p->timing[appearanceAnimationTimings[row].timing]];
  if(row==AP_ANIM_EFFECTS)return (const char *const[]){"Off","Reduced","Full","25%"}[p->effects];
  if(row==AP_ANIM_BLINK)return p->blink?"On":"Off";
  return "Press X";
}
static const char *appearanceAnimationDescription(AppearanceEditor *s,int row) {
  if(!s->animationEditor)return row?
    "Customize this profile independently. Changes save automatically.":
    "Cinematic starts at original timing. Instant starts with shorter menu animations and quieter effects.";
  if(row<AP_ANIM_EFFECTS)return "Choose a percentage of the original duration, or Instant. Input repeat rates stay unchanged.";
  if(row==AP_ANIM_EFFECTS)return "Choose the intensity of decorative orbs, particles, ripples and shimmer.";
  if(row==AP_ANIM_BLINK)return "Enable or disable decorative pulses and blinking. Status information stays visible.";
  return "Press X to restore this profile's original defaults.";
}
static int appearanceAnimationCanAdjust(AppearanceEditor *s,int row,int direction) {
  return s->animationEditor?(row<AP_ANIM_RESTORE||!direction):row==0;
}
static void appearanceAnimationAdjust(AppearanceEditor *s,LunaAppearance *a,int row,int direction) {
  if(!s->animationEditor) {a->animations.mode^=1;return;}
  LunaAnimationProfile *p=&a->animations.profiles[s->animationEditor-1];
  if(row==AP_ANIM_RESTORE) {lunaAnimationResetProfile(&a->animations,s->animationEditor-1);return;}
  uint32_t *value;
  unsigned count;
  if(row<AP_ANIM_EFFECTS) {value=&p->timing[appearanceAnimationTimings[row].timing];count=LUNA_ANIMATION_TIMING_COUNT;}
  else if(row==AP_ANIM_EFFECTS) {value=&p->effects;count=4;}
  else {value=&p->blink;count=2;}
  *value=(*value+count+direction)%count;
}
