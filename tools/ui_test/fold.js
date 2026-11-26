// Browsertest: moduler kan foldes sammen, og det huskes. Kræver mock_api.py med "1".
const { chromium } = require('playwright');
(async () => {
  const D = process.argv[2];
  const b = await chromium.launch();
  const fejl = [];
  const p = await (await b.newContext({ viewport: { width: 1440, height: 900 } })).newPage();
  p.on('pageerror', e => fejl.push(e.message));
  await p.goto('http://127.0.0.1:8766/');
  await p.fill('#l-kode', 'hemmelig123'); await p.click('#login-form button'); await p.waitForTimeout(800);
  console.log('Enheds-ID i sidepanelet (skal være false):', await p.locator('.side').innerText().then(t => /ID a1b2c3/.test(t)));
  await p.click('[data-fold="i1"]'); await p.click('[data-fold="u2"]'); await p.waitForTimeout(200);
  console.log('Inputmodul 1 foldet, rækker skjult:', !(await p.isVisible('.indmodul[data-im="1"] .raekker')));
  console.log('Outputmodul 2 foldet, LED\'er stadig synlige:', !(await p.isVisible('.skinne[data-modul="2"] .kanaler')),
    await p.isVisible('.skinne[data-modul="2"] .leds'));
  await p.screenshot({ path: D + '/fold.png' });
  await p.reload(); await p.waitForTimeout(800);
  console.log('Huskes efter genindlæsning:', !(await p.isVisible('.indmodul[data-im="1"] .raekker')));
  await p.click('[data-fold="i1"]'); await p.waitForTimeout(100);
  console.log('Foldet ud igen:', await p.isVisible('.indmodul[data-im="1"] .raekker'));
  await p.goto('http://127.0.0.1:8766/#diagnose'); await p.waitForTimeout(800);
  console.log('Enheds-ID under Diagnose:', await p.locator('#d-tal').innerText().then(t => /a1b2c3/.test(t)));
  console.log(fejl.length ? 'FEJL: ' + fejl.join('; ') : 'Ingen JavaScript-fejl');
  await b.close();
})();
