#pragma once

// Decides whether the open configuration network (CONFIG_AP_SSID) is needed.
// It goes down only once the controller has proven it reaches the internet:
// the station has an address and the cloud has been answering for
// STABLE_ONLINE_MS. It comes back when the station has been lost for
// STATION_LOST_MS or the cloud has been silent for CLOUD_SILENT_MS, so the
// configuration page stays reachable whenever the configured network fails.
class AccessPointPolicy {
public:
  static constexpr unsigned long STABLE_ONLINE_MS = 3UL * 60UL * 1000UL;
  static constexpr unsigned long STATION_LOST_MS = 60UL * 1000UL;
  static constexpr unsigned long CLOUD_SILENT_MS = 5UL * 60UL * 1000UL;

  // stationOnline: connected to the configured network with an address.
  // lastRequestAnswered: the latest cloud request got any HTTP response.
  // lastAnswerAt: millis() of the latest HTTP response, if there was one.
  // Returns whether the access point should be enabled.
  bool update(unsigned long now, bool stationOnline, bool lastRequestAnswered,
    unsigned long lastAnswerAt);
  bool enabled() const;

private:
  bool accessPointEnabled = true;
  bool online = false;
  unsigned long onlineSince = 0;
  bool stationLost = false;
  unsigned long stationLostSince = 0;
};
