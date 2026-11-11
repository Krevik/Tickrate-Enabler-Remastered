#include <cassert>
#include <string>
#include <vector>
#include "../src/reliablesound.cpp"

struct EmittedSound
{
	bool reliable;
	bool initMessage;
	std::vector<int> recipients;
	int entity;
	int channel;
	std::string sample;
	float volume;
	int soundLevel;
	int flags;
	int pitch;
	bool hasOrigin;
	Vector origin;
};

class FakeEngineSound : public IEngineSound
{
public:
	std::vector<EmittedSound> emitted;
	std::vector<std::string> precached;
	std::vector<std::string> precacheCalls;

	bool PrecacheSound(const char* pSample, bool, bool) { precacheCalls.push_back(pSample); precached.push_back(pSample); return true; }
	bool IsSoundPrecached(const char* pSample)
	{
		for (size_t i = 0; i < precached.size(); i++)
		{
			if (precached[i] == pSample) return true;
		}
		return false;
	}
	void PrefetchSound(const char*) {}
	bool IsLoopingSound(const char*) { return false; }
	float GetSoundDuration(const char*) { return 0.0f; }
	void EmitSound(IRecipientFilter&, int, int, const char*, float, float, int, int, const Vector*, const Vector*, CUtlVector<Vector>*, bool, float, int)
	{
		assert(!"the soundlevel_t overload must be used");
	}
	void EmitSound(IRecipientFilter& filter, int iEntIndex, int iChannel, const char* pSample, float flVolume, soundlevel_t iSoundlevel,
		int iFlags, int iPitch, const Vector* pOrigin, const Vector* pDirection, CUtlVector<Vector>* pUtlVecOrigins, bool bUpdatePositions,
		float soundtime, int speakerentity)
	{
		assert(pDirection == NULL && pUtlVecOrigins == NULL && bUpdatePositions && soundtime == 0.0f && speakerentity == -1);
		EmittedSound sound;
		sound.reliable = filter.IsReliable();
		sound.initMessage = filter.IsInitMessage();
		for (int i = 0; i < filter.GetRecipientCount(); i++)
		{
			sound.recipients.push_back(filter.GetRecipientIndex(i));
		}
		assert(filter.GetRecipientIndex(-1) == -1 && filter.GetRecipientIndex(filter.GetRecipientCount()) == -1);
		sound.entity = iEntIndex;
		sound.channel = iChannel;
		sound.sample = pSample;
		sound.volume = flVolume;
		sound.soundLevel = (int)iSoundlevel;
		sound.flags = iFlags;
		sound.pitch = iPitch;
		sound.hasOrigin = pOrigin != NULL;
		if (pOrigin != NULL) sound.origin = *pOrigin;
		emitted.push_back(sound);
	}
	void EmitSentenceByIndex(IRecipientFilter&, int, int, int, float, soundlevel_t, int, int, const Vector*, const Vector*, CUtlVector<Vector>*, bool, float, int) {}
	void StopSound(int, int, const char*) {}
	void StopAllSounds(bool) {}
	void SetRoomType(IRecipientFilter&, int) {}
	void SetPlayerDSP(IRecipientFilter&, int, bool) {}
	void EmitAmbientSound(const char*, float, int, int, float) {}
	float GetDistGainFromSoundLevel(soundlevel_t, float) { return 0.0f; }
	int GetGuidForLastSoundEmitted() { return 0; }
	bool IsSoundStillPlaying(int) { return false; }
	void StopSoundByGuid(int) {}
	void SetVolumeByGuid(int, float) {}
	void GetActiveSounds(CUtlVector<SndInfo_t>&) {}
	void PrecacheSentenceGroup(const char*) {}
	void NotifyBeginMoviePlayback() {}
	void NotifyEndMoviePlayback() {}
	bool GetSoundChannelVolume(const char*, float&, float&) { return false; }
	float GetElapsedTimeByGuid(int) { return 0.0f; }
};

static const int MAX_CLIENTS = 8;

// Clients 1, 2, 4 and 8 are connected humans, 7 is SourceTV; 3 is a bot, the rest are free.
static bool IsRecipient(int client)
{
	assert(client >= 1 && client <= MAX_CLIENTS);
	return client == 1 || client == 2 || client == 4 || client == 7 || client == 8;
}

static void Precache(FakeEngineSound& engine, const char* sample)
{
	engine.precached.push_back(sample);
}

