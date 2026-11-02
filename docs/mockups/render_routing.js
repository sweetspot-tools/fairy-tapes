// Renders the ROUTING tab of each plugin's HTML preview to src/<plugin>/docs/images/routing.png.
// The diagrams are drawn with wear off, so scratches and stains do not cover the lines.
//
// Usage, from the repository root:
//   python3 -m http.server 8765            (in another terminal)
//   node docs/mockups/render_routing.js http://localhost:8765/
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
const WIDTH = 1600;
const ROOT = path.join(__dirname, '..', '..');

(async () => {
  const base = process.argv[2] || 'http://localhost:8765/';
  // The previews load the tape fonts from Google Fonts: go through the proxy when there is one.
  const server = process.env.HTTPS_PROXY || process.env.https_proxy;
  const browser = await chromium.launch(server ? { proxy: { server, bypass: 'localhost,127.0.0.1' } } : {});
  const ctx = await browser.newContext({ viewport: { width: 1400, height: 1600 }, deviceScaleFactor: 2 });
  for (const [name, page] of Object.entries(PREVIEWS)) {
    const p = await ctx.newPage();
    await p.addInitScript(keys => { for (const k of keys) localStorage.setItem(k, 'off'); }, WEAR_KEYS);
    await p.goto(base + page, { waitUntil: 'networkidle' });
    await p.evaluate(() => document.fonts.ready);
    await p.addStyleTag({ content: '.bar{display:none!important}' });
    await p.evaluate(() => { showTab('routing'); document.getElementById('card').classList.add('flipped'); });
    await p.waitForTimeout(1500); // the flip takes about 0.8 s
    const file = path.join(ROOT, 'src', name, 'docs', 'images', 'routing.png');
    execFileSync('mkdir', ['-p', path.dirname(file)]);
    await (await p.$('#back')).screenshot({ path: file });
    execFileSync('python3', ['-c',
      'import sys;from PIL import Image;f=sys.argv[1];w=int(sys.argv[2]);im=Image.open(f).convert("RGB");'
      + 'im=im.resize((w,round(im.height*w/im.width)),Image.LANCZOS);'
      // 48-colour palette: keeps white, amber and the panel black, and the file stays near 150 KB
      + 'im.quantize(colors=48,method=Image.Quantize.FASTOCTREE,dither=Image.Dither.NONE).save(f,optimize=True)',
      file, String(WIDTH)]);
    console.log(file);
    await p.close();
  }
  await browser.close();
})();
