#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define APP_NORETURN __attribute__((noreturn))
#else
#define APP_NORETURN
#endif

/** Starts the statically allocated C++ application and the FreeRTOS scheduler. */
APP_NORETURN void App_Bootstrap(void);

#ifdef __cplusplus
}
#endif
