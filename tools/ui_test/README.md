# Test af web-UI'et uden board

`mock_api.py` efterligner enhedens API og WebSocket med eksempeldata, så
web-UI'et (`web/index.html`) kan prøves i en browser på PC'en.

Start mock-serveren (sidste argument: `1` = enheden er sat op, `0` = første opstart):

```bash
python3 tools/ui_test/mock_api.py web/index.html 1
```

Åbn http://127.0.0.1:8766/ og log ind med `hemmelig123`.

Automatisk gennemgang med Playwright (gemmer skærmbilleder i mappen `ud`):

```bash
mkdir -p ud && node tools/ui_test/flow.js ud
```

`opsaetning.js` tester første opstart og kræver, at mock-serveren er startet med `0`.
