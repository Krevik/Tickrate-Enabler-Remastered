#include <cassert>
#include <vector>
#include "../src/boomervomitpatch.cpp"
#include "../src/thirdparty/codepatch/patchmanager.cpp"

// Fake CVomit::UpdateAbility code; data used by the test lives elsewhere.
static BYTE g_ServerMemory[4096];
static DynLibInfo g_ServerLibrary;
static bool g_HasLibraryInfo = true;

// Server globals the patched instructions read ('enginetrace', 'lagcompensation').
static void* g_EngineTraceGlobal = NULL;
static void* g_LagCompensationGlobal = NULL;

static void* g_SymUpdateAbility = NULL;
static void* g_SymOnVomitCollide = NULL;
static void* g_SymGetTeamNumber = NULL;
static void* g_SymEngineTrace = NULL;
static void* g_SymLagCompensation = NULL;

MemoryUtils g_MemUtils;
MemoryUtils::MemoryUtils() {}
MemoryUtils::~MemoryUtils() {}
void MemoryUtils::SetMemPatchable(void*, size_t) {}
void* MemoryUtils::SimpleResolve(void*, const char* symbol)
{
	if (strcmp(symbol, SYM_CVOMIT_UPDATEABILITY) == 0) return g_SymUpdateAbility;
	if (strcmp(symbol, SYM_ONVOMITCOLLIDE) == 0) return g_SymOnVomitCollide;
	if (strcmp(symbol, SYM_CBASEENTITY_GETTEAMNUMBER) == 0) return g_SymGetTeamNumber;
	if (strcmp(symbol, SYM_ENGINETRACE) == 0) return g_SymEngineTrace;
	if (strcmp(symbol, SYM_LAGCOMPENSATION) == 0) return g_SymLagCompensation;
	return NULL;
}
bool MemoryUtils::GetLibraryInfo(const void*, DynLibInfo& lib)
{
	lib = g_ServerLibrary;
	return g_HasLibraryInfo;
}

/*
 * Fake game objects
 */

class FakeEntity : public IHandleEntity
{
public:
	explicit FakeEntity(int index) : m_handle(index, 1) {}
	void SetRefEHandle(const CBaseHandle& handle) { m_handle = handle; }
	const CBaseHandle& GetRefEHandle() const { return m_handle; }
	CBaseEntity* AsBaseEntity() { return reinterpret_cast<CBaseEntity*>(static_cast<IHandleEntity*>(this)); }
private:
	CBaseHandle m_handle;
};

static FakeEntity g_World(0);
static FakeEntity g_Boomer(4);
static FakeEntity g_SurvivorA(1);
static FakeEntity g_SurvivorB(2);
static FakeEntity g_SurvivorC(3);
static FakeEntity g_InfectedPlayer(5);
static FakeEntity g_Prop(100);

// What a segment would hit, nearest first.
struct SceneObject
{
	FakeEntity* entity;
	float fraction;
};
static std::vector<SceneObject> g_Scene;
static int g_TraceRayCalls = 0;

// Rejects the infected team, like CTraceFilterSimpleNoInfectedTeamOrGhosts.
class FakeVomitFilter : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* entity, int)
	{
		return entity != &g_InfectedPlayer && entity != &g_Boomer;
	}
	TraceType_t GetTraceType() const { return TRACE_EVERYTHING; }
};

