"""Run the actual cube/ring animation models at default and extreme timings."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
models = []
for name in ('cube_menu', 'ring_menu'):
    source = (root / f'src/ui/{name}.c').read_text()
    models.append(source[:source.index('#include "ui/view_internal.h"')]
                  .replace('#include "ui/underground.h"', ''))
tests = r'''
#include <assert.h>
int main(void) {
  const unsigned durations[]={0,1,16,250,1440,5000};
  for(unsigned i=0;i<sizeof(durations)/sizeof(durations[0]);i++) {
    unsigned duration=durations[i];
    lunaUIConfig.submenu_open_ms=duration;
    lunaUIConfig.submenu_close_ms=duration;
    lunaUIConfig.menu_open_ms=duration;
    lunaUIConfig.menu_close_ms=duration;
    lunaUIConfig.start_close_ms=duration;
    for(int fps=50;fps<=60;fps+=10) {
      CubeMenu cube; RingMenu ring;
      cubeInit(&cube,0,0,fps); ringInit(&ring,0,0);
      cube.submenuOpen=1;cube.submenuDirection=1;ring.closing=1;
      uint32_t now=0, step=fps==50?20:16;
      do {now+=step;cubeStep(&cube,now);ringStep(&ring,now);} while(now<duration+step);
      assert(cube.submenuAge==CUBE_SUBMENU_MS && !cube.submenuDirection);
      assert(cube.entry==500 && ring.exit==2000);
      cube.submenuDirection=-1;
      uint32_t end=now+duration;
      do {now+=step;cubeStep(&cube,now);} while(now<end+step);
      assert(!cube.submenuOpen && !cube.submenuDirection && cube.submenuAge==0);
      cubeClose(&cube,0);end=now+duration;
      do {now+=step;cubeStep(&cube,now);} while(now<end+step);
      assert(cube.exit>=500);
    }
  }
  puts("PASS: production animation models, zero/short/max durations, PAL/NTSC cadence");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'animation.c'
    c.write_text('\n'.join(models) + tests)
    exe = Path(tmp) / 'animation'
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-fsanitize=undefined',
                    '-I' + str(root / 'include'), '-I' + str(root / 'src/ui'),
                    str(c), str(root / 'src/ui/ui_config.c'), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
