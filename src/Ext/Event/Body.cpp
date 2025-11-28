#include "Body.h"

#include <Helpers/Macro.h>
#include <EventClass.h>
#include <ScenarioClass.h>
#include <Unsorted.h>
#include <AbstractClass.h>
#include <TargetClass.h>
#include <HouseClass.h>
#include <TechnoClass.h>
#include <FootClass.h>
#include <CCINIClass.h>
#include <stdio.h>
#include <time.h>
#include <set>
#include <TacticalClass.h>
#include <MapClass.h>
#include <ObjectClass.h>
#include <Phobos.version.h>

// FLOW:
// --------------------
// 1. Init_Random (0x52FC42) [Hooks.Random.cpp]
//    - First hook that fires during game initialization
//    - Checks spawn.ini to determine if we're in playback mode
//    - PLAYBACK: Reads replay header and restores Random state (Seed, Next1, Next2, Table, UniqueID)
//    - RECORDING: Allows normal initialization to proceed
//
// 2. Init_Random_AfterInit (0x52FE43) [Hooks.Random.cpp]
//    - Fires after Init_Random completes
//    - RECORDING: Saves complete game state to replay.dat header (Random state, spawn files, etc.)
//    - PLAYBACK: Does nothing (state already restored in step 1)
//
// 3. ScenarioClass_Start_ReplayInit (0x685659) [Body.cpp]
//    - Fires when scenario/mission starts
//    - Determines mode and initializes appropriate subsystem:
//      * PLAYBACK: Calls StartReplayPlayback() - opens replay file, loads settings
//      * RECORDING: Calls StartReplayRecording() - creates replay files, writes headers
//
// RECORDING FLOW (EACH FRAME):
// ----------------------------
// 1. LogicClass_Update_ReplayInjectEvents (0x55AFB3)
//    - Main per-frame hook
//    - Counts events for this frame
//    - Calls RecordFrameState() to save frame state (tactical pos, selection, etc.)
//    - Calls RecordEvent() for each event in the DoList
//    - Every 100 frames: Logs game statistics (units, buildings, kills per house)
//
// PLAYBACK FLOW (EACH FRAME):
// ---------------------------
// 1. LogicClass_Update_ReplayInjectEvents (0x55AFB3)
//    - Main per-frame hook
//    - Calls RestoreFrameState() to read and validate frame state
//    - Calls PlaybackFrameEvents() to read and inject events for this frame
//    - Every 100 frames: Logs game statistics for comparison
//
// SHUTDOWN FLOW:
// --------------
// 1. ScenarioClass_End_ReplayShutdown (0x685800)
//    - Fires when scenario ends
//    - Calls StopReplaySystem() to close all file handles and write footers
//
// FILES:
// ---------------
// replay.dat:
//   - ReplayHeader (magic, versions, Random state, spawn file sizes)
//   - spawn.ini content (SpawnIniSize bytes)
//   - spawnmap.ini content (SpawnMapSize bytes)
//   - Per-frame data:
//     * FrameStateRecord (frame #, tactical pos, selection CRC, event count, UniqueID)
//     * Selected object UniqueIDs (variable count)
//     * EventClass records (variable count based on EventCountThisFrame)
//
// recordPretty.dat: Human-readable recording log with timestamps
// playbackLog.dat: Human-readable playback log (if ReplayDebugLog=true in spawn.ini)
//
// ============================================================================

static bool ReplayRecording = false;
static bool ReplayPlayback = false;

static HANDLE ReplayFile = nullptr;
static HANDLE PrettyReplayFile = nullptr;
static HANDLE PlaybackLogFile = nullptr;

static const char* ReplayFileName = "replay.dat";
static const char* PrettyReplayFileName = "recordPretty.dat";
static const char* PlaybackLogFileName = "playbackLog.dat";

static char* CustomReplayPath = {0};
static int ReadEvents = 0;
static bool RunPlaybackFunc = true;
static bool PrintedRecordingStartOnce = false;

static int ExpectedEventsThisFrame = 0;
static DWORD RecordedSpawnIniSize = 0;
static DWORD RecordedSpawnMapSize = 0;

struct PlaybackSettings
{
	bool shroudEnabled;
	bool lockedViewport;
	bool selectUnits;
	bool debugLog;
};

static PlaybackSettings g_PlaybackSettings = { false, true, true, false }; // defaults

void WriteToPlaybackLog(const char* text);

// Returns true if ReplayFile is set in spawn.ini
bool IsReplayPlaybackMode(char* outReplayPath, size_t outPathSize)
{
	CCINIClass* pINI = CCINIClass::LoadINIFile("spawn.ini");
	if (!pINI)
		return false;

	char replayFilePath[MAX_PATH] = { 0 };
	pINI->ReadString("Settings", "ReplayFile", "", replayFilePath, sizeof(replayFilePath));
	CCINIClass::UnloadINIFile(pINI);

	bool isPlayback = (replayFilePath[0] != '\0');

	if (isPlayback && outReplayPath && outPathSize > 0)
	{
		strncpy_s(outReplayPath, outPathSize, replayFilePath, _TRUNCATE);
	}

	return isPlayback;
}

void EnableReplayPlayback(char* replayFilePath)
{
	ReplayPlayback = true;
	ReplayRecording = false;
	CustomReplayPath = replayFilePath;
}

// Returns true if header was successfully read and validated
bool ReadReplayHeader(const char* replayFilePath, ReplayHeader& outHeader, bool validateVersion)
{
	HANDLE hFile = CreateFileA(replayFilePath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD bytesRead;
	bool success = ReadFile(hFile, &outHeader, sizeof(ReplayHeader), &bytesRead, NULL) &&
		bytesRead == sizeof(ReplayHeader);

	CloseHandle(hFile);

	if (!success)
		return false;

	// Validate magic and version
	if (memcmp(outHeader.Magic, REPLAY_MAGIC, 4) != 0 || outHeader.Version != REPLAY_VERSION)
		return false;

	if (validateVersion)
	{
		if (outHeader.PhobosVersionMajor != VERSION_MAJOR ||
			outHeader.PhobosVersionMinor != VERSION_MINOR ||
			outHeader.PhobosVersionRevision != VERSION_REVISION ||
			outHeader.PhobosVersionPatch != VERSION_PATCH)
		{
			return false;
		}
	}

	return true;
}

// Load playback settings from spawn.ini
void LoadPlaybackSettings()
{
	// Todo: do we load these here?
	CCINIClass* pINI = CCINIClass::LoadINIFile("spawn.ini");
	if (pINI)
	{
		g_PlaybackSettings.shroudEnabled = pINI->ReadBool("Settings", "ReplayShroudEnabled", false);
		g_PlaybackSettings.lockedViewport = pINI->ReadBool("Settings", "ReplayLockedViewport", true);
		g_PlaybackSettings.selectUnits = pINI->ReadBool("Settings", "ReplaySelectUnits", true);
		g_PlaybackSettings.debugLog = pINI->ReadBool("Settings", "ReplayDebugLog", false);
		CCINIClass::UnloadINIFile(pINI);
	}
}

const char* EventTypeToString(EventType eventType)
{
	switch (eventType)
	{
	case EventType::Empty: return "Empty";
	case EventType::PowerOn: return "PowerOn";
	case EventType::PowerOff: return "PowerOff";
	case EventType::Ally: return "Ally";
	case EventType::MegaMission: return "MegaMission";
	case EventType::MegaMissionF: return "MegaMissionF";
	case EventType::Idle: return "Idle";
	case EventType::Scatter: return "Scatter";
	case EventType::Destruct: return "Destruct";
	case EventType::Deploy: return "Deploy";
	case EventType::Detonate: return "Detonate";
	case EventType::Place: return "Place";
	case EventType::Options: return "Options";
	case EventType::GameSpeed: return "GameSpeed";
	case EventType::Produce: return "Produce";
	case EventType::Suspend: return "Suspend";
	case EventType::Abandon: return "Abandon";
	case EventType::Primary: return "Primary";
	case EventType::SpecialPlace: return "SpecialPlace";
	case EventType::Exit: return "Exit";
	case EventType::Animation: return "Animation";
	case EventType::Repair: return "Repair";
	case EventType::Sell: return "Sell";
	case EventType::SellCell: return "SellCell";
	case EventType::Special: return "Special";
	case EventType::FrameSync: return "FrameSync";
	case EventType::Message: return "Message";
	case EventType::ResponseTime: return "ResponseTime";
	case EventType::FrameInfo: return "FrameInfo";
	case EventType::SaveGame: return "SaveGame";
	case EventType::Archive: return "Archive";
	case EventType::AddPlayer: return "AddPlayer";
	case EventType::Timing: return "Timing";
	case EventType::ProcessTime: return "ProcessTime";
	case EventType::PageUser: return "PageUser";
	case EventType::RemovePlayer: return "RemovePlayer";
	case EventType::LatencyFudge: return "LatencyFudge";
	case EventType::MegaFrameInfo: return "MegaFrameInfo";
	case EventType::PacketTiming: return "PacketTiming";
	case EventType::AboutToExit: return "AboutToExit";
	case EventType::FallbackHost: return "FallbackHost";
	case EventType::AddressChange: return "AddressChange";
	case EventType::PlanConnect: return "PlanConnect";
	case EventType::PlanCommit: return "PlanCommit";
	case EventType::PlanNodeDelete: return "PlanNodeDelete";
	case EventType::AllCheer: return "AllCheer";
	case EventType::AbandonAll: return "AbandonAll";
	default:
	{
		static char unknownBuf[32];
		sprintf_s(unknownBuf, sizeof(unknownBuf),
				  "Unknown(%u)", (unsigned)eventType);
		return unknownBuf;
	}
	}
}

void GetTimestamp(char* buffer, size_t bufferSize)
{
	time_t now = time(NULL);
	struct tm* timeinfo = localtime(&now);
	strftime(buffer, bufferSize, "%H:%M:%S", timeinfo);
}

void WriteToPrettyFile(const char* text)
{
	if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE)
	{
		DWORD bytesWritten;
		DWORD textLength = strlen(text);
		WriteFile(PrettyReplayFile, text, textLength, &bytesWritten, NULL);
		FlushFileBuffers(PrettyReplayFile);
	}
}

