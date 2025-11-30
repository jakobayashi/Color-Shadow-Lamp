#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>
#include <stdarg.h>

// Lightweight tagged logger with timestamp (millis) to help trace freezes.
inline void logStatus(const char *tag, const char *fmt, ...)
{
    if (!Serial)
    {
        return;
    }

    char message[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    Serial.printf("[T+%9lu ms][%s] %s\n", millis(), tag, message);
}

#endif
