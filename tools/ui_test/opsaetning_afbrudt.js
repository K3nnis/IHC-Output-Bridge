// Browsertest: forbindelsen til opsætnings-Wi-Fi'et afbrydes, mens "Gem" sendes.
// Siden skal fortsætte med at vente i stedet for at vise en fejl.
// Kræver mock_api.py startet med "0".
const { chromium, devices } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const fejl = [];
  const p = await (await b.newContext({ ...devices['iPhone 13'] })).newPage();
  p.on('pageerror', e => fejl.push(e.message));
  await p.goto('http://127.0.0.1:8766/'); await p.waitForTimeout(400);
  await p.fill('#o-ssid', 'Hjemme'); await p.fill('#o-wkode', 'wifikode');
  await p.fill('#o-kode', 'hemmelig123'); await p.fill('#o-kode2', 'hemmelig123');
  // Lad enheden modtage forespørgslen, men afbryd svaret (som når telefonen
  // mister forbindelsen til opsætnings-Wi-Fi'et).
  await p.route('**/api/opsaetning', async route => {
    if (route.request().method() === 'POST') {
      await route.fetch();  // når frem til enheden
      await route.abort('connectionreset');
    } else {
      await route.continue();
    }
  });
  await p.click('#o-gem'); await p.waitForTimeout(500);
  console.log('Fejlbesked vist (skal være tom):', JSON.stringify(await p.textContent('#o-besked')));
  console.log('Venter på forbindelse:', await p.isVisible('#o-faerdig'), '|', (await p.textContent('#o-faerdig h1')).trim());
  await p.unroute('**/api/opsaetning');
  await p.waitForTimeout(2600);
  console.log('Efter genforbindelse:', (await p.textContent('#o-faerdig h1')).trim());
  console.log(fejl.length ? 'FEJL: ' + fejl.join('; ') : 'Ingen JavaScript-fejl');
  await b.close();
})();