void WriteToPlaybackLog(const char* text)
{
	if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
	{
		DWORD bytesWritten;
		DWORD textLength = strlen(text);
		WriteFile(PlaybackLogFile, text, textLength, &bytesWritten, NULL);
		FlushFileBuffers(PlaybackLogFile);
	}
}

void LogFrameState(const FrameStateRecord& frameState, void (*writeFunc)(const char*), bool logSelectedObjects)
{
	if (!writeFunc)
		return;

	char logLine[512];
	sprintf_s(logLine, sizeof(logLine),
		"[FRAME STATE] Frame:%u TacticalPos:(%d,%d) SelectedCount:%d CRC:0x%08X Events:%d UniqueID:%d Seed:%d Next1:%d Next2:%d\r\n",
		frameState.FrameNumber,
		frameState.TacticalPos.X,
		frameState.TacticalPos.Y,
		frameState.SelectedObjectCount,
		frameState.SelectedObjectCRC,
		frameState.EventCountThisFrame,
		frameState.UniqueIDCounter,
		Game::Seed,
		ScenarioClass::Instance ? ScenarioClass::Instance->Random.Next1 : -1,
		ScenarioClass::Instance ? ScenarioClass::Instance->Random.Next2 : -1);
	writeFunc(logLine);

	if (logSelectedObjects && frameState.SelectedObjectCount > 0)
	{
		auto& currentObjects = ObjectClass::CurrentObjects;
		sprintf_s(logLine, sizeof(logLine),
			"  [SELECTED OBJECTS] Count:%d IDs: ",
			frameState.SelectedObjectCount);
		writeFunc(logLine);

		for (int i = 0; i < currentObjects.Count && i < 10; i++) // first 10
		{
			ObjectClass* pObj = currentObjects.Items[i];
			if (pObj)
			{
				sprintf_s(logLine, sizeof(logLine), "0x%08X ", pObj->UniqueID);
				writeFunc(logLine);
			}
		}
		if (currentObjects.Count > 10)
		{
			sprintf_s(logLine, sizeof(logLine), "... (%d more)", currentObjects.Count - 10);
			writeFunc(logLine);
		}
		writeFunc("\r\n");
	}
}

// Helper function to close a file handle with optional footer
void CloseFileWithFooter(HANDLE& fileHandle, void (*writeFunc)(const char*), const char* footerTitle)
{
	if (!fileHandle || fileHandle == INVALID_HANDLE_VALUE)
		return;

	if (writeFunc && footerTitle)
	{
		char timestamp[32];
		GetTimestamp(timestamp, sizeof(timestamp));

		char footer[256];
		sprintf_s(footer, sizeof(footer),
			"========================================\r\n"
			"=== %s ===\r\n"
			"Timestamp: %s\r\n",
			footerTitle, timestamp);

		writeFunc(footer);
	}

	CloseHandle(fileHandle);
	fileHandle = nullptr;
}

void LogGameStateStatistics(void (*writeFunc)(const char*))
{
	if (!writeFunc)
		return;

	char logLine[512];

	// Count units and buildings per house
	int unitCountByHouse[8] = { 0 };
	int buildingCountByHouse[8] = { 0 };
	int totalUnits = 0;
	int totalBuildings = 0;

	for (int i = 0; i < AbstractClass::Array.Count; i++)
	{
		auto pObj = AbstractClass::Array.GetItem(i);
		if (!pObj)
			continue;

		int houseIdx = pObj->GetOwningHouseIndex();
		if (houseIdx < 0 || houseIdx >= 8)
			continue;

		AbstractType type = pObj->WhatAmI();
		if (type == AbstractType::Unit || type == AbstractType::Infantry || type == AbstractType::Aircraft)
		{
			unitCountByHouse[houseIdx]++;
			totalUnits++;
		}
		else if (type == AbstractType::Building)
		{
			buildingCountByHouse[houseIdx]++;
			totalBuildings++;
		}
	}

	sprintf_s(logLine, sizeof(logLine),
		"=== Frame %u Game State: TotalUnits:%d TotalBuildings:%d UniqueIDCounter:%d Seed:%d Next1:%d Next2:%d ===\r\n",
		Unsorted::CurrentFrame,
		totalUnits,
		totalBuildings,
		ScenarioClass::Instance ? ScenarioClass::Instance->UniqueID : -1,
		Game::Seed,
		ScenarioClass::Instance ? ScenarioClass::Instance->Random.Next1 : -1,
		ScenarioClass::Instance ? ScenarioClass::Instance->Random.Next2 : -1);
	writeFunc(logLine);

	// Log per-house statistics
	for (int h = 0; h < 8; h++)
	{
		if (unitCountByHouse[h] > 0 || buildingCountByHouse[h] > 0)
		{
			HouseClass* pHouse = HouseClass::Array.GetItemOrDefault(h);
			int kills = pHouse ? pHouse->TotalKilledUnits : 0;
			int buildingKills = pHouse ? pHouse->TotalKilledBuildings : 0;

			sprintf_s(logLine, sizeof(logLine),
				"  House H%d: Units:%d Buildings:%d Kills:%d BldKills:%d\r\n",
				h, unitCountByHouse[h], buildingCountByHouse[h], kills, buildingKills);
			writeFunc(logLine);
		}
	}
}

