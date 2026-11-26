// Browsertest af web-UI'et mod tools/ui_test/mock_api.py. Se README.md.
const { chromium, devices } = require('playwright');
(async () => {
  const D = process.argv[2]; const b = await chromium.launch(); const fejl = [];
  const p = await (await b.newContext({ ...devices['iPhone 13'] })).newPage();
  p.on('pageerror', e => fejl.push(e.message));
  await p.goto('http://127.0.0.1:8766/'); await p.waitForTimeout(400);
  console.log('Første besøg viser opsætning:', await p.isVisible('#ops-form'));
  await p.click('#o-scan'); await p.waitForTimeout(300);
  console.log('Scan:', await p.textContent('#o-besked'));
  await p.fill('#o-ssid', 'Hjemme'); await p.fill('#o-wkode', 'wifikode'); await p.fill('#o-kode', 'hemmelig123'); await p.fill('#o-kode2', 'hemmelig123');
  await p.screenshot({ path: D + '/m0-opsaetning.png', fullPage: true });
  await p.click('#o-gem'); await p.waitForTimeout(2500);
  console.log('Efter gem:', (await p.textContent('#o-faerdig')).replace(/\s+/g, ' ').trim());
  const d = await (await b.newContext({ viewport: { width: 1440, height: 900 } })).newPage();
  await d.goto('http://127.0.0.1:8766/'); await d.fill('#l-kode', 'hemmelig123'); await d.click('#login-form button'); await d.waitForTimeout(800);
  console.log('Menuknap synlig på desktop (skal være false):', await d.isVisible('#menuknap'));
  console.log(fejl.length ? 'FEJL: ' + fejl.join('; ') : 'Ingen JavaScript-fejl');
  await b.close();
})();
