#include "hardware.h"
#include "featureFlags.h"
#include "networkValidation.h"
#include <gtest/gtest.h>

TEST(HardwareConfig, SelectedBoardUsesExpectedCanPins) {
#if BOARD == WAVESHARE_ESP32_S3_RS485_CAN
    EXPECT_EQ(CAN_TX_PIN, 15);
    EXPECT_EQ(CAN_RX_PIN, 16);
    EXPECT_EQ(HAS_SECOND_CAN, 0);
    EXPECT_EQ(USE_SECOND_CAN, 0);
#else
    EXPECT_EQ(CAN_TX_PIN, 7);
    EXPECT_EQ(CAN_RX_PIN, 6);
    EXPECT_EQ(HAS_SECOND_CAN, 1);
#endif
}

TEST(HardwareConfig, ModemCannotTakeOverBoardCanOrSpiPins) {
    EXPECT_TRUE(validModemGpio(-1));
    EXPECT_TRUE(validModemGpio(1));
    EXPECT_TRUE(validModemGpio(2));
    EXPECT_FALSE(validModemGpio(CAN_TX_PIN));
    EXPECT_FALSE(validModemGpio(CAN_RX_PIN));
#if HAS_SECOND_CAN
    for (int pin : {MCP2515_CS, MCP2515_SCLK, MCP2515_MOSI, MCP2515_MISO, MCP2515_RST, MCP2515_INT}) {
        EXPECT_FALSE(validModemGpio(pin)) << pin;
    }
#endif
}

TEST(HardwareConfig, ModemCannotUseEsp32ReservedPins) {
    for (int pin : {-2, 0, 3, 19, 20, 22, 25, 26, 30, 37, 43, 44, 45, 46, 49}) {
        EXPECT_FALSE(validModemGpio(pin)) << pin;
    }
}