// For help with logging to the pretty files
const char* GetHouseInfo(AbstractClass* pObj, char* buffer, size_t bufferSize)
{
	if (!pObj)
	{
		sprintf_s(buffer, bufferSize, "House:NULL");
		return buffer;
	}

	HouseClass* pHouse = pObj->GetOwningHouse();
	int houseIdx = pObj->GetOwningHouseIndex();

	const char* name = "NULL";

	if (pHouse) //fail in playback?
	{
		if (pHouse->UIName)
		{
			// convert wchar> narrow
			static char converted[64];
			wcstombs_s(nullptr, converted, pHouse->UIName, _TRUNCATE);
			name = converted;
		}
		else
		{
			name = "?";
		}

		sprintf_s(buffer, bufferSize, "House:H%d(%s)", houseIdx, name);
	}
	else
	{
		sprintf_s(buffer, bufferSize, "House:H%d(NULL)", houseIdx);
	}

	return buffer;
}

// For help with logging to the pretty files
void GetTargetDetails(TargetClass target, char* buffer, size_t bufferSize)
{
	if (target.m_ID == 0)
	{
		sprintf_s(buffer, bufferSize, "0x%08X(NONE)", target.m_ID);
		return;
	}

	AbstractClass* pObj = target.As_Abstract();
	if (pObj)
	{
		char houseInfo[64];
		GetHouseInfo(pObj, houseInfo, sizeof(houseInfo));
		sprintf_s(buffer, bufferSize, "0x%08X(Type:%d,%s)",
			target.m_ID, static_cast<int>(pObj->WhatAmI()), houseInfo);
	}
	else
	{
		sprintf_s(buffer, bufferSize, "0x%08X(INVALID)", target.m_ID);
	}
}

// For help with logging to the pretty files
void FormatEventDetails(const EventClass* pEvent, char* buffer, size_t bufferSize)
{
	buffer[0] = '\0';

	switch (pEvent->Type)
	{
	case EventType::Ally:
		sprintf_s(buffer, bufferSize, " -> AllyWithHouse:%d", pEvent->Ally.HouseID);
		break;

	case EventType::GameSpeed:
		sprintf_s(buffer, bufferSize, " -> Speed:%d", pEvent->GameSpeed.GameSpeed);
		break;

	case EventType::Place:
		sprintf_s(buffer, bufferSize, " -> Type:%d HeapID:%d Loc:(%d,%d)",
			static_cast<int>(pEvent->Place.RTTIType),
			pEvent->Place.HeapID,
			pEvent->Place.Location.X,
			pEvent->Place.Location.Y);
		break;

	case EventType::Produce:
		sprintf_s(buffer, bufferSize, " -> Type:%d HeapID:%d",
			static_cast<int>(pEvent->Produce.RTTIType),
			pEvent->Produce.HeapID);
		break;

	case EventType::Suspend:
		sprintf_s(buffer, bufferSize, " -> Type:%d HeapID:%d",
			static_cast<int>(pEvent->Suspend.RTTIType),
			pEvent->Suspend.HeapID);
		break;

	case EventType::Abandon:
		sprintf_s(buffer, bufferSize, " -> Type:%d HeapID:%d",
			static_cast<int>(pEvent->Abandon.RTTIType),
			pEvent->Abandon.HeapID);
		break;

	case EventType::SellCell:
		sprintf_s(buffer, bufferSize, " -> Loc:(%d,%d)",
			pEvent->SellCell.Location.X, pEvent->SellCell.Location.Y);
		break;

	case EventType::Animation:
		sprintf_s(buffer, bufferSize, " -> AnimID:%d HouseID:%d Loc:(%d,%d)",
			pEvent->Animation.AnimID,
			pEvent->Animation.HouseID,
			pEvent->Animation.Location.X,
			pEvent->Animation.Location.Y);
		break;

	case EventType::MegaMission:
	{
		char whomDetails[128];
		char targetDetails[128];
		char destDetails[128];

		GetTargetDetails(pEvent->MegaMission.Whom, whomDetails, sizeof(whomDetails));
		GetTargetDetails(pEvent->MegaMission.Target, targetDetails, sizeof(targetDetails));
		GetTargetDetails(pEvent->MegaMission.Destination, destDetails, sizeof(destDetails));

		sprintf_s(buffer, bufferSize, " -> Mission:%d Planning:%s Whom:%s Target:%s Dest:%s",
			static_cast<int>(pEvent->MegaMission.Mission),
			pEvent->MegaMission.IsPlanningEvent ? "Y" : "N",
			whomDetails,
			targetDetails,
			destDetails);
	}
	break;

	case EventType::MegaMissionF:
	{
		char whomDetails[128];
		char targetDetails[128];
		char destDetails[128];

		GetTargetDetails(pEvent->MegaMissionF.Whom, whomDetails, sizeof(whomDetails));
		GetTargetDetails(pEvent->MegaMissionF.Target, targetDetails, sizeof(targetDetails));
		GetTargetDetails(pEvent->MegaMissionF.Destination, destDetails, sizeof(destDetails));

		sprintf_s(buffer, bufferSize, " -> Mission:%d Whom:%s Target:%s Dest:%s Speed:%d MaxSpeed:%d",
			static_cast<int>(pEvent->MegaMissionF.Mission),
			whomDetails,
			targetDetails,
			destDetails,
			pEvent->MegaMissionF.Speed,
			pEvent->MegaMissionF.MaxSpeed);
	}
	break;

	case EventType::Idle:
	case EventType::Scatter:
	case EventType::Deploy:
	case EventType::Detonate:
	case EventType::Repair:
	case EventType::Sell:
	case EventType::Primary:
	{
		// All these events use the same structure with a "Whom" TargetClass
		TargetClass whom;
		if (pEvent->Type == EventType::Idle)
			whom = pEvent->Idle.Whom;
		else if (pEvent->Type == EventType::Scatter)
			whom = pEvent->Scatter.Whom;
		else if (pEvent->Type == EventType::Deploy)
			whom = pEvent->Deploy.Whom;
		else if (pEvent->Type == EventType::Detonate)
			whom = pEvent->Detonate.Whom;
		else if (pEvent->Type == EventType::Repair)
			whom = pEvent->Repair.Whom;
		else if (pEvent->Type == EventType::Sell)
			whom = pEvent->Sell.Whom;
		else // Primary
			whom = pEvent->Primary.Whom;

		char whomDetails[128];
		GetTargetDetails(whom, whomDetails, sizeof(whomDetails));
		sprintf_s(buffer, bufferSize, " -> Whom:%s", whomDetails);
	}
	break;

	case EventType::PowerOn:
	case EventType::PowerOff:
	{
		TargetClass target = (pEvent->Type == EventType::PowerOn) ?
			pEvent->Poweron.Target : pEvent->Poweroff.Target;
		char targetDetails[128];
		GetTargetDetails(target, targetDetails, sizeof(targetDetails));
		sprintf_s(buffer, bufferSize, " -> Target:%s", targetDetails);
	}
	break;

	case EventType::Archive: //what is this?
	{
		char whom1Details[128];
		char whom2Details[128];
		GetTargetDetails(pEvent->Archive.Whom1, whom1Details, sizeof(whom1Details));
		GetTargetDetails(pEvent->Archive.Whom2, whom2Details, sizeof(whom2Details));
		sprintf_s(buffer, bufferSize, " -> Whom1:%s Whom2:%s", whom1Details, whom2Details);
	}
	break;

	case EventType::RemovePlayer:
		sprintf_s(buffer, bufferSize, " -> HouseID:%d", pEvent->RemovePlayer.HouseID);
		break;

	case EventType::SpecialPlace:
		sprintf_s(buffer, bufferSize, " -> ID:%d Loc:(%d,%d)",
			pEvent->SpecialPlace.ID,
			pEvent->SpecialPlace.Location.X,
			pEvent->SpecialPlace.Location.Y);
		break;

	case EventType::FrameInfo:
		sprintf_s(buffer, bufferSize, " -> CRC:0x%08X CmdCnt:%d Delay:%d",
			pEvent->FrameInfo.CRC,
			pEvent->FrameInfo.CommandCount,
			pEvent->FrameInfo.Delay);
		break;

	default:
	{
		// Unknown event – dump some bytes
		char hexDump[128];
		char* pos = hexDump;

		pos += sprintf_s(pos, sizeof(hexDump) - (pos - hexDump), " -> [Unknown:%u Data:",
						 (unsigned)pEvent->Type);

		for (int i = 0; i < 16; i++)
			pos += sprintf_s(pos, sizeof(hexDump) - (pos - hexDump), "%02X ",
							 (unsigned char)pEvent->DataBuffer[i]);

		sprintf_s(pos, sizeof(hexDump) - (pos - hexDump), "]");
		strcpy_s(buffer, bufferSize, hexDump);
	}
	break;
	}
}

