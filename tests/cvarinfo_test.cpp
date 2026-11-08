#include <cassert>
#include "../src/tickrate_enabler.h"

int main()
{
	CvarInfo info("test_maxrate");
	bool hasMax = true;
	float maxValue = 30000.0f;
	info.RestoreDefaults(hasMax, maxValue);
	assert(hasMax && maxValue == 30000.0f);

	info.SaveDefaults(hasMax, maxValue);
	hasMax = false;
	maxValue = 0.0f;
	info.RestoreDefaults(hasMax, maxValue);
	assert(hasMax && maxValue == 30000.0f);

	// A second unload must not overwrite later changes.
	hasMax = false;
	maxValue = 10000.0f;
	info.RestoreDefaults(hasMax, maxValue);
	assert(!hasMax && maxValue == 10000.0f);

	// A convar absent on the next load must not reuse an older capture.
	info.SaveDefaults(true, 30000.0f);
	info.ClearSavedDefaults();
	info.RestoreDefaults(hasMax, maxValue);
	assert(!hasMax && maxValue == 10000.0f);

	// Preserve both fields even when the original maximum was disabled.
	info.SaveDefaults(false, 20000.0f);
	hasMax = true;
	maxValue = 40000.0f;
	info.RestoreDefaults(hasMax, maxValue);
	assert(!hasMax && maxValue == 20000.0f);

	// Captures are independent for each convar.
	CvarInfo missingInfo("test_missing_maxrate");
	info.SaveDefaults(true, 30000.0f);
	missingInfo.RestoreDefaults(hasMax, maxValue);
	assert(!hasMax && maxValue == 20000.0f);
	info.RestoreDefaults(hasMax, maxValue);
	assert(hasMax && maxValue == 30000.0f);
}
