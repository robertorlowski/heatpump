#include <access_point_policy.hpp>

bool AccessPointPolicy::update(unsigned long now, bool stationOnline,
  bool lastRequestAnswered, unsigned long lastAnswerAt)
{
  const bool cloudSilent = now - lastAnswerAt >= CLOUD_SILENT_MS;

  // A failed request restarts the stable period, so the access point goes
  // down only after STABLE_ONLINE_MS without a single miss. A stale answer
  // does not count, otherwise a controller that stopped sending requests
  // would switch the access point off again right after restoring it.
  // lastAnswerAt is valid whenever lastRequestAnswered is set.
  const bool reachable = stationOnline && lastRequestAnswered && !cloudSilent;
  if (reachable && !online) onlineSince = now;
  online = reachable;

  if (!stationOnline && !stationLost) stationLostSince = now;
  stationLost = !stationOnline;

  if (accessPointEnabled) {
    if (online && now - onlineSince >= STABLE_ONLINE_MS) {
      accessPointEnabled = false;
    }
  } else {
    // The access point is off only after an answer, so lastAnswerAt is set
    // and a single failed request does not bring it back.
    const bool stationGone =
      stationLost && now - stationLostSince >= STATION_LOST_MS;
    if (stationGone || cloudSilent) accessPointEnabled = true;
  }
  return accessPointEnabled;
}

bool AccessPointPolicy::enabled() const
{
  return accessPointEnabled;
}