void StartReplayRecording()
{
	ReplayRecording = true;
	ReplayPlayback = false;
	ReadEvents = 0;

	if (ReplayFile && ReplayFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(ReplayFile);
		ReplayFile = nullptr;
	}

	// Close pretty file if open
	if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(PrettyReplayFile);
		PrettyReplayFile = nullptr;
	}

	// Open replay file
	ReplayFile = CreateFileA(ReplayFileName, GENERIC_WRITE, 0, NULL,
							OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (ReplayFile && ReplayFile != INVALID_HANDLE_VALUE)
	{
		// Header already writte, append events to the end.
		SetFilePointer(ReplayFile, 0, NULL, FILE_END);
	}

	// Open pretty replay file
	PrettyReplayFile = CreateFileA(PrettyReplayFileName, GENERIC_WRITE, 0, NULL,
								  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE)
	{
		char timestamp[32];
		GetTimestamp(timestamp, sizeof(timestamp));

		char header[2048];
		sprintf_s(header, sizeof(header),
			"=== REPLAY RECORDING STARTED ===\r\n"
			"Timestamp: %s\r\n"
			"Map: %s\r\n" // todo
			"\r\n"
			"SEED INFORMATION:\r\n"
			"  Game::Seed (from spawn.ini): %u\r\n"
			"\r\n"
			"Start Frame: %d\r\n"
			"Format: Time Frame HouseIdx EventType Details\r\n"
			"========================================\r\n",
			timestamp,
			ScenarioClass::Instance->FileName,
			Game::Seed,
			Unsorted::CurrentFrame);

		WriteToPrettyFile(header);
	}
}

void StartReplayPlayback()
{
	ReplayRecording = false;
	ReplayPlayback = true;
	ReadEvents = 0;
	RunPlaybackFunc = true;

	if (ReplayFile && ReplayFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(ReplayFile);
	}

	// Close pretty file during playback
	if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE)
	{
		WriteToPrettyFile("=== RECORDING STOPPED FOR PLAYBACK ===\r\n");
		CloseHandle(PrettyReplayFile);
		PrettyReplayFile = nullptr;
	}

	// Load playback settings from spawn.ini
	LoadPlaybackSettings();

	// Open playback log file
	if (g_PlaybackSettings.debugLog)
	{
		PlaybackLogFile = CreateFileA(PlaybackLogFileName, GENERIC_WRITE, 0, NULL,
									 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}
	else
	{
		PlaybackLogFile = nullptr;
	}

	// Which replay file to open
	const char* replayFileToOpen = (CustomReplayPath[0] != '\0') ? CustomReplayPath : ReplayFileName;

	// Open replay file for reading events
	ReplayFile = CreateFileA(replayFileToOpen, GENERIC_READ, FILE_SHARE_READ, NULL,
							OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (ReplayFile && ReplayFile != INVALID_HANDLE_VALUE)
	{
		char logMsg[512];
		sprintf_s(logMsg, sizeof(logMsg), "Successfully opened replay file: %s\r\n", replayFileToOpen);
		WriteToPlaybackLog(logMsg);

		// Skip past header and spawn inis
		ReplayHeader header;
		DWORD bytesRead;

		if (ReadFile(ReplayFile, &header, sizeof(ReplayHeader), &bytesRead, NULL) &&
			bytesRead == sizeof(ReplayHeader))
		{
			// Validate magic
			if (memcmp(header.Magic, REPLAY_MAGIC, 4) == 0 && header.Version == REPLAY_VERSION)
			{
				// Skip over spawn files to get to event stream
				RecordedSpawnIniSize = header.SpawnIniSize;
				RecordedSpawnMapSize = header.SpawnMapSize;
				DWORD totalSpawnDataSize = RecordedSpawnIniSize + RecordedSpawnMapSize;
				if (totalSpawnDataSize > 0)
				{
					SetFilePointer(ReplayFile, totalSpawnDataSize, NULL, FILE_CURRENT);
				}

				// Log that playback started
				if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
				{
					char timestamp[32];
					GetTimestamp(timestamp, sizeof(timestamp));

					char logHeader[2048];
					sprintf_s(logHeader, sizeof(logHeader),
						"=== REPLAY PLAYBACK STARTED ===\r\n"
						"Timestamp: %s\r\n"
						"Replay File: %s\r\n"
						"Map: %s\r\n"
						"Seed: %d\r\n"
						"\r\n"
						"Format: [Time] CurrentFrame EventFrame EventType Details\r\n"
						"========================================\r\n",
						timestamp,
						replayFileToOpen,
						header.MapName,
						Game::Seed);

					WriteToPlaybackLog(logHeader);
				}
			}
			else
			{
				// Invalid header
				if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
				{
					WriteToPlaybackLog("ERROR: Invalid replay file header!\r\n");
				}
			}
		}
	}
	else
	{
		// Log the error
		DWORD error = GetLastError();
		char logMsg[512];
		sprintf_s(logMsg, sizeof(logMsg),
			"FAILED to open replay file: %s\r\nError code: %d (0x%X)\r\n",
			replayFileToOpen, error, error);
		WriteToPlaybackLog(logMsg);
	}
}

void StopReplaySystem()
{
	ReplayRecording = false;
	ReplayPlayback = false;
	RunPlaybackFunc = false;

	if (ReplayFile && ReplayFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(ReplayFile);
		ReplayFile = nullptr;
	}

	CloseFileWithFooter(PrettyReplayFile, WriteToPrettyFile, "REPLAY RECORDING ENDED");
	CloseFileWithFooter(PlaybackLogFile, WriteToPlaybackLog, "REPLAY PLAYBACK ENDED");
}

// TODO:
// Unused for now (haven't looked at Multiplayer)
// These are the events Iran was skipping
// Looking at RA1's source, I don't think we need to do all of these
// But we'll need to do some stuff
bool IsTimingEvent(EventType eventType)
{
	switch (eventType)
	{
	case EventType::ResponseTime:
	case EventType::FrameInfo:
	case EventType::Timing:
	case EventType::ProcessTime:
	case EventType::PacketTiming:
	case EventType::MegaFrameInfo:
	case EventType::FrameSync:
		return true;
	default:
		return false;
	}
}

// ============================================================================
// FRAME STATE: RECORDING
// ============================================================================
// Records per-frame game state during replay recording
// Similar to RA1's Do_Record_Playback (CONQUER.CPP lines 5094-5228)
void RecordFrameState(int eventsThisFrame)
{
	if (!ReplayRecording || !ReplayFile || ReplayFile == INVALID_HANDLE_VALUE)
		return;

	FrameStateRecord frameState;
	frameState.FrameNumber = static_cast<uint32_t>(Unsorted::CurrentFrame);
	
	// Record tactical position (viewport coords)
	if (TacticalClass::Instance)
	{
		frameState.TacticalPos = TacticalClass::Instance->TacticalPos;
	}
	else
	{
		frameState.TacticalPos = { 0, 0 };
	}

	// Record selected objects (RA1's CurrentObject list)
	// TODO: selecting objects makes the game crawl, I'm doing something wrong
	auto& currentObjects = ObjectClass::CurrentObjects;
	frameState.SelectedObjectCount = currentObjects.Count;
	
	// Calculate CRC/sum of selected objects (RA1 CONQUER.CPP lines 5125-5130)
	uint32_t sum = 0;
	for (int i = 0; i < currentObjects.Count; i++)
	{
		ObjectClass* pObj = currentObjects.Items[i];
		if (pObj)
		{
			// UniqueID as the target equivalent?
			sum += static_cast<uint32_t>(pObj->UniqueID);
		}
	}
	frameState.SelectedObjectCRC = sum;
	frameState.EventCountThisFrame = eventsThisFrame;
	frameState.UniqueIDCounter = ScenarioClass::Instance ? ScenarioClass::Instance->UniqueID : 0;

	// Write the frame state record
	DWORD bytesWritten;
	WriteFile(ReplayFile, &frameState, sizeof(FrameStateRecord), &bytesWritten, NULL);

	// Write selected object UniqueIDs (RA1 CONQUER.CPP lines 5135-5138)
	for (int i = 0; i < currentObjects.Count; i++)
	{
		ObjectClass* pObj = currentObjects.Items[i];
		if (pObj)
		{
			uint32_t uniqueID = static_cast<uint32_t>(pObj->UniqueID);
			WriteFile(ReplayFile, &uniqueID, sizeof(uint32_t), &bytesWritten, NULL);
		}
	}

	// Pretty logging
	if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE)
	{
		LogFrameState(frameState, WriteToPrettyFile, true);
	}
}

