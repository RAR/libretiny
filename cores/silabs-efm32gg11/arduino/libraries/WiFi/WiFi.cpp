/* Copyright (c) Andrew Rankin 2026-06-12. */

#include "WiFiPrivate.h"

WiFiClass::WiFiClass() {
	data = (WiFiData *)calloc(1, sizeof(WiFiData));

	DATA->scanSem = xSemaphoreCreateBinary();
	DATA->apLock  = xSemaphoreCreateRecursiveMutex();
	DATA->mode	  = WIFI_MODE_NULL;
	lt_wifi_sta_init(&DATA->staState);
}

WiFiClass::~WiFiClass() {
	resetNetworkInfo(DATA->sta);
	resetNetworkInfo(DATA->ap);
	vSemaphoreDelete(DATA->scanSem);
	if (DATA->apLock)
		vSemaphoreDelete(DATA->apLock);
	free(data);
	data = NULL;
}
