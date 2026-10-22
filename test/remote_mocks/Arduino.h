#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
unsigned long millis();
void delay(unsigned long ms);
struct ESPClass { void restart(); };
extern ESPClass ESP;
