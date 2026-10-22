#pragma once

#include "uplinkPolicy.h"

// Call once after configuration and Wi-Fi initialization.
void setupConnectivity();
Uplink getActiveUplink();
// Prints a cached snapshot; never sends AT commands from the CAN/CLI loop.
void printLteStatus();
