#include "Body.h"

#include "Utilities/Debug.h"
#include <Helpers/Macro.h>
#include <ScenarioClass.h>
#include <Unsorted.h>
#include <CCINIClass.h>
#include <SessionClass.h>
#include <Phobos.version.h>
#include <Ext/Event/Body.h>

// ============================================================================
// RANDOM STATE INITIALIZATION HOOKS
// ============================================================================
//
// These hooks intercept Init_Random to restore (playback) or save (recording)
// the complete RNG state at game initialization.
//
// HOOK SEQUENCE:
// 1. InitRandom_CheckReplayMode (0x52FC42) - Before RNG initialization
//    - Detects playback mode from spawn.ini
//    - If playback: Reads replay header and restores Random state
//    - If recording: Allows normal RNG initialization
//
// 2. InitRandom_AfterInit_SaveState (0x52FE43) - After RNG initialization
//    - If recording: Saves complete state to replay.dat header
//    - If playback: Does nothing (already restored in hook 1)
//
// ============================================================================

//Remind me why we don't just merge these?

// Track if we've already handled Init_Random to only restore/save on first call
static bool g_InitRandomHandled = false;
// Track if we've already saved the recording state header
static bool g_RecordingStateSaved = false;

// Hook before the recording flag check in Init_Random
// Address 0x52FC42: test byte ptr [RecordingFlag],0x2
DEFINE_HOOK(0x52FC42, InitRandom_CheckReplayMode, 0x7)
{
	// Only handle the first Init_Random call (game's call, not Spawner's)
	if (g_InitRandomHandled)
	{
		Debug::Log("[InitRandom] Already handled, skipping (this is Spawner's call)\n");
		return 0;
	}

	Debug::Log("[InitRandom] First call detected at frame %d\n", Unsorted::CurrentFrame);
	g_InitRandomHandled = true;

	// Check if we're in replay playback mode
	char replayFilePath[MAX_PATH] = {0};
	bool isReplayMode = IsReplayPlaybackMode(replayFilePath, sizeof(replayFilePath));

	if (isReplayMode)
	{
		Debug::Log("[InitRandom] PLAYBACK mode - loading from: %s\n", replayFilePath);

		// Set ObserverMode early so the loading screen patches (spectators.asm) see it.
		// We read the setting directly from spawn.ini here because LoadPlaybackSettings()
		// hasn't been called yet. MakeObserver() / HouseClass::Observer are set later at
		// ScenarioClass_Start because CurrentPlayer isn't populated yet at this point.
		{
			CCINIClass* pINI = CCINIClass::LoadINIFile("spawn.ini");
			const bool observerMode = pINI
				? pINI->ReadBool("Settings", "ReplayObserverMode", true)
				: true;
			if (pINI)
				CCINIClass::UnloadINIFile(pINI);

			if (observerMode)
				Game::ObserverMode = true;
		}

		ReplayHeader header;
		if (!ReadReplayHeader(replayFilePath, header, true))
		{
			Debug::Log("[InitRandom] ERROR: Failed to read/validate replay header\n");
			if (header.PhobosVersionMajor != VERSION_MAJOR ||
				header.PhobosVersionMinor != VERSION_MINOR ||
				header.PhobosVersionRevision != VERSION_REVISION ||
				header.PhobosVersionPatch != VERSION_PATCH)
			{
				Debug::Log("[InitRandom] ERROR: Phobos version mismatch!\n");
				Debug::Log("[InitRandom]   Replay version: %d.%d.%d.%d\n",
					header.PhobosVersionMajor, header.PhobosVersionMinor,
					header.PhobosVersionRevision, header.PhobosVersionPatch);
				Debug::Log("[InitRandom]   Current version: %d.%d.%d.%d\n",
					VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION, VERSION_PATCH);
				Debug::Log("[InitRandom]   Playback blocked.\n");
			}
			return 0;  // Fall back to normal init
		}

		// Restore Seed
		Game::Seed = header.Seed;
		Debug::Log("[InitRandom] Restored Seed = %08X\n", Game::Seed);

		// Restore ScenarioClass::Random
		if (ScenarioClass::Instance)
		{
			ScenarioClass::Instance->Random.Next1 = header.RandomNext1;
			ScenarioClass::Instance->Random.Next2 = header.RandomNext2;
			memcpy(ScenarioClass::Instance->Random.Table, header.RandomizerTable,
				sizeof(header.RandomizerTable));
			ScenarioClass::Instance->UniqueID = header.UniqueIDCounter;

			Debug::Log("[InitRandom] Restored Random: Next1=%d, Next2=%d, UniqueID=%d\n",
				header.RandomNext1, header.RandomNext2, header.UniqueIDCounter);
		}
		else
		{
			Debug::Log("[InitRandom] ScenarioClass::Instance not available yet\n");
		}

		// Jump to part that prints "Seed is XYXYXYY" and returns
		// (skip all the sys time init)
		R->EAX(Game::Seed);
		return 0x52FDF9;  // Jump before "Seed = EAX" assignment
	}

	Debug::Log("[InitRandom] RECORDING mode - executing normal initialization\n");
	return 0;
}

