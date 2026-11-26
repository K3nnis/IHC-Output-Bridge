// Web: webserver med opsætningsside, login, JSON-API, WebSocket og
// firmwareopdatering. Kører på kerne 0. API'et er beskrevet i docs/api.md.
#pragma once

namespace web {

void start();

// Lukker alle WebSocket-forbindelser (efter log ud eller ny adgangskode).
void luk_websockets();

// Kaldes af tilstand, når bussen melder ændringer; sendes live til UI'et.
void aendring();

}  // namespace web
