#ifndef FLEET_TEST_H
#define FLEET_TEST_H

#include <libultraship/libultra/controller.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fleet Combo test mode (host fleet-test folder): adds scripted buttons and stick to controller 1.
void FleetTest_InjectPad(OSContPad* pad);
// Runs a pending test command; once per frame, at frame end.
void FleetTest_Tick(void);

#ifdef __cplusplus
}
#endif

#endif
