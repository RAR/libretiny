/* Phase 1 main.cpp for silabs-efm32gg11.
 *
 * Plan T23 prescribed a stand-alone `extern "C" int main(void)` that called
 * `lt_init()` then xTaskCreate()/vTaskStartScheduler(). However:
 *
 *   - cores/common/arduino/src/main.c already defines `int main(void)` which
 *     calls `lt_init_arduino()` then `startMainTask()` then provides the
 *     `mainTask(arg)` body (setup() + loop() + yield()). Adding our own
 *     `main()` here would duplicate that symbol at link time.
 *
 *   - The other families (beken-72xx, realtek-amb, lightning-ln882h) follow
 *     the common pattern: their family `main.cpp` defines ONLY
 *     `startMainTask()` (and optionally `lt_init_arduino()`).
 *
 *   - The common path expects `lt_init_family()` to run before main() (called
 *     from `lt_main()` in cores/common/base/lt_main.c). On the other families
 *     a startup fixup redirects Reset_Handler to `lt_main()`. Our Gecko SDK
 *     startup currently jumps to `_start -> main()` directly, so
 *     `lt_init_family()` never runs unless we call it ourselves. We call it
 *     at the top of `startMainTask()` — this is a Phase 1 convenience; T29 or
 *     a later phase can move the call into a proper startup fixup.
 *
 * So: follow precedent. Define `startMainTask()` and let common's main.c run.
 */

#include <ArduinoPrivate.h>

extern "C" {

#include "FreeRTOS.h"
#include "lt_family.h"
#include "task.h"

// Forward decl of common's lt_init_family() implementation (cores/.../lt_init.c).
extern void lt_init_family(void);

#ifndef LT_MAIN_TASK_STACK_SIZE
#define LT_MAIN_TASK_STACK_SIZE (4096)
#endif

#ifndef LT_MAIN_TASK_PRIORITY
#define LT_MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2)
#endif

bool startMainTask(void) {
	// Run family init here (see file comment for why).
	lt_init_family();

	BaseType_t rc = xTaskCreate(
		(TaskFunction_t)mainTask,
		"main",
		LT_MAIN_TASK_STACK_SIZE / sizeof(StackType_t),
		NULL,
		LT_MAIN_TASK_PRIORITY,
		NULL
	);
	if (rc != pdPASS) {
		return false;
	}

	vTaskStartScheduler();
	// Should not reach here.
	return true;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
	(void)task;
	printf("*** stack overflow in task '%s' ***\r\n", name);
	NVIC_SystemReset();
}

void vApplicationMallocFailedHook(void) {
	printf("*** FreeRTOS malloc failed ***\r\n");
	NVIC_SystemReset();
}

} // extern "C"
