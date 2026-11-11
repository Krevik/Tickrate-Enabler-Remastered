// Checks every Linux L4D2 patch site in src/sigs.h against the real game binaries.
// Usage: realbinary_check <server_srv.so> <engine_srv.so>
// The binaries come from the dedicated server depot (tools/l4d2ds_linux); run_tests.py skips this
// check when they are missing. Absolute operands are compared with symbol values (no relocation is
// applied to the file, the link-time base is 0).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "../src/sigs.h"

struct ElfFile
{
	std::vector<unsigned char> data;
	std::map<std::string, uint32_t> symbols;
	struct Section { uint32_t addr, offset, size, type; };
	std::vector<Section> sections;

	bool Load(const char* path)
	{
		FILE* file = fopen(path, "rb");
		if (file == NULL)
		{
			return false;
		}
		fseek(file, 0, SEEK_END);
		long size = ftell(file);
		fseek(file, 0, SEEK_SET);
		data.resize(size);
		bool ok = fread(data.data(), 1, size, file) == (size_t)size;
		fclose(file);
		if (!ok || size < 52 || memcmp(data.data(), "\x7F" "ELF", 4) != 0 || data[4] != 1)
		{
			return false;
		}

		uint32_t shoff = U32(0x20);
		uint16_t shentsize = U16(0x2E), shnum = U16(0x30);
		for (uint16_t i = 0; i < shnum; i++)
		{
			uint32_t base = shoff + i * shentsize;
			sections.push_back({ U32(base + 0x0C), U32(base + 0x10), U32(base + 0x14), U32(base + 0x04) });
		}
		for (uint16_t i = 0; i < shnum; i++)
		{
			const Section& s = sections[i];
			if (s.type != 2 && s.type != 11) // SYMTAB / DYNSYM
			{
				continue;
			}
			uint32_t link = U32(shoff + i * shentsize + 0x18);
			const Section& strtab = sections[link];
			for (uint32_t k = 0; k + 16 <= s.size; k += 16)
			{
				uint32_t name = U32(s.offset + k), value = U32(s.offset + k + 4);
				if (value != 0)
				{
					symbols.emplace(std::string((const char*)&data[strtab.offset + name]), value);
				}
			}
		}
		return true;
	}

	uint32_t U32(uint32_t offset) const { uint32_t v; memcpy(&v, &data[offset], 4); return v; }
	uint16_t U16(uint32_t offset) const { uint16_t v; memcpy(&v, &data[offset], 2); return v; }

	// File bytes at a virtual address (NULL if not inside a PROGBITS section).
	const unsigned char* At(uint32_t vaddr, size_t length) const
	{
		for (const Section& s : sections)
		{
			if (s.type == 1 && s.addr != 0 && vaddr >= s.addr && vaddr + length <= s.addr + s.size)
			{
				return &data[s.offset + (vaddr - s.addr)];
			}
		}
		return NULL;
	}

	uint32_t Symbol(const char* name) const
	{
		std::map<std::string, uint32_t>::const_iterator it = symbols.find(name);
		return it == symbols.end() ? 0 : it->second;
	}
};

static int g_Failures = 0;

static void Check(bool ok, const char* what)
{
	printf("  %-70s %s\n", what, ok ? "ok" : "MISMATCH");
	if (!ok)
	{
		g_Failures++;
	}
}

static bool BytesAt(const ElfFile& elf, uint32_t vaddr, const char* bytes, size_t length)
{
	const unsigned char* p = elf.At(vaddr, length);
	return p != NULL && memcmp(p, bytes, length) == 0;
}

// 'mov eax, [abs32]' (A1) whose operand is the address of 'global'.
static bool LoadsGlobal(const ElfFile& elf, uint32_t vaddr, const char* global)
{
	const unsigned char* p = elf.At(vaddr, 5);
	uint32_t operand;
	return p != NULL && p[0] == 0xA1 && (memcpy(&operand, p + 1, 4), operand == elf.Symbol(global)) && operand != 0;
}

