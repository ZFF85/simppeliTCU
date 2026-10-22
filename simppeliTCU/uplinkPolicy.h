#pragma once

#include <stdint.h>

enum class Uplink { OFFLINE, WIFI, LTE };

// Independent of the modem driver so timing/priority can be tested on a host.
class UplinkPolicy {
public:
    explicit UplinkPolicy(uint32_t wifiGraceMs = 20000) : graceMs(wifiGraceMs) {}

    Uplink update(uint32_t now, bool wifiReady, bool lteReady) {
        if (!observed || wifiReady != previousWifi) {
            wifiChangedAt = now;
            previousWifi = wifiReady;
            observed = true;
        }

        if (wifiReady && (active != Uplink::LTE || !lteReady ||
                          uint32_t(now - wifiChangedAt) >= 10000)) {
            active = Uplink::WIFI;
        } else if (lteReady) {
            active = Uplink::LTE;
        } else {
            active = Uplink::OFFLINE;
        }
        return active;
    }

    bool shouldStartLte(uint32_t now) const {
        return observed && !previousWifi && uint32_t(now - wifiChangedAt) >= graceMs;
    }

private:
    uint32_t graceMs;
    uint32_t wifiChangedAt = 0;
    bool observed = false;
    bool previousWifi = false;
    Uplink active = Uplink::OFFLINE;
};
