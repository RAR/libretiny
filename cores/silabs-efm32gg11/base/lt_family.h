#pragma once

#include "lt_defs.h"

// Silicon Labs Gecko SDK pulls in the device header per chip variant.
// This header makes it available across the family's core code.
#include "em_device.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_gpio.h"