// Hook after Init_Random completes
// Address 0x52FE43: Right after CALL FUN_0065c6d0, before setting up for final MOVSD
// Hook size 5? = MOV ECX,0xfd (B9 FD 00 00 00)
DEFINE_HOOK(0x52FE43, InitRandom_AfterInit_SaveState, 0x5)
{
	// Only save on the first Init_Random call
	if (g_RecordingStateSaved)
	{
		Debug::Log("[InitRandom_AfterInit] Already saved, skipping\n");
		return 0;
	}

	Debug::Log("[InitRandom_AfterInit] Hook called at frame %d\n", Unsorted::CurrentFrame);

	// Check if we're not in replay mode (i.e. recording)
	bool isReplayMode = IsReplayPlaybackMode();
	Debug::Log("[InitRandom_AfterInit] isReplayMode=%d\n", isReplayMode);

	if (!isReplayMode && ScenarioClass::Instance)
	{
		g_RecordingStateSaved = true;
		Debug::Log("[InitRandom] RECORDING - Random state after first Init_Random:\n");
		Debug::Log("[InitRandom]   Seed=%08X, Next1=%d, Next2=%d, UniqueID=%d\n",
			Game::Seed,
			ScenarioClass::Instance->Random.Next1,
			ScenarioClass::Instance->Random.Next2,
			ScenarioClass::Instance->UniqueID);

		const char* replayPath = "replay.dat";
		HANDLE hFile = CreateFileA(replayPath, GENERIC_WRITE, 0, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			Debug::Log("[InitRandom] ERROR: Failed to create replay file\n");
			return 0;
		}

		ReplayHeader header;
		memset(&header, 0, sizeof(ReplayHeader));
		memcpy(header.Magic, REPLAY_MAGIC, 4);
		header.Version = REPLAY_VERSION;

		// Copy map name from ScenarioClass
		strncpy_s(header.MapName, sizeof(header.MapName),
			ScenarioClass::Instance->FileName, _TRUNCATE);

		header.StartFrame = 0;

		// Phobos version
		header.PhobosVersionMajor = VERSION_MAJOR;
		header.PhobosVersionMinor = VERSION_MINOR;
		header.PhobosVersionRevision = VERSION_REVISION;
		header.PhobosVersionPatch = VERSION_PATCH;

		// Game version
		strcpy_s(header.GameVersionString, "1.006"); //TODO

		// Game mode (Skirmish, LAN, Internet, Campaign)
		header.GameMode = static_cast<uint32_t>(SessionClass::Instance.GameMode);

		header.UniqueIDCounter = ScenarioClass::Instance->UniqueID;
		header.Seed = Game::Seed;
		header.RandomNext1 = ScenarioClass::Instance->Random.Next1;
		header.RandomNext2 = ScenarioClass::Instance->Random.Next2;

		// Copy randomizer table
		memcpy(header.RandomizerTable, ScenarioClass::Instance->Random.Table,
			sizeof(header.RandomizerTable));

		// Read spawn files
		DWORD spawnIniSize = 0;
		DWORD spawnMapSize = 0;
		char* spawnIniContent = nullptr;
		char* spawnMapContent = nullptr;

		// Read spawn.ini
		HANDLE hSpawnIni = CreateFileA("spawn.ini", GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hSpawnIni != INVALID_HANDLE_VALUE)
		{
			spawnIniSize = GetFileSize(hSpawnIni, NULL);
			spawnIniContent = static_cast<char*>(malloc(spawnIniSize));
			if (spawnIniContent)
			{
				DWORD bytesRead;
				ReadFile(hSpawnIni, spawnIniContent, spawnIniSize, &bytesRead, NULL); //TODO check ret val
				spawnIniSize = bytesRead;
			}
			CloseHandle(hSpawnIni);
		}

		// Read spawnmap.ini
		HANDLE hSpawnMap = CreateFileA("spawnmap.ini", GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hSpawnMap != INVALID_HANDLE_VALUE)
		{
			spawnMapSize = GetFileSize(hSpawnMap, NULL);
			spawnMapContent = static_cast<char*>(malloc(spawnMapSize));
			if (spawnMapContent)
			{
				DWORD bytesRead;
				ReadFile(hSpawnMap, spawnMapContent, spawnMapSize, &bytesRead, NULL); //TODO check ret val
				spawnMapSize = bytesRead;
			}
			CloseHandle(hSpawnMap);
		}

		header.SpawnIniSize = spawnIniContent ? spawnIniSize : 0;
		header.SpawnMapSize = spawnMapContent ? spawnMapSize : 0;

		// Write header
		DWORD bytesWritten;
		WriteFile(hFile, &header, sizeof(ReplayHeader), &bytesWritten, NULL);

		// Write spawn.ini content
		if (spawnIniContent && spawnIniSize > 0)
		{
			WriteFile(hFile, spawnIniContent, spawnIniSize, &bytesWritten, NULL);
			free(spawnIniContent);
		}

		// Write spawnmap.ini content
		if (spawnMapContent && spawnMapSize > 0)
		{
			WriteFile(hFile, spawnMapContent, spawnMapSize, &bytesWritten, NULL);
			free(spawnMapContent);
		}

		CloseHandle(hFile);
		Debug::Log("[InitRandom] Saved replay header to %s\n", replayPath);
	}

	Debug::Log("[InitRandom_AfterInit] Returning from hook\n");
	return 0;
}

// Reset per-game statics so that returning to the main menu and starting a new game
// works correctly without needing to restart the process.
DEFINE_HOOK(0x685800, InitRandom_ScenarioEnd_Reset, 0x5)
{
	g_InitRandomHandled = false;
	g_RecordingStateSaved = false;
	return 0;
}