// Restores per-frame game state during replay playback
// Similar to RA1's Do_Record_Playback (CONQUER.CPP lines 5094-5228)
void RestoreFrameState()
{
	if (!ReplayPlayback || !ReplayFile || ReplayFile == INVALID_HANDLE_VALUE)
		return;

	FrameStateRecord frameState;
	DWORD bytesRead;

	// Read the frame state record
	if (!ReadFile(ReplayFile, &frameState, sizeof(FrameStateRecord), &bytesRead, NULL) ||
		bytesRead != sizeof(FrameStateRecord))
	{
		if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
		{
			WriteToPlaybackLog("ERROR: Failed to read FrameStateRecord\r\n");
		}
		RunPlaybackFunc = false;
		StopReplaySystem();
		return;
	}

	// Validate frame number
	if (frameState.FrameNumber != static_cast<uint32_t>(Unsorted::CurrentFrame))
	{
		if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
		{
			char logLine[256];
			sprintf_s(logLine, sizeof(logLine),
				"ERROR: Frame mismatch. Expected:%u Got:%u\r\n",
				Unsorted::CurrentFrame, frameState.FrameNumber);
			WriteToPlaybackLog(logLine);
		}
		RunPlaybackFunc = false;
		StopReplaySystem();
		return;
	}

	// Store expected event count for PlaybackFrameEvents()
	ExpectedEventsThisFrame = frameState.EventCountThisFrame;

 	// Restore UniqueID counter to keep counters synchronized
	// Note: this seems hacky.
	// What is happening is the first time a MegaMission event happens:
	//	in recording mode, the UniqueID counter will increase by one before the event is logged
	//	in playback mode, the UniqueID counter won't have had that increase, so it starts diverging.
	// As we are hooking at the beginning of the Logic updates, I am guessing that the UniqueID
	// increasing when recording is related to user input which runs before logic.
	// So when you click and move a unit, it draws a line. I think that line gets a UniqueID.
	// It's not critical to the playback (only unit UniqueIDs should be...(?)) so we'll just set the ID to what we recorded.
	// It works well and fixes the Mega Mission divergence. But something still wrong with keyboard shortcuts (grouping units/Using T) (maybe - double check)
	if (ScenarioClass::Instance)
	{
		
		if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
		{
			if (ScenarioClass::Instance->UniqueID != frameState.UniqueIDCounter)
			{
				ScenarioClass::Instance->UniqueID = frameState.UniqueIDCounter;

				char logLine[256];
				sprintf_s(logLine, sizeof(logLine),
					"  [RESTORED] UniqueID to %d\r\n",
					frameState.UniqueIDCounter);
				WriteToPlaybackLog(logLine);
			}
		}
	}

	// Restore viewport pos (RA1 CONQUER.CPP lines 5164-5168)
	if (g_PlaybackSettings.lockedViewport && TacticalClass::Instance)
	{
		if (frameState.TacticalPos.X != TacticalClass::Instance->TacticalPos.X ||
			frameState.TacticalPos.Y != TacticalClass::Instance->TacticalPos.Y)
		{
			TacticalClass::Instance->TacticalPos = frameState.TacticalPos;

			if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
			{
				char logLine[256];
				sprintf_s(logLine, sizeof(logLine),
					"  [RESTORED] TacticalPos to (%d,%d)\r\n",
					frameState.TacticalPos.X, frameState.TacticalPos.Y);
				WriteToPlaybackLog(logLine);
			}
		}
	}

	// Restore selected objects (RA1 CONQUER.CPP lines 5170-5203)
	auto& currentObjects = ObjectClass::CurrentObjects;

	uint32_t* selectedIDs = new uint32_t[frameState.SelectedObjectCount];
	for (int i = 0; i < frameState.SelectedObjectCount; i++)
	{
		if (!ReadFile(ReplayFile, &selectedIDs[i], sizeof(uint32_t), &bytesRead, NULL) ||
			bytesRead != sizeof(uint32_t))
		{
			if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
			{
				WriteToPlaybackLog("ERROR: Failed to read selected object IDs\r\n");
			}
			delete[] selectedIDs;
			RunPlaybackFunc = false;
			StopReplaySystem();
			return;
		}
	}
	
	// Calculate CRC of current selection
	uint32_t currentCRC = 0;
	for (int i = 0; i < currentObjects.Count; i++)
	{
		ObjectClass* pObj = currentObjects.Items[i];
		if (pObj)
		{
			currentCRC += static_cast<uint32_t>(pObj->UniqueID);
		}
	}

	// If CRC doesn't match, restore selection (RA1 CONQUER.CPP lines 5185-5202)
	if (g_PlaybackSettings.selectUnits &&
		(currentCRC != frameState.SelectedObjectCRC ||
		 currentObjects.Count != frameState.SelectedObjectCount))
	{
		if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
		{
			char logLine[512];
			sprintf_s(logLine, sizeof(logLine),
				"  [SELECTION MISMATCH] Restoring: Count=%d CRC=0x%08X\r\n",
				frameState.SelectedObjectCount, frameState.SelectedObjectCRC);
			WriteToPlaybackLog(logLine);
		}

		// Clear current selection
		MapClass::UnselectAll();

		// Re-select objects based on recorded UniqueIDs
		for (int i = 0; i < frameState.SelectedObjectCount; i++)
		{
			// Find object with this UniqueID
			ObjectClass* pObj = nullptr;
			for (int j = 0; j < AbstractClass::Array.Count; j++)
			{
				AbstractClass* pAbs = AbstractClass::Array.GetItem(j);
				if (pAbs && pAbs->UniqueID == selectedIDs[i])
				{
					pObj = abstract_cast<ObjectClass*>(pAbs);
					break;
				}
			}

			if (pObj)
			{
				pObj->Select();

				if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE && i < 5) // Log first 5
				{
					char logLine[256];
					sprintf_s(logLine, sizeof(logLine),
						"    [SELECTED] UniqueID:0x%08X Type:%d\r\n",
						selectedIDs[i], static_cast<int>(pObj->WhatAmI()));
					WriteToPlaybackLog(logLine);
				}
			}
			else if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE && i < 5)
			{
				char logLine[256];
				sprintf_s(logLine, sizeof(logLine),
					"    [NOT FOUND] UniqueID:0x%08X\r\n", selectedIDs[i]);
				WriteToPlaybackLog(logLine);
			}
		}
	}
	
	delete[] selectedIDs;

	// Log frame state
	if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
	{
		LogFrameState(frameState, WriteToPlaybackLog, false);
	}
}