class FakeEngineTrace : public IEngineTrace
{
public:
	int GetPointContents(const Vector&, int, IHandleEntity**) { return 0; }
	int GetPointContents_WorldOnly(const Vector&, int) { return 0; }
	int GetPointContents_Collideable(ICollideable*, const Vector&) { return 0; }
	void ClipRayToEntity(const Ray_t&, unsigned int, IHandleEntity*, trace_t*) {}
	void ClipRayToCollideable(const Ray_t&, unsigned int, ICollideable*, trace_t*) {}
	void TraceRay(const Ray_t& ray, unsigned int mask, ITraceFilter* filter, trace_t* trace)
	{
		g_TraceRayCalls++;
		assert(mask == 0x2004003);
		for (size_t i = 0; i < g_Scene.size(); i++)
		{
			IHandleEntity* entity = g_Scene[i].entity;
			if (entity == &g_World || filter->ShouldHitEntity(entity, mask))
			{
				trace->fraction = g_Scene[i].fraction;
				trace->m_pEnt = g_Scene[i].entity->AsBaseEntity();
				trace->endpos = ray.m_Start + ray.m_Delta * g_Scene[i].fraction;
				return;
			}
		}
		trace->fraction = 1.0f;
		trace->m_pEnt = NULL;
		trace->endpos = ray.m_Start + ray.m_Delta;
	}
	void SetupLeafAndEntityListRay(const Ray_t&, ITraceListData*) {}
	void SetupLeafAndEntityListBox(const Vector&, const Vector&, ITraceListData*) {}
	void TraceRayAgainstLeafAndEntityList(const Ray_t&, ITraceListData*, unsigned int, ITraceFilter*, trace_t*) {}
	void SweepCollideable(ICollideable*, const Vector&, const Vector&, const QAngle&, unsigned int, ITraceFilter*, trace_t*) {}
	void EnumerateEntities(const Ray_t&, bool, IEntityEnumerator*) {}
	void EnumerateEntities(const Vector&, const Vector&, IEntityEnumerator*) {}
	ICollideable* GetCollideable(IHandleEntity*) { return NULL; }
	int GetStatByIndex(int, bool) { return 0; }
	void GetBrushesInAABB(const Vector&, const Vector&, CUtlVector<int>*, int) {}
	CPhysCollide* GetCollidableFromDisplacementsInAABB(const Vector&, const Vector&) { return NULL; }
	bool GetBrushInfo(int, CUtlVector<Vector4D>*, int*) { return false; }
	bool PointOutsideWorld(const Vector&) { return false; }
	int GetLeafContainingPoint(const Vector&) { return 0; }
	ITraceListData* AllocTraceListData() { return NULL; }
	void FreeTraceListData(ITraceListData*) {}
};
static FakeEngineTrace g_EngineTrace;

// ILagCompensationManager as the game calls it: vtable slot 1 = FinishLagCompensation(player).
static void* g_FinishedPlayer = NULL;
static int g_FinishCalls = 0;
static void FakeFinishLagCompensation(void*, void* player)
{
	g_FinishedPlayer = player;
	g_FinishCalls++;
}
static void* g_LagCompensationVTable[2] = { NULL, reinterpret_cast<void*>(&FakeFinishLagCompensation) };
static VomitInterfaceProxy g_LagCompensation = { g_LagCompensationVTable };

struct CollideCall
{
	void* attacker;
	void* victim;
	void* particle;
	int team;
	VomitVector velocity;
	VomitVector position;
};
static std::vector<CollideCall> g_CollideCalls;
static void FakeOnVomitCollide(void* attacker, void* victim, void* particle, int team, VomitVector velocity, VomitVector position)
{
	CollideCall call = { attacker, victim, particle, team, velocity, position };
	g_CollideCalls.push_back(call);
}
static int FakeGetTeamNumber(const void* entity)
{
	assert(entity == &g_Boomer);
	return 3;
}

static CGlobalVars g_Globals(false);

/*
 * Helpers
 */

static void WriteGlobalLoad(size_t offset, void* global)
{
	g_ServerMemory[offset] = 0xA1;
	memcpy(g_ServerMemory + offset + 1, &global, sizeof(global));
}

static void WriteCall(size_t offset, void* target)
{
	g_ServerMemory[offset] = 0xE8;
	int32_t displacement = (int32_t)(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(g_ServerMemory + offset + 5));
	memcpy(g_ServerMemory + offset + 1, &displacement, sizeof(displacement));
}

