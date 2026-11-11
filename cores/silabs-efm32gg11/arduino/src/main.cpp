/* Copyright (c) Andrew Rankin 2026-06-12. */

#include <ArduinoPrivate.h>

extern "C" {

// In bytes. Like the other families' 8 KiB; deep call chains or large
// objects on the stack (JSON documents in a web handler) need more.
#ifndef LT_MAIN_TASK_STACK_SIZE
#define LT_MAIN_TASK_STACK_SIZE 8192
#endif

#ifndef LT_MAIN_TASK_PRIORITY
#define LT_MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2)
#endif

// The common mainTask() (cores/common/arduino/src/main.c) plus the OTA
// confirmation between setup() and the first loop(): a new image counts as
// healthy once setup() has returned (see LT_OTA_AUTO_CONFIRM in lt_family.h).
// lt_ota_confirm() does nothing unless the running image is an unconfirmed
// trial boot.
static void familyMainTask(void *arg) {
	(void)arg;
	setup();
#if LT_OTA_AUTO_CONFIRM
	lt_ota_confirm();
#endif
	for (;;) {
		loop();
		yield();
	}
}

bool startMainTask(void) {
	BaseType_t ret = xTaskCreate(
		familyMainTask,
		"main",
		LT_MAIN_TASK_STACK_SIZE / sizeof(StackType_t),
		NULL,
		LT_MAIN_TASK_PRIORITY,
		NULL
	);
	if (ret != pdPASS)
		return false;
	vTaskStartScheduler();
	return true;
}

} // extern "C"