void RecordEvent(EventClass* pEvent)
{
	if (!ReplayRecording)
		return;

	// TODO for multiplayer
	//if (IsTimingEvent(static_cast<unsigned char>(pEvent->Type)))
	//	return;

	// Only record events that haven't been executed yet (RA1 QUEUE.CPP line 3611)
	if (pEvent->IsExecuted)
		return;

	if (!ReplayFile || ReplayFile == INVALID_HANDLE_VALUE)
	{
		ReplayFile = CreateFileA(ReplayFileName, GENERIC_WRITE, 0, NULL,
								CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}

	// Initialize pretty file if not already done
	if (!PrettyReplayFile || PrettyReplayFile == INVALID_HANDLE_VALUE)
	{
		PrettyReplayFile = CreateFileA(PrettyReplayFileName, GENERIC_WRITE, 0, NULL,
									  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE)
		{
			char timestamp[32];
			GetTimestamp(timestamp, sizeof(timestamp));

			char header[512];
			sprintf_s(header, sizeof(header),
				"=== REPLAY RECORDING STARTED ===\r\n"
				"Timestamp: %s\r\n"
				"Format: [Time] Frame HouseIdx EventType Details\r\n"
				"========================================\r\n",
				timestamp);

			WriteToPrettyFile(header);
		}
	}

	// Record to binary file
	if (ReplayFile && ReplayFile != INVALID_HANDLE_VALUE)
	{
		DWORD bytesWritten;
		if (WriteFile(ReplayFile, pEvent, sizeof(EventClass), &bytesWritten, NULL))
		{
			FlushFileBuffers(ReplayFile);
		}
	}

	// Record to pretty file
	if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE)
	{
		char timestamp[32];
		char details[512];
		char logLine[1024];

		GetTimestamp(timestamp, sizeof(timestamp));
		FormatEventDetails(pEvent, details, sizeof(details));

		const char* eventName = EventTypeToString(pEvent->Type);

		sprintf_s(logLine, sizeof(logLine),
			"[%s] %6u H%2d %-15s%s\r\n",
			timestamp,
			pEvent->Frame,
			static_cast<int>(pEvent->HouseIndex),
			eventName,
			details);

		WriteToPrettyFile(logLine);

		// For MegaMission and MegaMissionF events, log it all
		// Not needed, but damn handy.
		if (pEvent->Type == EventType::MegaMission || pEvent->Type == EventType::MegaMissionF)
		{
			const char* eventPrefix = (pEvent->Type == EventType::MegaMissionF) ? "MegaMissionF" : "MegaMission";

			// Get Whom from appropriate event type
			TargetClass whom = (pEvent->Type == EventType::MegaMissionF) ?
				pEvent->MegaMissionF.Whom : pEvent->MegaMission.Whom;
			AbstractClass* pUnit = whom.As_Abstract();

			if (pUnit)
			{
				char houseInfo[64];
				GetHouseInfo(pUnit, houseInfo, sizeof(houseInfo));

				sprintf_s(logLine, sizeof(logLine),
					"  [REC] %s unit EXISTS - ID:0x%08X Type:%d %s\r\n",
					eventPrefix,
					whom.m_ID,
					static_cast<int>(pUnit->WhatAmI()),
					houseInfo);
				WriteToPrettyFile(logLine);

				TechnoClass* pTechno = abstract_cast<TechnoClass*>(pUnit);
				if (pTechno)
				{
					// Log validation state
					sprintf_s(logLine, sizeof(logLine),
						"  [REC] %s State: IsActive=%d Strength=%d IsInLimbo=%d\r\n",
						eventPrefix,
						pTechno->IsAlive ? 1 : 0,
						pTechno->Health,
						pTechno->InLimbo ? 1 : 0);
					WriteToPrettyFile(logLine);

					// Log IsTether state (RA1 line 716: if (!techno->IsTethered))
					sprintf_s(logLine, sizeof(logLine),
						"  [REC] %s IsTether=%d (if 0, will send RADIO_OVER_OUT)\r\n",
						eventPrefix,
						pTechno->IsTether ? 1 : 0);
					WriteToPrettyFile(logLine);

					// Log Team membership (RA1 lines 721-723)
					FootClass* pFoot = abstract_cast<FootClass*>(pTechno);
					if (pFoot)
					{
						sprintf_s(logLine, sizeof(logLine),
							"  [REC] %s Team=%p (will Remove if non-null)\r\n",
							eventPrefix,
							pFoot->Team);
						WriteToPrettyFile(logLine);

						// Log current MegaMission state
						sprintf_s(logLine, sizeof(logLine),
							"  [REC] %s Current: Mission=%d MegaDest=%p MegaTarget=%p\r\n",
							eventPrefix,
							static_cast<int>(pFoot->MegaMission),
							pFoot->MegaDestination,
							pFoot->MegaTarget);
						WriteToPrettyFile(logLine);

						// Special logging for MegaMissionF events
						if (pEvent->Type == EventType::MegaMissionF)
						{
							sprintf_s(logLine, sizeof(logLine),
								"  [REC] MegaMissionF FORMATION: Speed=%d MaxSpeed=%d (sets formation=true)\r\n",
								pEvent->MegaMissionF.Speed,
								pEvent->MegaMissionF.MaxSpeed);
							WriteToPrettyFile(logLine);
						}
					}

					// Log radio contact state - unsure about this, probs needs yeeting
					int radioLinkCount = pTechno->RadioLinks.Capacity;
					TechnoClass* firstRadioLink = (radioLinkCount > 0) ? pTechno->RadioLinks[0] : nullptr;
					sprintf_s(logLine, sizeof(logLine),
						"  [REC] %s RadioLinkCount=%d FirstLink=%p\r\n",
						eventPrefix,
						radioLinkCount,
						firstRadioLink);
					WriteToPrettyFile(logLine);

					// Log event details being recorded
					if (pEvent->Type == EventType::MegaMissionF)
					{
						sprintf_s(logLine, sizeof(logLine),
							"  [REC] MegaMissionF EVENT: Recording Mission=%d Target=0x%08X Dest=0x%08X\r\n",
							static_cast<int>(pEvent->MegaMissionF.Mission),
							pEvent->MegaMissionF.Target.m_ID,
							pEvent->MegaMissionF.Destination.m_ID);
					}
					else
					{
						sprintf_s(logLine, sizeof(logLine),
							"  [REC] MegaMission EVENT: Recording Mission=%d Target=0x%08X Dest=0x%08X\r\n",
							static_cast<int>(pEvent->MegaMission.Mission),
							pEvent->MegaMission.Target.m_ID,
							pEvent->MegaMission.Destination.m_ID);
					}
					WriteToPrettyFile(logLine);
				}
			}
			else
			{
				sprintf_s(logLine, sizeof(logLine),
					"  [REC] %s unit MISSING - ID:0x%08X - RECORDING INVALID EVENT!\r\n",
					eventPrefix,
					whom.m_ID);
				WriteToPrettyFile(logLine);
			}
		}
	}
}

