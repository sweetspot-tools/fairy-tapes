// Renders two images of each plugin's HTML preview into src/<plugin>/docs/images/:
//   panel.png    the front panel, as the README shows it (default wear, meters running);
//   routing.png  the ROUTING tab of the back, with wear off so scratches and stains do not cover the lines.
//
// Usage, from the repository root:
//   python3 -m http.server 8765            (in another terminal)
//   node docs/mockups/render_previews.js http://localhost:8765/ [panel|routing]   (both when omitted)
// Needs Playwright with Chromium, and Pillow (python3) to scale the images down.
const { chromium } = require('playwright');
const { execFileSync } = require('child_process');
const path = require('path');

const PREVIEWS = {
  'the-stripper': 'src/the-stripper/docs/mockups/stripper_rack.html',
  'pillow-talk': 'src/pillow-talk/docs/mockups/pillow_rack.html',
  'hotline': 'src/hotline/docs/mockups/hotline_rack.html',
  'deep-throat': 'src/deep-throat/docs/mockups/deep_throat_rack.html',
  'happy-ending': 'src/happy-ending/docs/mockups/happy_ending_rack.html',
};
const WEAR_KEYS = ['st_wear', 'pt_wear', 'dt_wear', 'he_wear'];
const WIDTH = 1600;        // routing diagrams
const PANEL_WIDTH = 1200;  // front panels
const ROOT = path.join(__dirname, '..', '..');

(async () => {
  const base = process.argv[2] || 'http://localhost:8765/';
  // The previews load the tape fonts from Google Fonts: go through the proxy when there is one.
  const server = process.env.HTTPS_PROXY || process.env.https_proxy;
  const browser = await chromium.launch(server ? { proxy: { server, bypass: 'localhost,127.0.0.1' } } : {});
  const ctx = await browser.newContext({ viewport: { width: 1400, height: 1600 }, deviceScaleFactor: 2 });
  const only = process.argv[3];
  const shrink = file => execFileSync('python3', ['-c',
    'import sys;from PIL import Image;f=sys.argv[1];w=int(sys.argv[2]);im=Image.open(f).convert("RGB");'
    + 'im=im.resize((w,round(im.height*w/im.width)),Image.LANCZOS);'
    // 48-colour palette for the diagrams (white, amber, panel black): about 150 KB; the panel keeps full colour
    + 'im=(im.quantize(colors=48,method=Image.Quantize.FASTOCTREE,dither=Image.Dither.NONE) if sys.argv[3]=="1" else im);'
    + 'im.save(f,optimize=True)',
    file, String(file.endsWith('routing.png') ? WIDTH : PANEL_WIDTH), file.endsWith('routing.png') ? '1' : '0']);
  for (const [name, page] of Object.entries(PREVIEWS)) {
    const dir = path.join(ROOT, 'src', name, 'docs', 'images');
    execFileSync('mkdir', ['-p', dir]);
    if (only !== 'routing') {
      const p = await ctx.newPage();
      await p.goto(base + page, { waitUntil: 'networkidle' });
      await p.evaluate(() => document.fonts.ready);
      await p.waitForTimeout(1500); // let the meters and the scope draw a few frames
      await p.addStyleTag({ content: '.bar{display:none!important}' });
      const file = path.join(dir, 'panel.png');
      await (await p.$('#ui')).screenshot({ path: file });
      shrink(file);
      console.log(file);
      await p.close();
    }
    if (only !== 'panel') {
      const p = await ctx.newPage();
      await p.addInitScript(keys => { for (const k of keys) localStorage.setItem(k, 'off'); }, WEAR_KEYS);
      await p.goto(base + page, { waitUntil: 'networkidle' });
      await p.evaluate(() => document.fonts.ready);
      await p.addStyleTag({ content: '.bar{display:none!important}' });
      await p.evaluate(() => { showTab('routing'); document.getElementById('card').classList.add('flipped'); });
      await p.waitForTimeout(1500); // the flip takes about 0.8 s
      const file = path.join(dir, 'routing.png');
      await (await p.$('#back')).screenshot({ path: file });
      shrink(file);
      console.log(file);
      await p.close();
    }
  }
  await browser.close();
})();
