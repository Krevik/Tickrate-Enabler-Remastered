#include <cassert>
#include "../src/nextbotfixes.cpp"

static void CheckValues(float expectedPushDist, float expectedFrameTime)
{
	const float tolerance = 0.000001f;
	assert(std::isfinite(g_flZombiePushDist));
	assert(std::isfinite(g_WitchAngerGlobals.frametime));
	assert(std::fabs(g_flZombiePushDist - expectedPushDist) < tolerance);
	assert(std::fabs(g_WitchAngerGlobals.frametime - expectedFrameTime) < tolerance);
}

int main()
{
	ConVar frequency("test_nb_update_frequency", "0.1");
	const float tickInterval = 0.01f;
	UpdateNextBotFixes(&frequency, tickInterval);
	CheckValues(ZOMBIE_PUSH_DIST_DEFAULT, TICK_INTERVAL_DEFAULT);

	frequency.m_fValue = 0.0f;
	UpdateNextBotFixes(&frequency, tickInterval);
	CheckValues(0.3f, TICK_INTERVAL_DEFAULT * 0.1f);
	frequency.m_fValue = -1.0f;
	UpdateNextBotFixes(&frequency, tickInterval);
	CheckValues(0.3f, TICK_INTERVAL_DEFAULT * 0.1f);

	frequency.m_fValue = 0.025f;
	UpdateNextBotFixes(&frequency, tickInterval);
	CheckValues(0.9f, TICK_INTERVAL_DEFAULT * 0.3f);
	frequency.m_fValue = 0.2f;
	UpdateNextBotFixes(&frequency, tickInterval);
	CheckValues(ZOMBIE_PUSH_DIST_DEFAULT, TICK_INTERVAL_DEFAULT * 2.0f);

	const float invalidValues[] = {
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::max(),
		(float)std::numeric_limits<int>::max()
	};
	for (float invalidFrequency : invalidValues)
	{
		frequency.m_fValue = invalidFrequency;
		UpdateNextBotFixes(&frequency, tickInterval);
		CheckValues(ZOMBIE_PUSH_DIST_DEFAULT, TICK_INTERVAL_DEFAULT * 2.0f);
	}

	frequency.m_fValue = NB_UPDATE_INTERVAL_DEFAULT;
	const float invalidTickIntervals[] = {
		0.0f, -tickInterval,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::denorm_min(),
		std::numeric_limits<float>::max()
	};
	for (float invalidTickInterval : invalidTickIntervals)
	{
		UpdateNextBotFixes(&frequency, invalidTickInterval);
		CheckValues(ZOMBIE_PUSH_DIST_DEFAULT, TICK_INTERVAL_DEFAULT * 2.0f);
	}
	UpdateNextBotFixes(NULL, tickInterval);
	CheckValues(ZOMBIE_PUSH_DIST_DEFAULT, TICK_INTERVAL_DEFAULT * 2.0f);

	UpdateNextBotFixes(&frequency, TICK_INTERVAL_DEFAULT);
	CheckValues(ZOMBIE_PUSH_DIST_DEFAULT, TICK_INTERVAL_DEFAULT);
}