static void WriteVanillaCode()
{
	memset(g_ServerMemory, 0x90, sizeof(g_ServerMemory));
	WriteGlobalLoad(VOMIT_PIERCE_TRACE_LOAD_OFFSET, &g_EngineTraceGlobal);
	memcpy(g_ServerMemory + VOMIT_PIERCE_TRACE_VTABLE_OFFSET, "\x8B\x10", 2);
	memcpy(g_ServerMemory + VOMIT_PIERCE_TRACE_CALL_OFFSET, "\xFF\x52\x14", 3);
	WriteCall(VOMIT_PIERCE_COLLIDE_CALL_OFFSET, reinterpret_cast<void*>(&FakeOnVomitCollide));
	WriteGlobalLoad(VOMIT_PIERCE_FINISH_LOAD_OFFSET, &g_LagCompensationGlobal);
	memcpy(g_ServerMemory + VOMIT_PIERCE_FINISH_VTABLE_OFFSET, "\x8B\x10", 2);
	memcpy(g_ServerMemory + VOMIT_PIERCE_FINISH_CALL_OFFSET, "\xFF\x52\x04", 3);
}

static void ResetSymbols()
{
	g_SymUpdateAbility = g_ServerMemory;
	g_SymOnVomitCollide = reinterpret_cast<void*>(&FakeOnVomitCollide);
	g_SymGetTeamNumber = reinterpret_cast<void*>(&FakeGetTeamNumber);
	g_SymEngineTrace = &g_EngineTraceGlobal;
	g_SymLagCompensation = &g_LagCompensationGlobal;
}

static void ExpectRejected(const CGlobalVars* globals = &g_Globals)
{
	bool rejected = false;
	try
	{
		BoomerVomitPiercePatch patch(NULL, globals);
	}
	catch (PatchException&)
	{
		rejected = true;
	}
	assert(rejected);
}

// Runs the patched code path like the game: 'mov eax, [operand]; mov edx, [eax]; call [edx+slot*4]'.
static void* LoadPatchedInterface(size_t offset)
{
	void** operand;
	memcpy(&operand, g_ServerMemory + offset + 1, sizeof(operand));
	return *operand;
}

typedef void (*GameTraceRayFn)(void*, const Ray_t*, unsigned int, ITraceFilter*, trace_t*);
static void GameTraceSegment(const Vector& start, const Vector& end, trace_t& trace)
{
	void* engineTrace = LoadPatchedInterface(VOMIT_PIERCE_TRACE_LOAD_OFFSET);
	GameTraceRayFn traceRay = reinterpret_cast<GameTraceRayFn>((*reinterpret_cast<void***>(engineTrace))[5]);
	Ray_t ray;
	ray.Init(start, end);
	FakeVomitFilter filter;
	traceRay(engineTrace, &ray, 0x2004003, &filter, &trace);
}

typedef void (*GameFinishFn)(void*, void*);
static void GameFinishLagCompensation(void* player)
{
	void* lagCompensation = LoadPatchedInterface(VOMIT_PIERCE_FINISH_LOAD_OFFSET);
	GameFinishFn finish = reinterpret_cast<GameFinishFn>((*reinterpret_cast<void***>(lagCompensation))[1]);
	finish(lagCompensation, player);
}

static void ResetRun()
{
	g_Scene.clear();
	g_TraceRayCalls = 0;
	g_CollideCalls.clear();
	g_FinishCalls = 0;
	g_FinishedPlayer = NULL;
}

/*
 * Tests
 */

