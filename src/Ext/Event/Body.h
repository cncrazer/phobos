#pragma once

#include <cstddef>
#include <stdint.h>
#include <map>
#include <vector>
#include <GeneralStructures.h>
#include <TargetClass.h>

// Replay file header structure
#pragma pack(push, 1)
struct ReplayHeader
{
	char Magic[4];           // YREJ
	uint32_t Version;        // Replay format version
	char MapName[260];       // Map filename
	uint32_t StartFrame;     // Frame when recording started
	uint8_t PhobosVersionMajor;      // Phobos version
	uint8_t PhobosVersionMinor;
	uint8_t PhobosVersionRevision;
	uint8_t PhobosVersionPatch;
	char GameVersionString[32];      // Game version ("1.006")
	uint32_t GameMode;               // GameMode enum value (Skirmish=5, LAN=3, Internet=4, Campaign=0)

	int UniqueIDCounter;     // UniqueID counter at recording start
	int Seed;                // Random seed used for this game
	int RandomNext1;         // Randomizer::Next1 index
	int RandomNext2;         // Randomizer::Next2 index
	uint32_t RandomizerTable[250];  // Complete RNG state

	// Spawn file sizes (files stored after header, before events)
	uint32_t SpawnIniSize;      // Size of spawn.ini content
	uint32_t SpawnMapSize;      // Size of spawnmap.ini content
};
#pragma pack(pop)

// After ReplayHeader, contains:
// 1. spawn.ini content (SpawnIniSize bytes)
// 2. spawnmap.ini content (SpawnMapSize bytes)
// 3. Frame records:
//    For each frame:
//      a. FrameStateRecord (per-frame game state)
//      b. N x EventClass records (events for that frame)

constexpr char REPLAY_MAGIC[4] = { 'Y', 'R', 'E', 'J' };
constexpr uint32_t REPLAY_VERSION = 4;

// Per-frame game state record (similar to RA1's Do_Record_Playback)
// This is written once per frame before any events for that frame
#pragma pack(push, 1)
struct FrameStateRecord
{
	uint32_t FrameNumber;           // Current frame number
	Point2D TacticalPos;            // Map.TacticalPos (equivalent to RA1's DesiredTacticalCoord)
	int32_t SelectedObjectCount;    // Number of selected objects (ObjectClass::CurrentObjects.Count)
	uint32_t SelectedObjectCRC;     // CRC/sum of selected object IDs
	int32_t EventCountThisFrame;    // Number of events that follow for this frame
	int32_t UniqueIDCounter;        // ScenarioClass::UniqueID at this frame (to keep counters synchronized)
};
#pragma pack(pop)

enum class EventTypeExt : uint8_t
{
	// Vanilla game used Events from 0x00 to 0x2F
	// CnCNet reserved Events from 0x30 to 0x3F
	// Ares used Events 0x60 and 0x61

	Sample = 0x40, // Sample event, remove it when Phobos needs its own events
	ApproachObject = 0x41, // Phobos: issued when player clicks Approach Object

	FIRST = Sample,
	LAST = ApproachObject
};

#pragma pack(push, 1)
class EventExt
{
public:
	EventTypeExt Type;
	bool IsExecuted;
	char HouseIndex;
	uint32_t Frame;
	union
	{
		char DataBuffer[104];

		struct Sample
		{
			char DataBuffer[104];
		} Sample;

		struct
		{
			TargetClass Whom;
			TargetClass Target;
		} ApproachObject;
	};

	bool AddEvent();
	void RespondEvent();

	void RespondApproachObject();

	static size_t GetDataSize(EventTypeExt type);
	static bool IsValidType(EventTypeExt type);
};

static_assert(sizeof(EventExt) == 111);
static_assert(offsetof(EventExt, DataBuffer) == 7);
#pragma pack(pop)

bool IsReplayPlaybackMode(char* outReplayPath = nullptr, size_t outPathSize = 0);
bool ReadReplayHeader(const char* replayFilePath, ReplayHeader& outHeader, bool validateVersion = true);