// 'call/jmp rel32' at vaddr targeting the symbol.
static bool BranchesTo(const ElfFile& elf, uint32_t vaddr, unsigned char opcode, const char* target)
{
	const unsigned char* p = elf.At(vaddr, 5);
	int32_t displacement;
	return p != NULL && p[0] == opcode && (memcpy(&displacement, p + 1, 4), vaddr + 5 + displacement == elf.Symbol(target));
}

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		fprintf(stderr, "usage: %s <server_srv.so> <engine_srv.so>\n", argv[0]);
		return 2;
	}
	ElfFile server, engine;
	if (!server.Load(argv[1]) || !engine.Load(argv[2]))
	{
		fprintf(stderr, "can't read the binaries\n");
		return 2;
	}

	printf("server_srv.so\n");
	uint32_t update = server.Symbol(SYM_CVOMIT_UPDATEABILITY);
	Check(update != 0, "CVomit::UpdateAbility symbol");
	for (size_t i = 0; i < NUM_FRAMETIME_READS; i++)
	{
		Check(LoadsGlobal(server, update + g_FrameTimeReadOffsets[i], "gpGlobals"), "frametime read: mov eax, gpGlobals");
	}

	uint32_t activate = server.Symbol(SYM_CVOMIT_ACTIVATEABILITY);
	const unsigned char* delay = server.At(activate + VOMIT_DELAY_PATCH_OFFSET, VOMIT_DELAY_PATCH_OPCODE_SIZE + 4);
	uint32_t constant = 0;
	float value = 0.0f;
	if (delay != NULL)
	{
		memcpy(&constant, delay + VOMIT_DELAY_PATCH_OPCODE_SIZE, 4);
		const unsigned char* c = server.At(constant, 4);
		if (c != NULL)
		{
			memcpy(&value, c, 4);
		}
	}
	Check(activate != 0 && delay != NULL && memcmp(delay, VOMIT_DELAY_PATCH_OPCODE, VOMIT_DELAY_PATCH_OPCODE_SIZE) == 0
		&& value == VOMIT_DELAY_VANILLA, "vomit delay: addss xmm0, [0.25]");

	Check(LoadsGlobal(server, update + VOMIT_PIERCE_TRACE_LOAD_OFFSET, SYM_ENGINETRACE), "vomit pierce: mov eax, enginetrace");
	Check(BytesAt(server, update + VOMIT_PIERCE_TRACE_VTABLE_OFFSET, "\x8B\x10", 2), "vomit pierce: mov edx, [eax]");
	Check(BytesAt(server, update + VOMIT_PIERCE_TRACE_CALL_OFFSET, "\xFF\x52\x14", 3), "vomit pierce: call [edx+14h] (TraceRay)");
	Check(BranchesTo(server, update + VOMIT_PIERCE_COLLIDE_CALL_OFFSET, 0xE8, SYM_ONVOMITCOLLIDE), "vomit pierce: call OnVomitCollide");
	Check(LoadsGlobal(server, update + VOMIT_PIERCE_FINISH_LOAD_OFFSET, SYM_LAGCOMPENSATION), "vomit pierce: mov eax, lagcompensation");
	Check(BytesAt(server, update + VOMIT_PIERCE_FINISH_VTABLE_OFFSET, "\x8B\x10", 2), "vomit pierce: mov edx, [eax]");
	Check(BytesAt(server, update + VOMIT_PIERCE_FINISH_CALL_OFFSET, "\xFF\x52\x04", 3), "vomit pierce: call [edx+4] (FinishLagCompensation)");
	Check(server.Symbol(SYM_CBASEENTITY_GETTEAMNUMBER) != 0, "CBaseEntity::GetTeamNumber symbol");

	uint32_t lagcomp = server.Symbol(SYM_CTERRORPLAYER_WANTSLAGCOMPENSATIONONENTITY);
	Check(lagcomp != 0 && BytesAt(server, lagcomp + LAGCOMP_WINDOW_ANCHOR_OFFSET, LAGCOMP_WINDOW_ANCHOR_BYTES, LAGCOMP_WINDOW_ANCHOR_BYTES_SIZE),
		"lag comp window: usercmd checks");
	Check(lagcomp != 0 && BytesAt(server, lagcomp + LAGCOMP_WINDOW_PATCH_OFFSET, LAGCOMP_WINDOW_PATCH_BYTES, LAGCOMP_WINDOW_PATCH_BYTES_SIZE),
		"lag comp window: cmp ecx, 5");

	printf("engine_srv.so\n");
	uint32_t baseClient = engine.Symbol(SYM_CBASECLIENT_SETUPDATERATE);
	uint32_t gameClient = engine.Symbol(SYM_CGAMECLIENT_SETUPDATERATE);
	Check(baseClient != 0 && BytesAt(engine, baseClient + UPDATERATE_BASECLIENT_CLAMP_OFFSET, UPDATERATE_BASECLIENT_CLAMP_BYTES,
		UPDATERATE_BASECLIENT_CLAMP_BYTES_SIZE), "CBaseClient::SetUpdateRate: clamp to 100, snapshot interval");
	Check(gameClient != 0 && BytesAt(engine, gameClient + UPDATERATE_GAMECLIENT_CLAMP_OFFSET, UPDATERATE_GAMECLIENT_CLAMP_BYTES,
		UPDATERATE_GAMECLIENT_CLAMP_BYTES_SIZE), "CGameClient::SetUpdateRate: clamp to 100");
	Check(BranchesTo(engine, gameClient + UPDATERATE_GAMECLIENT_JMP_OFFSET, 0xE9, SYM_CBASECLIENT_SETUPDATERATE),
		"CGameClient::SetUpdateRate: jmp CBaseClient::SetUpdateRate");

	printf(g_Failures == 0 ? "all patch sites match\n" : "%d patch site(s) don't match\n", g_Failures);
	return g_Failures == 0 ? 0 : 1;
}