static void TestPierce()
{
	WriteVanillaCode();
	ResetSymbols();
	BYTE original[VOMIT_PIERCE_CODE_SIZE];
	memcpy(original, g_ServerMemory, sizeof(original));

	{
		BoomerVomitPiercePatch patch(NULL, &g_Globals);
		patch.Patch();
		assert(LoadPatchedInterface(VOMIT_PIERCE_TRACE_LOAD_OFFSET) == &s_TraceProxy);
		assert(LoadPatchedInterface(VOMIT_PIERCE_FINISH_LOAD_OFFSET) == &s_FinishProxy);
		assert(memcmp(original, g_ServerMemory, VOMIT_PIERCE_TRACE_LOAD_OFFSET) == 0);

		// Survivor A in front, an infected player (ignored by the game's filter), survivor B and a wall.
		ResetRun();
		g_Globals.tickcount = 100;
		g_Scene.push_back({ &g_SurvivorA, 0.2f });
		g_Scene.push_back({ &g_InfectedPlayer, 0.3f });
		g_Scene.push_back({ &g_SurvivorB, 0.5f });
		g_Scene.push_back({ &g_World, 0.8f });
		trace_t trace;
		GameTraceSegment(Vector(0, 0, 0), Vector(100, 0, -10), trace);
		assert(trace.m_pEnt == g_World.AsBaseEntity() && trace.fraction == 0.8f);
		assert(g_TraceRayCalls == 3);
		assert(g_CollideCalls.empty()); // applied when the stream trace finishes

		// Next segment of the same stream: A again (already covered), then C, nothing behind.
		g_Scene.clear();
		g_Scene.push_back({ &g_SurvivorA, 0.1f });
		g_Scene.push_back({ &g_SurvivorC, 0.4f });
		GameTraceSegment(Vector(100, 0, -10), Vector(170, 0, -30), trace);
		assert(trace.m_pEnt == NULL && trace.fraction == 1.0f);
		assert(g_TraceRayCalls == 5);

		GameFinishLagCompensation(&g_Boomer);
		assert(g_FinishCalls == 1 && g_FinishedPlayer == &g_Boomer);
		assert(g_CollideCalls.size() == 3);
		assert(g_CollideCalls[0].victim == &g_SurvivorA);
		assert(g_CollideCalls[1].victim == &g_SurvivorB);
		assert(g_CollideCalls[2].victim == &g_SurvivorC);
		for (size_t i = 0; i < g_CollideCalls.size(); i++)
		{
			assert(g_CollideCalls[i].attacker == &g_Boomer);
			assert(g_CollideCalls[i].particle == NULL);
			assert(g_CollideCalls[i].team == 3);
		}
		assert(g_CollideCalls[0].position.x == 100.0f && g_CollideCalls[0].position.z == -10.0f);
		assert(g_CollideCalls[0].velocity.x == 100.0f && g_CollideCalls[0].velocity.z == -10.0f);
		assert(g_CollideCalls[2].position.x == 170.0f && g_CollideCalls[2].position.z == -30.0f);

		// Next tick: the survivors can be hit again.
		ResetRun();
		g_Globals.tickcount = 101;
		g_Scene.push_back({ &g_SurvivorA, 0.2f });
		g_Scene.push_back({ &g_Prop, 0.6f });
		g_Scene.push_back({ &g_SurvivorB, 0.7f });
		GameTraceSegment(Vector(0, 0, 0), Vector(100, 0, 0), trace);
		assert(trace.m_pEnt == g_Prop.AsBaseEntity()); // props still stop the stream
		GameFinishLagCompensation(&g_Boomer);
		assert(g_CollideCalls.size() == 1 && g_CollideCalls[0].victim == &g_SurvivorA);

		// Nothing pierced: only the real FinishLagCompensation runs.
		ResetRun();
		g_Scene.push_back({ &g_World, 0.5f });
		GameTraceSegment(Vector(0, 0, 0), Vector(100, 0, 0), trace);
		assert(trace.m_pEnt == g_World.AsBaseEntity() && g_TraceRayCalls == 1);
		GameFinishLagCompensation(&g_Boomer);
		assert(g_CollideCalls.empty() && g_FinishCalls == 1);

		// Hits left over from an unfinished trace in an earlier tick are dropped.
		ResetRun();
		g_Scene.push_back({ &g_SurvivorA, 0.2f });
		GameTraceSegment(Vector(0, 0, 0), Vector(100, 0, 0), trace);
		g_Globals.tickcount = 102;
		g_Scene.clear();
		g_Scene.push_back({ &g_SurvivorB, 0.2f });
		GameTraceSegment(Vector(0, 0, 0), Vector(100, 0, 0), trace);
		GameFinishLagCompensation(&g_Boomer);
		assert(g_CollideCalls.size() == 1 && g_CollideCalls[0].victim == &g_SurvivorB);
		ResetRun();
		g_Scene.push_back({ &g_SurvivorA, 0.2f });
		GameTraceSegment(Vector(0, 0, 0), Vector(100, 0, 0), trace);
		g_Globals.tickcount = 103;
		GameFinishLagCompensation(&g_Boomer);
		assert(g_CollideCalls.empty() && g_FinishCalls == 1);

		// More players than the hit list holds: the rest is left to the game (vanilla stop + hit).
		ResetRun();
		g_Globals.maxClients = 64;
		std::vector<FakeEntity*> crowd;
		for (int i = 0; i < VOMIT_PIERCE_MAX_HITS + 2; i++)
		{
			crowd.push_back(new FakeEntity(10 + i));
			g_Scene.push_back({ crowd.back(), 0.01f * (i + 1) });
		}
		GameTraceSegment(Vector(0, 0, 0), Vector(100, 0, 0), trace);
		assert(trace.m_pEnt == crowd[VOMIT_PIERCE_MAX_HITS]->AsBaseEntity());
		GameFinishLagCompensation(&g_Boomer);
		assert(g_CollideCalls.size() == VOMIT_PIERCE_MAX_HITS);
		for (size_t i = 0; i < crowd.size(); i++)
		{
			delete crowd[i];
		}
		g_Globals.maxClients = 8;
	}
	assert(memcmp(original, g_ServerMemory, sizeof(original)) == 0);
}

