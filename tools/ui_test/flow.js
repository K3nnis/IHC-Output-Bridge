// Browsertest af web-UI'et mod tools/ui_test/mock_api.py. Se README.md.
const { chromium } = require('playwright');
(async () => {
  const D = process.argv[2];
  const b = await chromium.launch();
  const fejl = [];
  async function side(opt) {
    const ctx = await b.newContext(opt); const p = await ctx.newPage();
    p.on('pageerror', e => fejl.push('JS-fejl: ' + e.message));
    p.on('console', m => { if (m.type() === 'error' && !/401|status of 4/.test(m.text())) fejl.push('console: ' + m.text()); });
    return p;
  }
  // Desktop
  const p = await side({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1 });
  await p.goto('http://127.0.0.1:8766/'); await p.waitForTimeout(300);
  await p.screenshot({ path: D + '/d0-login.png' });
  await p.fill('#l-kode', 'forkert'); await p.click('#login-form button'); await p.waitForTimeout(300);
  console.log('Forkert kode:', await p.textContent('#l-besked'));
  await p.fill('#l-kode', 'hemmelig123'); await p.click('#login-form button'); await p.waitForTimeout(1200);
  await p.screenshot({ path: D + '/d1-oversigt.png' });
  const foer = await p.locator('.kanal[data-m="1"][data-k="2"]').getAttribute('aria-checked');
  await p.click('.kanal[data-m="1"][data-k="2"]'); await p.waitForTimeout(400);
  console.log('Udgang 1.2 før/efter klik:', foer, await p.locator('.kanal[data-m="1"][data-k="2"]').getAttribute('aria-checked'));
  const ind1 = await p.locator('li[data-im="1"][data-ik="9"] .pille').textContent(); await p.waitForTimeout(1600);
  console.log('Indgang 1.9 live (skifter hvert 1,5 s):', ind1, '->', await p.locator('li[data-im="1"][data-ik="9"] .pille').textContent());
  await p.fill('#soeg', 'spots'); await p.waitForTimeout(200);
  console.log('Søgning "spots" synlige udgange:', await p.locator('.kanal:not(.skjult-soeg)').count());
  await p.screenshot({ path: D + '/d2-soeg.png' }); await p.fill('#soeg', '');
  for (const v of ['kanaler', 'regler', 'netvaerk', 'firmware', 'diagnose', 'indstillinger']) {
    await p.goto('http://127.0.0.1:8766/#' + v); await p.waitForTimeout(700);
    if (v === 'kanaler') await p.click('details.modulboks summary');
    await p.waitForTimeout(200);
    await p.screenshot({ path: D + `/d-${v}.png`, fullPage: true });
  }
  await p.goto('http://127.0.0.1:8766/#regler'); await p.waitForTimeout(500);
  await p.selectOption('#r-ind', '1.8'); await p.selectOption('#r-ud', '2.8'); await p.click('#r-tilfoej'); await p.waitForTimeout(500);
  console.log('Regler efter tilføj:', await p.locator('.regel').count());
  await p.click('.regel .fare >> nth=0'); await p.waitForTimeout(500);
  console.log('Regler efter slet:', await p.locator('.regel').count());
  await p.goto('http://127.0.0.1:8766/#kanaler'); await p.waitForTimeout(500);
  await p.click('details.modulboks summary'); const inp = p.locator('tr[data-k="1"] input').first();
  await inp.fill('Stue lampe'); await inp.press('Tab'); await p.waitForTimeout(400);
  console.log('Kanal gemt:', await p.locator('tr[data-k="1"] .st').first().textContent());
  // Mobil
  const m = await side({ ...require('playwright').devices['iPhone 13'] });
  await m.goto('http://127.0.0.1:8766/'); await m.waitForTimeout(300);
  await m.fill('#l-kode', 'hemmelig123'); await m.click('#login-form button'); await m.waitForTimeout(1200);
  await m.screenshot({ path: D + '/m1-oversigt.png' });
  await m.click('#menuknap'); await m.waitForTimeout(400);
  await m.screenshot({ path: D + '/m2-menu.png' });
  await m.click('#nav a[href="#regler"]'); await m.waitForTimeout(600);
  await m.screenshot({ path: D + '/m3-regler.png', fullPage: true });
  console.log('Vandret scroll på mobil (skal være false):', await m.evaluate(() => document.documentElement.scrollWidth > innerWidth));
  console.log(fejl.length ? 'FEJL:\n' + fejl.join('\n') : 'Ingen JavaScript-fejl');
  await b.close();
})();