static bool Run(FakeEngineSound& engine, const char* commandLine, char* error = NULL, size_t errorLength = 0)
{
	CCommand args;
	assert(args.Tokenize(commandLine));
	char localError[256] = "";
	bool ok = EmitReliableSoundCommand(args, &engine, MAX_CLIENTS, IsRecipient,
		error != NULL ? error : localError, error != NULL ? errorLength : sizeof(localError));
	if (ok) assert(localError[0] == '\0');
	return ok;
}

static void ExpectInvalid(const char* commandLine)
{
	FakeEngineSound engine;
	Precache(engine, "a.wav");
	char error[256] = "";
	assert(!Run(engine, commandLine, error, sizeof(error)));
	assert(error[0] != '\0');
	assert(engine.emitted.empty() && engine.precacheCalls.empty());
}

static void TestWorldSoundToEveryone()
{
	FakeEngineSound engine;
	Precache(engine, "player/jockey/voice/alert/jockey_02.wav");
	assert(Run(engine, "te_reliable_sound 0 6 95 0.75 105 0 1.5,-2,300 \"player/jockey/voice/alert/jockey_02.wav\""));
	assert(engine.emitted.size() == 1);
	const EmittedSound& sound = engine.emitted[0];
	assert(sound.reliable && !sound.initMessage);
	assert(sound.recipients == std::vector<int>({ 1, 2, 4, 7, 8 })); // humans and SourceTV
	assert(sound.entity == 0 && sound.channel == 6 && sound.soundLevel == 95 && sound.pitch == 105 && sound.flags == 0);
	assert(sound.volume == 0.75f);
	assert(sound.sample == "player/jockey/voice/alert/jockey_02.wav");
	assert(sound.hasOrigin && sound.origin.x == 1.5f && sound.origin.y == -2.0f && sound.origin.z == 300.0f);
	assert(engine.precacheCalls.empty()); // never precached at runtime
}

// Precaching during the game can stop the map, so an unknown sample is refused.
static void TestNotPrecached()
{
	FakeEngineSound engine;
	char error[256] = "";
	assert(!Run(engine, "te_reliable_sound 0 6 95 1 100 0 0,0,0 \"player/missing.wav\"", error, sizeof(error)));
	assert(strstr(error, "precached") != NULL);
	assert(engine.emitted.empty() && engine.precacheCalls.empty());
}

// A hooked game sound re-emitted unchanged: same entity, channel and recipients, entity position.
static void TestRelayToHookRecipients()
{
	FakeEngineSound engine;
	engine.precached.push_back(")player/charger/voice/idle/Charger_lurk_01.wav");
	assert(Run(engine, "te_reliable_sound 5 2 85 1 100 1024 - \")player/charger/voice/idle/Charger_lurk_01.wav\" 1,3,4,4,7"));
	assert(engine.emitted.size() == 1 && engine.precacheCalls.empty());
	const EmittedSound& sound = engine.emitted[0];
	assert(sound.reliable);
	assert(sound.recipients == std::vector<int>({ 1, 4, 7 })); // bot 3 skipped, duplicate 4 once, SourceTV kept
	assert(sound.entity == 5 && sound.channel == 2 && sound.soundLevel == 85 && sound.flags == 1024);
	assert(!sound.hasOrigin);
	assert(sound.sample == ")player/charger/voice/idle/Charger_lurk_01.wav");
}

static void TestNoRecipients()
{
	FakeEngineSound engine;
	Precache(engine, "a.wav");
	assert(Run(engine, "te_reliable_sound 5 2 85 1 100 0 - \"a.wav\" 3,5"));
	assert(engine.emitted.empty() && engine.precacheCalls.empty());
}

static void TestInvalid()
{
	ExpectInvalid("te_reliable_sound");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 a.wav 1 extra");
	ExpectInvalid("te_reliable_sound -1 6 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 2048 6 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 1x 6 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 8 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 256 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1.5 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 nan 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 0 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 256 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 -1 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 a,b,c a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,inf a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 - a.wav");	// the world needs an origin
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 \"\"");
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 0");
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 9");	// above maxplayers
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 1,,2");
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 1,");
	// strtol overflow must not saturate into a valid value
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 4294967295 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 99999999999999 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 99999999999999 6 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 99999999999999");
	// samples the engine would choke on
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 \" a.wav\"");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 \"a\x01.wav\"");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 \"\xC3\xA9.wav\"");
}

int main()
{
	TestWorldSoundToEveryone();
	TestNotPrecached();
	TestRelayToHookRecipients();
	TestNoRecipients();
	TestInvalid();
	return 0;
}
