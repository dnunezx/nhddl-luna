"""Exercise the production hub loop/model with scripted pad frames (host gcc)."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
source = (root / 'src/ui/cube_menu.c').read_text()
model = source[:source.index('#include "ui/view_internal.h"')]
model = model.replace('#include "ui/underground.h"', '')
loop = source[source.index('int uiCubeHubLoop('):source.index('int uiCubeSettingsLoop(')]
harness = r'''
#include <assert.h>
#include <stdlib.h>
#define PAD_CROSS 1
#define PAD_CIRCLE 2
#define PAD_TRIANGLE 4
#define PAD_START 8
#define PAD_UP 16
#define PAD_DOWN 32
#define PAD_LEFT 64
#define PAD_RIGHT 128
#define PAD_L1 256
#define PAD_R1 512
#define GS_MODE_PAL 50
static struct {int Mode;} gs = {60}, *gsGlobal=&gs;
static CubeMenu *activeCubeMenu;
static const LunaCubeHub *activeCubeHub;
static const LunaCubeSubmenu *activeCubeSubmenu;
static const char *activeCubeStatus;
static int activeCubeConfirm;
static int frame, scenario, begins, ends, moves, duringEntry;
static int script[256];
static uint32_t uiNowMs(void) {assert(frame<250);return (uint32_t)frame++*16;}
static int pollInput(void) {return script[frame-1];}
void uiCubePanelGlassClose(void) {}
static void cubeMaterialInit(void) {}
static void cubeHubClose(void) {activeCubeMenu=NULL;}
static int cubeDraw(CubeMenu *m,const char *s,int c,const LunaCubeHub *h,const LunaCubeSubmenu *sub) {
  (void)m;(void)s;(void)c;(void)h;(void)sub;
  (void)cubeSmooth(0);(void)cubeRotation(0,0);return 1;
}
#define gsKit_queue_exec(x) ((void)(x))
#define gsKit_finish() ((void)0)
#define gsKit_sync_flip(x) ((void)(x))
#define usleep(x) ((void)(x))
static void begin(void *context) {(void)context;begins++;}
static void end(void *context) {(void)context;ends++;assert(ends<=begins);}
static void draw(void *context,float alpha) {(void)context;(void)alpha;}
static int input(void *context,int buttons) {
  (void)context;
  if(buttons&PAD_DOWN) {moves++; if(activeCubeMenu->submenuDirection>0) duringEntry++;}
  if(buttons&PAD_CIRCLE) return LUNA_CUBE_SUBMENU_BACK;
  return LUNA_CUBE_SUBMENU_STAY;
}
'''
tests = r'''
int main(void) {
  const char *labels[]={"one","two"};
  LunaCubeSubmenu submenus[2]={{0,begin,draw,input,end},{0,begin,draw,input,end}};
  LunaCubeHub hub={.labels=labels,.count=2,.submenus=submenus,.submenuCount=2,.confirmRow=-1};
  for(scenario=0;scenario<3;scenario++) {
    frame=begins=ends=moves=duringEntry=0;memset(script,0,sizeof(script));
    script[1]=PAD_CROSS;
    script[2]=PAD_DOWN;script[3]=PAD_DOWN;
    script[5]=PAD_CIRCLE;
    if(scenario==0) {
      script[6]=PAD_DOWN;
      script[7]=PAD_CROSS;
      script[8]=PAD_DOWN;
      script[10]=PAD_CIRCLE;
      script[11]=PAD_CIRCLE;
      script[120]=PAD_CIRCLE;
    } else if(scenario==1) {
      script[6]=PAD_CIRCLE;
      script[120]=PAD_CIRCLE;
    } else {
      script[7]=PAD_CIRCLE;
    }
    int selected=0;
    assert(uiCubeHubLoop(&selected,NULL,&hub)==-1);
    assert(duringEntry==(scenario==0?2:1));
    assert(moves==(scenario==0?2:1));
    assert(begins==(scenario==0?2:1) && ends==begins);
    assert(selected==(scenario==0?1:0));
    if(scenario!=2)assert(frame>=120);
  }
  puts("PASS: opening navigation, holds, rapid Back/reopen, parent navigation, teardown");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'menu.c'
    c.write_text(model + harness + loop + tests)
    exe = Path(tmp) / 'menu'
    command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
               '-I' + str(root / 'include'), str(c)]
    if (root / 'src/ui/ui_config.c').exists():
        command.append(str(root / 'src/ui/ui_config.c'))
    subprocess.run(command + ['-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