void PlaybackFrameEvents()
{
	if (!RunPlaybackFunc || !ReplayPlayback)
		return;

	if (!ReplayFile || ReplayFile == INVALID_HANDLE_VALUE)
	{
		StartReplayPlayback();
		if (!ReplayFile || ReplayFile == INVALID_HANDLE_VALUE)
			return;
	}

	// Read events sequentially
	// The number of events to read was set by RestoreFrameState()
	char eventBuffer[sizeof(EventClass)];
	EventClass* event = reinterpret_cast<EventClass*>(eventBuffer);
	DWORD bytesRead;

	for (int i = 0; i < ExpectedEventsThisFrame; i++)
	{
		bool success = ReadFile(ReplayFile, eventBuffer, sizeof(EventClass), &bytesRead, NULL);

		if (bytesRead == 0 || !success || bytesRead != sizeof(EventClass))
		{
			if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
			{
				char logLine[256];
				sprintf_s(logLine, sizeof(logLine),
					"PLAYBACK ENDED/ERROR: Expected %d events, read %d. CurrentFrame=%u\r\n",
					ExpectedEventsThisFrame, i, Unsorted::CurrentFrame);
				WriteToPlaybackLog(logLine);
			}
			RunPlaybackFunc = false;
			StopReplaySystem();
			return;
		}

		ReadEvents++;

		// Log the event being played back
		if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
		{
			char timestamp[32];
			char details[512];
			char logLine[1024];

			GetTimestamp(timestamp, sizeof(timestamp));
			FormatEventDetails(event, details, sizeof(details));

			const char* eventName = EventTypeToString(event->Type);

			sprintf_s(logLine, sizeof(logLine),
				"[%s] Current:%6u Event:%6u H%2d %-15s%s (IsExecuted: %s)\r\n",
				timestamp,
				Unsorted::CurrentFrame,
				event->Frame,
				static_cast<int>(event->HouseIndex),
				eventName,
				details,
				event->IsExecuted ? "YES" : "NO");

			WriteToPlaybackLog(logLine);
		}

		// Reset the IsExecuted flag before re-injecting the event
		// Required for Execute_DoList to process the event (RA1 QUEUE.CPP line 3732)
		event->IsExecuted = false;

		// For MegaMission and MegaMissionF events, verify the unit exists and log info
		if ((event->Type == EventType::MegaMission || event->Type == EventType::MegaMissionF) &&
			PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
		{
			const char* eventPrefix = (event->Type == EventType::MegaMissionF) ? "MegaMissionF" : "MegaMission";

			// Get Whom from appropriate event type
			TargetClass whom = (event->Type == EventType::MegaMissionF) ?
				event->MegaMissionF.Whom : event->MegaMission.Whom;
			AbstractClass* pUnit = whom.As_Abstract();

			char debugLine[512];
			if (pUnit)
			{
				char houseInfo[64];
				GetHouseInfo(pUnit, houseInfo, sizeof(houseInfo));

				sprintf_s(debugLine, sizeof(debugLine),
					"  [PLAY] %s unit EXISTS - ID:0x%08X Type:%d %s\r\n",
					eventPrefix,
					whom.m_ID,
					static_cast<int>(pUnit->WhatAmI()),
					houseInfo);
				WriteToPlaybackLog(debugLine);

				// Log detailed info for MegaMissions
				// in RA1 EVENT.CPP lines 676-792
				TechnoClass* pTechno = abstract_cast<TechnoClass*>(pUnit);
				if (pTechno)
				{
					// Log validation state
					sprintf_s(debugLine, sizeof(debugLine),
						"  [PLAY] %s State: IsActive=%d Strength=%d IsInLimbo=%d\r\n",
						eventPrefix,
						pTechno->IsAlive ? 1 : 0,
						pTechno->Health,
						pTechno->InLimbo ? 1 : 0);
					WriteToPlaybackLog(debugLine);

					// Log IsTether state (RA1 line 716: if (!techno->IsTethered))
					sprintf_s(debugLine, sizeof(debugLine),
						"  [PLAY] %s IsTether=%d (if 0, will send RADIO_OVER_OUT)\r\n",
						eventPrefix,
						pTechno->IsTether ? 1 : 0);
					WriteToPlaybackLog(debugLine);

					// Log Team membership (RA1 lines 721-723)
					FootClass* pFoot = abstract_cast<FootClass*>(pTechno);
					if (pFoot)
					{
						sprintf_s(debugLine, sizeof(debugLine),
							"  [PLAY] %s Team=%p (will Remove if non-null)\r\n",
							eventPrefix,
							pFoot->Team);
						WriteToPlaybackLog(debugLine);

						// Log current MegaMission state
						sprintf_s(debugLine, sizeof(debugLine),
							"  [PLAY] %s Current: Mission=%d MegaDest=%p MegaTarget=%p\r\n",
							eventPrefix,
							static_cast<int>(pFoot->MegaMission),
							pFoot->MegaDestination,
							pFoot->MegaTarget);
						WriteToPlaybackLog(debugLine);

						// Special logging for MegaMissionF events
						if (event->Type == EventType::MegaMissionF)
						{
							sprintf_s(debugLine, sizeof(debugLine),
								"  [PLAY] MegaMissionF FORMATION: Speed=%d MaxSpeed=%d (sets formation=true)\r\n",
								event->MegaMissionF.Speed,
								event->MegaMissionF.MaxSpeed);
							WriteToPlaybackLog(debugLine);
						}
					}

					// Log radio contact state (unsure aboutt his)
					int radioLinkCount = pTechno->RadioLinks.Capacity;
					TechnoClass* firstRadioLink = (radioLinkCount > 0) ? pTechno->RadioLinks[0] : nullptr;
					sprintf_s(debugLine, sizeof(debugLine),
						"  [PLAY] %s RadioLinkCount=%d FirstLink=%p\r\n",
						eventPrefix,
						radioLinkCount,
						firstRadioLink);
					WriteToPlaybackLog(debugLine);

					// Log event details being executed
					if (event->Type == EventType::MegaMissionF)
					{
						sprintf_s(debugLine, sizeof(debugLine),
							"  [PLAY] MegaMissionF EVENT: Executing Mission=%d Target=0x%08X Dest=0x%08X\r\n",
							static_cast<int>(event->MegaMissionF.Mission),
							event->MegaMissionF.Target.m_ID,
							event->MegaMissionF.Destination.m_ID);
					}
					else
					{
						sprintf_s(debugLine, sizeof(debugLine),
							"  [PLAY] MegaMission EVENT: Executing Mission=%d Target=0x%08X Dest=0x%08X\r\n",
							static_cast<int>(event->MegaMission.Mission),
							event->MegaMission.Target.m_ID,
							event->MegaMission.Destination.m_ID);
					}
					WriteToPlaybackLog(debugLine);
				}
			}
			else
			{
				sprintf_s(debugLine, sizeof(debugLine),
					"  [PLAY] %s unit MISSING - ID:0x%08X - CANNOT EXECUTE EVENT!\r\n",
					eventPrefix,
					whom.m_ID);
				WriteToPlaybackLog(debugLine);
			}
		}

		// Add event to DoList for execution
		// Note we recorded from OutList, but this is what RA1 does.
		if (!EventClass::DoList.Add(*event))
		{
			if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE)
			{
				WriteToPlaybackLog("ERROR: DoList FULL during playback\r\n");
			}
			RunPlaybackFunc = false;
			StopReplaySystem();
			return;
		}
	}
}

bool EventExt::AddEvent()
{
	return EventClass::OutList.Add(*reinterpret_cast<EventClass*>(this));
}

void EventExt::RespondEvent()
{
	switch (this->Type)
	{
	case EventTypeExt::Sample:
		// Place the handler here
		break;
	}
}

size_t EventExt::GetDataSize(EventTypeExt type)
{
	switch (type)
	{
	case EventTypeExt::Sample:
		return sizeof(EventExt::Sample);
	}

	return 0;
}

bool EventExt::IsValidType(EventTypeExt type)
{
	return (type >= EventTypeExt::FIRST && type <= EventTypeExt::LAST);
}

// hooks

DEFINE_HOOK(0x4C6CC8, Networking_RespondToEvent, 0x5)
{
	GET(EventClass*, pEvent, ESI);

	// Handle Phobos extended events
	if (EventExt::IsValidType(static_cast<EventTypeExt>(pEvent->Type)))
	{
		reinterpret_cast<EventExt*>(pEvent)->RespondEvent();
	}

	return 0;
}

DEFINE_HOOK(0x64B6FE, sub_64B660_GetEventSize, 0x6)
{
	const auto eventType = static_cast<EventTypeExt>(R->EDI() & 0xFF);

	if (EventExt::IsValidType(eventType))
	{
		const size_t eventSize = EventExt::GetDataSize(eventType);

		R->EDX(eventSize);
		R->EBP(eventSize);
		return 0x64B71D;
	}

	return 0;
}

