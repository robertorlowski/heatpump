#include <config_portal.hpp>

#include <WebServer.h>

#include <device_config.hpp>

namespace {
WebServer server(80);
bool running = false;
bool restartRequested = false;
unsigned long restartRequestedAt = 0;

const char PAGE_TEMPLATE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sterownik CO</title><style>
*{box-sizing:border-box}
body{margin:0;padding:24px 16px;background:#14161a;color:#e8eaed;
font:16px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
form{max-width:420px;margin:0 auto}
h1{font-size:1.25rem;margin:0 0 2px}
.sub{margin:0 0 24px;color:#9aa0a6;font-size:.875rem}
label{display:block;margin:18px 0 6px;font-size:.875rem;color:#c8cdd2}
input{width:100%;padding:11px 12px;border:1px solid #3c4043;border-radius:8px;
background:#1e2126;color:#e8eaed;font-size:1rem}
input:focus{outline:2px solid #5a9;outline-offset:-1px;border-color:#5a9}
small{display:block;margin-top:6px;color:#9aa0a6;font-size:.75rem}
button{width:100%;margin-top:28px;padding:13px;border:0;border-radius:8px;
background:#4db6a0;color:#0b2b24;font-size:1rem;font-weight:600}
.msg{padding:12px 14px;border-radius:8px;margin-bottom:20px;font-size:.875rem}
.ok{background:#12352c;color:#7fd6bb}
.err{background:#3a1c1c;color:#f0a0a0}
</style></head><body><form method="POST" action="/save">
<h1>Sterownik CO</h1>
<p class="sub">Konfiguracja połączenia</p>
%MESSAGE%
<label for="ssid">Sieć Wi-Fi</label>
<input id="ssid" name="ssid" value="%SSID%" required maxlength="63"
autocapitalize="off" autocorrect="off" spellcheck="false">
<label for="password">Hasło Wi-Fi</label>
<input id="password" name="password" type="password" maxlength="63"
placeholder="bez zmian">
<small>Puste pole zostawia dotychczasowe hasło.</small>
<label for="rootid">Root ID</label>
<input id="rootid" name="rootid" value="%ROOTID%" required maxlength="63"
autocapitalize="off" autocorrect="off" spellcheck="false">
<small>Identyfikator urządzenia z GET /api/devices w chpc-web.</small>
<button type="submit">Zapisz i uruchom ponownie</button>
</form></body></html>)HTML";

String escapeHtml(const String &value)
{
  String escaped;
  escaped.reserve(value.length());
  for (size_t index = 0; index < value.length(); index++) {
    const char character = value.charAt(index);
    switch (character) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += character;
    }
  }
  return escaped;
}

String renderPage(const String &message)
{
  const DeviceConfig &config = deviceConfig();
  String page = FPSTR(PAGE_TEMPLATE);
  page.replace("%MESSAGE%", message);
  page.replace("%SSID%", escapeHtml(config.wifiSsid));
  page.replace("%ROOTID%", escapeHtml(config.rootId));
  return page;
}

void sendPage(int status, const String &message)
{
  server.send(status, "text/html; charset=utf-8", renderPage(message));
}

void handleRoot()
{
  sendPage(200, "");
}

void handleSave()
{
  DeviceConfig next = deviceConfig();
  next.wifiSsid = server.arg("ssid");
  next.rootId = server.arg("rootid");

  // An empty password field keeps the stored one, so the page never has to
  // echo the Wi-Fi password back over plain HTTP.
  const String password = server.arg("password");
  if (password.length() > 0) next.wifiPassword = password;

  if (!saveDeviceConfig(next)) {
    sendPage(400, F("<div class=\"msg err\">Nie zapisano. "
      "Sieć Wi-Fi i Root ID nie mogą być puste.</div>"));
    return;
  }

  sendPage(200, F("<div class=\"msg ok\">Zapisano. "
    "Sterownik uruchamia się ponownie.</div>"));

  // Restarting here would cut the response short, so it is deferred until
  // handleClient() has closed the connection.
  restartRequested = true;
  restartRequestedAt = millis();
}
}

void beginConfigPortal()
{
  if (running) return;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([]() {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });
  server.begin();
  running = true;
}

void handleConfigPortal()
{
  if (!running) return;

  server.handleClient();

  constexpr unsigned long RESTART_DELAY_MS = 500;
  if (restartRequested && millis() - restartRequestedAt >= RESTART_DELAY_MS) {
    ESP.restart();
  }
}
