/* Each profile has independent overrides; editing an inactive profile leaves
 * the current presentation alone. Saving uses Appearance's verified record. */
static const char *appearanceAnimationLabel(AppearanceEditor *s,int row) {
  static const char *const labels[]={"Start entrance","Start departure","Start selection",
    "Menu entrance","Menu departure","Menu selection","Library movement",
    "View transitions","Artwork motion","Dialogs and overlays","Startup and launch",
    "Background motion","Decorative effects","Pulsing and blinking","Restore defaults"};
  if(!s->animationEditor)return (const char *const[]){"Animation Mode","Customize Cinematic","Customize Instant"}[row];
  return labels[row];
}
static const char *appearanceAnimationValue(AppearanceEditor *s,int row) {
  if(!s->animationEditor)return row?"Open":s->pending.animations.mode==LUNA_ANIMATION_CINEMATIC?"Cinematic":"Instant";
  const LunaAnimationProfile *p=&s->pending.animations.profiles[s->animationEditor-1];
  if(row<LUNA_ANIM_COUNT)return (const char *const[]){"Instant","25%","50%","75%","Original","150%","200%"}[p->timing[row]];
  if(row==LUNA_ANIM_COUNT)return (const char *const[]){"Off","Slow","Original","25%"}[p->background];
  if(row==LUNA_ANIM_COUNT+1)return (const char *const[]){"Off","Reduced","Full","25%"}[p->effects];
  if(row==LUNA_ANIM_COUNT+2)return p->blink?"On":"Off";
  return "Press X";
}
static const char *appearanceAnimationDescription(AppearanceEditor *s,int row) {
  if(!s->animationEditor)return row?
    "Customize this profile independently. Changes save automatically.":
    "Cinematic defaults to the original presentation. Instant defaults to 25% durations, motion and effects.";
  if(row<LUNA_ANIM_COUNT)return "Choose a percentage of the original duration, or Instant. Input repeat rates stay unchanged.";
  if(row==LUNA_ANIM_COUNT)return "Pause animated backgrounds, slow them down, or keep their original speed.";
  if(row==LUNA_ANIM_COUNT+1)return "Choose the intensity of decorative orbs, particles, ripples and shimmer.";
  if(row==LUNA_ANIM_COUNT+2)return "Enable or disable decorative pulses and blinking. Status information stays visible.";
  return "Press X to restore this profile's original defaults.";
}
static int appearanceAnimationCanAdjust(AppearanceEditor *s,int row,int direction) {
  return s->animationEditor?(row<LUNA_ANIM_COUNT+3||!direction):row==0;
}
static void appearanceAnimationAdjust(AppearanceEditor *s,LunaAppearance *a,int row,int direction) {
  if(!s->animationEditor) {a->animations.mode^=1;return;}
  LunaAnimationProfile *p=&a->animations.profiles[s->animationEditor-1];
  if(row==LUNA_ANIM_COUNT+3) {lunaAnimationResetProfile(&a->animations,s->animationEditor-1);return;}
  uint32_t *value;
  unsigned count;
  if(row<LUNA_ANIM_COUNT) {value=&p->timing[row];count=LUNA_ANIMATION_TIMING_COUNT;}
  else if(row==LUNA_ANIM_COUNT) {value=&p->background;count=4;}
  else if(row==LUNA_ANIM_COUNT+1) {value=&p->effects;count=4;}
  else {value=&p->blink;count=2;}
  *value=(*value+count+direction)%count;
}