DEFINE_HOOK(0x64BE7D, sub_64BDD0_GetEventSize1, 0x6)
{
	const auto eventType = static_cast<EventTypeExt>(R->EDI() & 0xFF);

	if (EventExt::IsValidType(eventType))
	{
		const size_t eventSize = EventExt::GetDataSize(eventType);

		REF_STACK(size_t, eventSizeInStack, STACK_OFFSET(0xAC, -0x8C));
		eventSizeInStack = eventSize;
		R->ECX(eventSize);
		R->EBP(eventSize);
		return 0x64BE97;
	}

	return 0;
}

DEFINE_HOOK(0x64C30E, sub_64BDD0_GetEventSize2, 0x6)
{
	const auto eventType = static_cast<EventTypeExt>(R->ESI() & 0xFF);

	if (EventExt::IsValidType(eventType))
	{
		const size_t eventSize = EventExt::GetDataSize(eventType);

		R->ECX(eventSize);
		R->EBP(eventSize);
		return 0x64C321;
	}

	return 0;
}

//DEFINE_HOOK(0x55B4E1, LogicClass_Update_BeforeAll, 0x5) //no
//DEFINE_HOOK(0x55DBCD, LogicClass_Update_ReplayInjectEvents, 0x6) //no
DEFINE_HOOK(0x55AFB3, LogicClass_Update_ReplayInjectEvents, 0x6) //yay
{
	//------------------------------------------------------------------------
	//	Move events from the OutList (events generated by this player) into the
	//	DoList (the list of events to execute).
	//------------------------------------------------------------------------
	// see: Queue_AI_Normal
	// Note:
	// The game likely does this itself already. It hasn't been done here because DoList is empty whereas OutList isn't.
	// so we are doing it for the game, a bit early but hopefully that doesn't break anything. We should at least stop the game doing it
	while (EventClass::OutList.Count > 0)
	{
		EventClass& event = EventClass::OutList.First();

		// Matches RA1 QUEUE.CPP lines 413, 2532, 2766, 3732
		event.IsExecuted = false;

		// Add to DoList
		if (!EventClass::DoList.Add(event))
		{
			// DoList is full
			if (ReplayRecording && PrettyReplayFile)
			{
				char errorMsg[256];
				sprintf_s(errorMsg, sizeof(errorMsg),
					"ERROR Frame %d: DoList FULL, could not transfer event from OutList\r\n",
					Unsorted::CurrentFrame);
				WriteToPrettyFile(errorMsg);
			}
			break;
		}

		// Yeet
		EventClass::OutList.Next();
	}

	int currentUniqueID = ScenarioClass::Instance ? ScenarioClass::Instance->UniqueID : -1;

	if (ReplayRecording && PrettyReplayFile)
	{
		char uniqueIdLog[128];
		sprintf_s(uniqueIdLog, sizeof(uniqueIdLog),
			"=== Frame %d START - UniqueID: %d ===\n",
			Unsorted::CurrentFrame, currentUniqueID);
		WriteToPrettyFile(uniqueIdLog);
	}

	if (ReplayPlayback && PlaybackLogFile)
	{
		char uniqueIdLog[128];
		sprintf_s(uniqueIdLog, sizeof(uniqueIdLog),
			"=== Frame %d START - UniqueID: %d ===\n",
			Unsorted::CurrentFrame, currentUniqueID);
		WriteToPlaybackLog(uniqueIdLog);
	}
	if (ReplayRecording)
	{

		//// Update the replay header with current UniqueID
		//if (ReplayFile && ReplayFile != INVALID_HANDLE_VALUE && ScenarioClass::Instance)
		//{
		//	DWORD offset = offsetof(ReplayHeader, UniqueIDCounter);
		//	SetFilePointer(ReplayFile, offset, NULL, FILE_BEGIN);

		//	currentUniqueID = ScenarioClass::Instance->UniqueID;
		//	DWORD bytesWritten;
		//	WriteFile(ReplayFile, &currentUniqueID, sizeof(currentUniqueID), &bytesWritten, NULL);
		//	FlushFileBuffers(ReplayFile);

		//	// Seek back to end for event writing
		//	SetFilePointer(ReplayFile, 0, NULL, FILE_END);
		//}

		// Log to pretty file
		if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE && !PrintedRecordingStartOnce)
		{
			char logMsg[256];
			sprintf_s(logMsg, sizeof(logMsg),
				"\r\n*** Recording started at Frame %d, UniqueID %d ***\r\n\r\n",
				Unsorted::CurrentFrame,
				 ScenarioClass::Instance ? ScenarioClass::Instance->UniqueID : 0);
			WriteToPrettyFile(logMsg);
			PrintedRecordingStartOnce = true;
		}
	}

	// Record events from DoList (all events scheduled for execution)
	// See (QUEUE.CPP lines 3602-3627)
	if (ReplayRecording)
	{
		// Count events for this frame first (needed for FrameStateRecord)
		int eventsThisFrame = 0;
		int doListCount = EventClass::DoList.Count;
		for (int i = 0; i < doListCount; i++)
		{
			EventClass& event = EventClass::DoList[i];
			if (event.Frame == static_cast<unsigned int>(Unsorted::CurrentFrame) && !event.IsExecuted)
			{
				eventsThisFrame++;
			}
		}
		
		// Record per-frame state first (like RA1's Do_Record_Playback)
		RecordFrameState(eventsThisFrame);
		
		// Then record all events for this frame
		for (int i = 0; i < doListCount; i++)
		{
			EventClass& event = EventClass::DoList[i];
			if (event.Frame == static_cast<unsigned int>(Unsorted::CurrentFrame) && !event.IsExecuted)
			{
				RecordEvent(&event);
			}
		}

		// Logging every 100 frames during recording
		if (PrettyReplayFile && PrettyReplayFile != INVALID_HANDLE_VALUE &&
			Unsorted::CurrentFrame % 100 == 0)
		{
			LogGameStateStatistics(WriteToPrettyFile);
		}
	}

	// Call replay function during playback
	if (ReplayPlayback && RunPlaybackFunc)
	{
		// Read per-frame state first (like RA1's Do_Record_Playback)
		RestoreFrameState();
		
		// Then read and inject events for this frame
		PlaybackFrameEvents();
	}
		
	// Logging every 100 frames during playback
	if (PlaybackLogFile && PlaybackLogFile != INVALID_HANDLE_VALUE &&
		Unsorted::CurrentFrame % 100 == 0)
	{
		LogGameStateStatistics(WriteToPlaybackLog);
	}

	return 0;
}

// Scenario start
DEFINE_HOOK(0x685659, ScenarioClass_Start_ReplayInit, 0x5)
{
	// Check spawn.ini for replay mode (set by Client)
	// If [Settings] ReplayFile is set, we're in playback mode
	char replayFilePath[MAX_PATH] = { 0 };
	if (IsReplayPlaybackMode(replayFilePath, sizeof(replayFilePath)))
	{
		EnableReplayPlayback(replayFilePath);
	}

	// If not in playback mode, always record
	if (!ReplayPlayback)
	{
		ReplayRecording = true;
	}

	if (ReplayRecording)
	{
		StartReplayRecording();
	}

	if (ReplayPlayback)
	{
		StartReplayPlayback();

		// Remove shroud
		if (!g_PlaybackSettings.shroudEnabled)
		{
			// TODO: This just seems to deploy the MCV automatically.
			for (int i = 0; i < HouseClass::Array.Count; i++)
			{
				HouseClass* pHouse = HouseClass::Array.GetItemOrDefault(i);
				if (pHouse)
				{
					MapClass::Instance.Reveal(pHouse);
				}
			}
		}
	}

	return 0;
}

// Scenario end
// TODO: Something fucky when leaving half-way through a playback?
DEFINE_HOOK(0x685800, ScenarioClass_End_ReplayShutdown, 0x5)
{
	StopReplaySystem();
	return 0;
}
