#include <cassert>
#include <cstring>
#include <type_traits>
#include <eiface.h>
#include "../src/basicbinpatch.h"

MemoryUtils g_MemUtils;
MemoryUtils::MemoryUtils() {}
MemoryUtils::~MemoryUtils() {}
void MemoryUtils::SetMemPatchable(void*, size_t) {}

static_assert(!std::is_copy_constructible<BasicBinPatch>::value);
static_assert(!std::is_copy_assignable<BasicBinPatch>::value);
static_assert(!std::is_move_constructible<BasicBinPatch>::value);
static_assert(!std::is_copy_constructible<BasicStaticBinPatch<2>>::value);
static_assert(!std::is_copy_assignable<BasicStaticBinPatch<2>>::value);
static_assert(!std::is_move_constructible<BasicStaticBinPatch<2>>::value);

template <typename PatchType>
static void TestRestoration()
{
	const BYTE original[] = { 0x12, 0x34 };
	const BYTE replacement[] = { 0x90, 0x90 };
	BYTE target[sizeof(original)];
	memcpy(target, original, sizeof(target));
	{
		PatchType patch(target, replacement);
		patch.Patch();
		patch.Patch();
		assert(memcmp(target, replacement, sizeof(target)) == 0);
		patch.Unpatch();
		patch.Unpatch();
		assert(memcmp(target, original, sizeof(target)) == 0);
		patch.Patch();
	}
	assert(memcmp(target, original, sizeof(target)) == 0);
	{
		PatchType patch(target, replacement, true);
		target[0] = 0x56;
		patch.Patch();
	}
	assert(target[0] == 0x56 && target[1] == original[1]);
	{
		PatchType patch(target, replacement);
		target[0] = 0x78;
	}
	assert(target[0] == 0x78);
}

class TwoByteBinPatch : public BasicBinPatch
{
public:
	TwoByteBinPatch(BYTE* target, const BYTE* replacement, bool patchTimeCopy = false)
		: BasicBinPatch(target, replacement, 2, patchTimeCopy) {}
};

int main()
{
	TestRestoration<TwoByteBinPatch>();
	TestRestoration<BasicStaticBinPatch<2>>();
	BYTE target[] = { 0x12, 0x34 };
	const BYTE replacement[] = { 0x90, 0x90 };
	try
	{
		TwoByteBinPatch patch(target, replacement);
		patch.Patch();
		throw 1;
	}
	catch (int)
	{
		assert(target[0] == 0x12 && target[1] == 0x34);
	}
}