static void TestRejected()
{
	WriteVanillaCode();
	ResetSymbols();
	{
		BoomerVomitPiercePatch patch(NULL, &g_Globals);
	}

	ExpectRejected(NULL);

	void** symbols[] = { &g_SymUpdateAbility, &g_SymOnVomitCollide, &g_SymGetTeamNumber, &g_SymEngineTrace, &g_SymLagCompensation };
	for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++)
	{
		*symbols[i] = NULL;
		ExpectRejected();
		ResetSymbols();
	}

	// Operands reading the wrong globals.
	g_SymEngineTrace = &g_LagCompensationGlobal;
	ExpectRejected();
	ResetSymbols();
	g_SymLagCompensation = &g_EngineTraceGlobal;
	ExpectRejected();
	ResetSymbols();
	g_SymOnVomitCollide = reinterpret_cast<void*>(&FakeGetTeamNumber);
	ExpectRejected();
	ResetSymbols();

	const size_t sites[] = {
		VOMIT_PIERCE_TRACE_LOAD_OFFSET, VOMIT_PIERCE_TRACE_VTABLE_OFFSET + 1, VOMIT_PIERCE_TRACE_CALL_OFFSET + 2,
		VOMIT_PIERCE_COLLIDE_CALL_OFFSET, VOMIT_PIERCE_FINISH_LOAD_OFFSET, VOMIT_PIERCE_FINISH_VTABLE_OFFSET,
		VOMIT_PIERCE_FINISH_CALL_OFFSET + 2,
	};
	for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++)
	{
		WriteVanillaCode();
		g_ServerMemory[sites[i]] ^= 0xFF;
		ExpectRejected();
	}
	WriteVanillaCode();

	const size_t fullModuleSize = g_ServerLibrary.memorySize;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_ServerMemory) - reinterpret_cast<uintptr_t>(g_ServerLibrary.baseAddress)
		+ VOMIT_PIERCE_CODE_SIZE - 1;
	ExpectRejected();
	g_ServerLibrary.memorySize = fullModuleSize;
	g_HasLibraryInfo = false;
	ExpectRejected();
	g_HasLibraryInfo = true;
	{
		BoomerVomitPiercePatch patch(NULL, &g_Globals);
	}
}

int main()
{
	Dl_info moduleInfo;
	assert(dladdr(g_ServerMemory, &moduleInfo) != 0);
	g_ServerLibrary.baseAddress = moduleInfo.dli_fbase;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_ServerMemory) - reinterpret_cast<uintptr_t>(moduleInfo.dli_fbase)
		+ sizeof(g_ServerMemory);

	g_EngineTraceGlobal = static_cast<IEngineTrace*>(&g_EngineTrace);
	g_LagCompensationGlobal = &g_LagCompensation;
	g_Globals.maxClients = 8;

	TestPierce();
	TestRejected();
	return 0;
}
