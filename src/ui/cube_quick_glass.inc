/* Screen-space CubeGlass panels share materials with an active crystal hub. */
static int cubeQuickGlassActive;
static int cubePanelGlassActive;
static GlassColorPreset cubeQuickGlassTheme;

void uiCubePanelGlassClose(void) {
  if (!cubePanelGlassActive) return;
  cubeMaterialClose();
  cubePanelGlassActive = 0;
  cubeQuickGlassActive = 0;
}

void uiCubeQuickMenuGlassClose(void) {
  if (cubeQuickGlassActive) uiCubePanelGlassClose();
}

static int cubePanelGlassDraw(int left, int top, int right, int bottom,
                              int z, int fillAlpha) {
  if (right <= left || bottom <= top || left >= gsGlobal->Width ||
      top >= gsGlobal->Height || right <= 0 || bottom <= 0)
    return 0;
  const GlassColorPreset theme = getGlassColorPreset();
  if (!activeCubeMenu && (!cubePanelGlassActive || theme != cubeQuickGlassTheme)) {
    uiCubePanelGlassClose();
    cubeMaterialInit();
    cubePanelGlassActive = 1;
    cubeQuickGlassTheme = theme;
    // Standalone panels use the UI palette. Nested sheets borrow the hub's
    // neutral maps without changing its material pixels or texture ownership.
    if (theme != GLASS_COLOR_ORIGINAL) {
      for (int map = 0; map < 4; map++) {
        for (int pixel = 0; pixel < 4096; pixel++) {
          const u32 rgba = cubeIcePixels[map][pixel];
          cubeIcePixels[map][pixel] = (u32)glassPresetColor(
              rgba & 255, (rgba >> 8) & 255, (rgba >> 16) & 255, rgba >> 24);
        }
      }
    }
  }
  // A background change may have recreated the scratch pages since init.
  cubeWorkReady = uiBorrowBackgroundWorkPages(cubeWork);

  const float x1 = left * 640.0f / gsGlobal->Width;
  const float y1 = top * 480.0f / gsGlobal->Height;
  const float x2 = right * 640.0f / gsGlobal->Width;
  const float y2 = bottom * 480.0f / gsGlobal->Height;
  const float depth = 4.0f, frontScale = (500.0f - depth * .5f) / 500.0f;
  CubeGlass scene = {0};
  scene.position = (CubeVec){(320 - (x1 + x2) * .5f) * frontScale,
                            (240 - (y1 + y2) * .5f) * frontScale, 0};
  scene.rotation = cubePrepareRotation((CubeVec){0, 0, 0});
  scene.left = fmaxf(0, x1);
  scene.top = fmaxf(0, y1);
  scene.width = fminf(640, x2) - scene.left;
  scene.height = fminf(480, y2) - scene.top;
  scene.depth = 500;
  scene.fade = scene.highlight = 1;
  const CubeVec size = {(x2 - x1) * frontScale, (y2 - y1) * frontScale, depth};
  for (int f = 0; f < 6; f++) {
    CubeGlassFace *face = &scene.face[f];
    const float *normal = cubeUnitMesh[f][0] + 3;
    face->normal = (CubeVec){normal[0], normal[1], normal[2]};
    CubeVec center = {0};
    for (int j = 0; j < 4; j++) {
      const float *vertex = cubeUnitMesh[f][j];
      face->v[j] = (CubeVec){scene.position.x + size.x * vertex[0],
                             scene.position.y + size.y * vertex[1],
                             size.z * vertex[2]};
      center.x += face->v[j].x * .25f;
      center.y += face->v[j].y * .25f;
      center.z += face->v[j].z * .25f;
    }
    face->front = -face->normal.x * center.x - face->normal.y * center.y -
                   face->normal.z * (500 + center.z);
    if (face->front > 0) scene.visible |= 1U << f;
  }

  GSTEST savedTest = *gsGlobal->Test;
  const uint64_t savedBlend = gsGlobal->PrimAlpha;
  const int savedAlpha = gsGlobal->PrimAlphaEnable, savedPabe = gsGlobal->PABE;
  const float savedScale = cubeSceneScale, savedX = cubeSceneOffsetX, savedY = cubeSceneOffsetY;
  cubeSceneScale = 1;
  cubeSceneOffsetX = cubeSceneOffsetY = 0;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  if (fillAlpha > 0 || theme != GLASS_COLOR_ORIGINAL) {
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_prim_sprite(gsGlobal, left, top, right, bottom, z,
        glassPresetColor(5, 13, 34, fillAlpha > 0 ? fillAlpha :
                         theme == GLASS_COLOR_BLACK ? 0x48 : 0x28));
  }
  if (cubeWorkReady) cubeGlassDraw(&scene, (CubeVec){0, 0, 0});
  else {
    // Keep the same slab, gloss and contour when refraction scratch is absent.
    // An opaque-enough base keeps instructions readable in this reduced pass.
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_prim_sprite(gsGlobal, left, top, right, bottom, z,
        glassPresetColor(5, 13, 34, 0x68));
    cubeRenderScene = &scene;
    cubeTarget(-1);
    cubeGlassFaces(&scene, (CubeVec){0, 0, 0}, CUBE_GLOSS, (CubeVec){0, 0, 0}, 1);
    cubeGlassHighlight(&scene, (CubeVec){0, 0, 0});
  }
  cubeRestoreGlassState();
  cubeSceneScale = savedScale;
  cubeSceneOffsetX = savedX;
  cubeSceneOffsetY = savedY;
  gsGlobal->PrimAlphaEnable = savedAlpha;
  gsKit_set_primalpha(gsGlobal, savedBlend, savedPabe);
  *gsGlobal->Test = savedTest;
  gsKit_set_test(gsGlobal, savedTest.ATE ? GS_ATEST_ON : GS_ATEST_OFF);
  return 1;
}

void uiCubePanelGlassDraw(int left, int top, int right, int bottom, int z, int fillAlpha) {
  cubePanelGlassDraw(left, top, right, bottom, z, fillAlpha);
}

int uiCubeQuickMenuGlassDraw(int left, int top, int right, int bottom) {
  int drawn = cubePanelGlassDraw(left, top, right, bottom, 0, 0);
  if (drawn && !activeCubeMenu) cubeQuickGlassActive = 1;
  return drawn;
}
