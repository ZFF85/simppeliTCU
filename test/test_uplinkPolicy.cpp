#include "uplinkPolicy.h"
#include <cstdlib>
#include <iostream>
#include <limits>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "Failed at line " << __LINE__ << ": " #condition "\n"; \
        return EXIT_FAILURE; \
    } \
} while (false)

int main() {
    // Wi-Fi wins at boot even if the modem is already online.
    UplinkPolicy boot;
    CHECK(boot.update(0, true, true) == Uplink::WIFI);
    CHECK(!boot.shouldStartLte(60000));

    // Allow a full primary + fallback Wi-Fi attempt before starting the modem.
    UplinkPolicy offline;
    CHECK(offline.update(100, false, false) == Uplink::OFFLINE);
    CHECK(!offline.shouldStartLte(20099));
    CHECK(offline.shouldStartLte(20100));
    CHECK(offline.update(21000, false, true) == Uplink::LTE);

    // Returning Wi-Fi must stay connected for ten seconds; flaps reset the timer.
    CHECK(offline.update(22000, true, true) == Uplink::LTE);
    CHECK(offline.update(31999, true, true) == Uplink::LTE);
    CHECK(offline.update(32000, false, true) == Uplink::LTE);
    CHECK(offline.update(33000, true, true) == Uplink::LTE);
    CHECK(offline.update(42999, true, true) == Uplink::LTE);
    CHECK(offline.update(43000, true, true) == Uplink::WIFI);

    // An existing standby PPP link can take over immediately on Wi-Fi loss.
    CHECK(offline.update(44000, false, true) == Uplink::LTE);
    CHECK(offline.update(45000, false, false) == Uplink::OFFLINE);
    CHECK(offline.update(46000, true, false) == Uplink::WIFI);

    // Do not keep waiting for Wi-Fi stability if LTE itself has failed.
    UplinkPolicy recovery;
    CHECK(recovery.update(0, false, true) == Uplink::LTE);
    CHECK(recovery.update(100, true, true) == Uplink::LTE);
    CHECK(recovery.update(101, true, false) == Uplink::WIFI);

    // A brief Wi-Fi recovery restarts the modem startup grace period.
    UplinkPolicy grace;
    grace.update(0, false, false);
    grace.update(19000, true, false);
    grace.update(20000, false, false);
    CHECK(!grace.shouldStartLte(39999));
    CHECK(grace.shouldStartLte(40000));

    // An LTE-only configuration need not wait for unconfigured Wi-Fi networks.
    UplinkPolicy lteOnly(0);
    lteOnly.update(0, false, false);
    CHECK(lteOnly.shouldStartLte(0));

    // Both timers survive millis() wrapping after ~49 days.
    constexpr uint32_t wrap = std::numeric_limits<uint32_t>::max() - 100;
    UplinkPolicy rollover;
    rollover.update(wrap, false, false);
    CHECK(!rollover.shouldStartLte(wrap + uint32_t(19999)));
    CHECK(rollover.shouldStartLte(wrap + uint32_t(20000)));
    rollover.update(wrap, false, true);
    CHECK(rollover.update(wrap + uint32_t(1), true, true) == Uplink::LTE);
    CHECK(rollover.update(wrap + uint32_t(10000), true, true) == Uplink::LTE);
    CHECK(rollover.update(wrap + uint32_t(10001), true, true) == Uplink::WIFI);

    std::cout << "Uplink policy tests passed\n";
}
