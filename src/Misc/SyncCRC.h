#pragma once

#include <CRC.h>
#include <Unsorted.h>
#include <EventClass.h>

// Maximum number of mid-frame CRC checkpoints per frame.
// Each checkpoint stores a CRC of the full game state at a specific point in the frame's logic.
// When analyzing desync logs, comparing checkpoints narrows down which part of the frame diverged.
static constexpr int MAX_CHECKPOINTS = 20;

// Per-category CRC indices for breakdown at each checkpoint.
enum CRCCategory : int
{
	CAT_VANILLA = 0,  // EventClass::CurrentFrameCRC
	CAT_RNG,          // Random2Class state
	CAT_TECHNO,
	CAT_HOUSE,
	CAT_BUILDING,
	CAT_SCENARIO,
	CAT_TEAM,
	CAT_BULLET,
	CAT_RADSITE,
	CAT_AE,           // AttachEffects
	CAT_CELL,         // CellRadiation
	CAT_ANIM,         // AnimExt
	CAT_FACTORY,      // FactoryClass
	CAT_SUPER,        // SuperClass
	CAT_TRIGGER,      // TriggerClass + TagClass
	CAT_TEMPORAL,     // TemporalClass
	CAT_MINDCTRL,     // CaptureManagerClass
	CAT_SPAWN,        // SpawnManagerClass + SlaveManagerClass
	CAT_PARASITE,     // ParasiteClass + AirstrikeClass + EMPulse
	NUM_CRC_CATEGORIES
};

const char* CRCCategoryName(int cat);

// Object count categories for per-checkpoint snapshots.
enum ObjectCountCategory : int
{
	CNT_TECHNO = 0,
	CNT_INFANTRY,
	CNT_UNIT,
	CNT_AIRCRAFT,
	CNT_BUILDING,
	CNT_BULLET,
	CNT_ANIM,
	CNT_TEAM,
	CNT_HOUSE,
	CNT_RADSITE,
	CNT_FACTORY,
	CNT_SUPER,
	CNT_TRIGGER,
	CNT_TEMPORAL,
	CNT_CAPTURE,
	CNT_SPAWN,
	CNT_SLAVE,
	CNT_PARASITE,
	NUM_COUNT_CATEGORIES
};

const char* ObjectCountCategoryName(int cat);

struct FrameCheckpointData
{
	DWORD CRCs[MAX_CHECKPOINTS];
	DWORD CategoryCRCs[MAX_CHECKPOINTS][NUM_CRC_CATEGORIES];
	int ObjectCounts[MAX_CHECKPOINTS][NUM_COUNT_CATEGORIES];
	int RNGCallCount[MAX_CHECKPOINTS]; // Cumulative RNG calls at each checkpoint
	int ActiveCount;      // How many checkpoints were taken this frame
	int Frame;

	// Labels for each checkpoint for log output
	const char* Labels[MAX_CHECKPOINTS];

	FrameCheckpointData() : ActiveCount(0), Frame(0)
	{
		memset(CRCs, 0, sizeof(CRCs));
		memset(CategoryCRCs, 0, sizeof(CategoryCRCs));
		memset(ObjectCounts, 0, sizeof(ObjectCounts));
		memset(RNGCallCount, 0, sizeof(RNGCallCount));
		memset(Labels, 0, sizeof(Labels));
	}

	void Reset(int frame)
	{
		ActiveCount = 0;
		Frame = frame;
		memset(CRCs, 0, sizeof(CRCs));
		memset(CategoryCRCs, 0, sizeof(CategoryCRCs));
		memset(ObjectCounts, 0, sizeof(ObjectCounts));
		memset(RNGCallCount, 0, sizeof(RNGCallCount));
		memset(Labels, 0, sizeof(Labels));
	}
};

class SyncCRC
{
public:
	// ---- Comprehensive CRC ----
	// Computes CRC over all critical Phobos extension data for all game objects.
	// Called after the vanilla ComputeFrameCRC to fold in Phobos state.
	static void ComputePhobosExtensionCRC(CRCEngine& crc);

	// Augment the current frame CRC (EventClass::CurrentFrameCRC) with Phobos extension data.
	// Call this after vanilla CRC is computed but before it's compared for desync detection.
	static void AugmentCurrentFrameCRC();

	static void CRCTechnoExtensions(CRCEngine& crc);
	static void CRCHouseExtensions(CRCEngine& crc);
	static void CRCBuildingExtensions(CRCEngine& crc);
	static void CRCScenarioExtension(CRCEngine& crc);
	static void CRCTeamExtensions(CRCEngine& crc);
	static void CRCBulletExtensions(CRCEngine& crc);
	static void CRCRadSiteExtensions(CRCEngine& crc);
	static void CRCAttachEffects(CRCEngine& crc);
	static void CRCCellRadiation(CRCEngine& crc);
	static void CRCAnimExtensions(CRCEngine& crc);

	// Vanilla game arrays not covered by Phobos extensions
	static void CRCFactories(CRCEngine& crc);
	static void CRCSuperWeapons(CRCEngine& crc);
	static void CRCTriggersAndTags(CRCEngine& crc);
	static void CRCTemporals(CRCEngine& crc);
	static void CRCMindControl(CRCEngine& crc);
	static void CRCSpawnAndSlave(CRCEngine& crc);
	static void CRCParasiteAirstrikeEMP(CRCEngine& crc);

	// ---- Mid-frame Checkpoints ----
	// Take a CRC snapshot at the current point in frame logic.
	// label: short identifier for this checkpoint location (e.g. "PostLogicAI", "PostTechnoAI")
	static void TakeCheckpoint(const char* label);

	// Reset checkpoint state for a new frame.
	static void BeginFrame();

	// ---- RNG Call Counter ----
	// Incremented by RNG hooks each time Random2Class is called.
	static int FrameRNGCallCount;

	// ---- Per-Object CRC Dump ----
	// Writes individual per-object CRCs (vanilla + Phobos ext) to a file for desync comparison.
	static void WritePerObjectCRCDump(FILE* pLogFile);

	// ---- Data Access ----
	static const FrameCheckpointData& GetCurrentFrameData();

	// History buffer for last N frames of checkpoint data
	static constexpr int HISTORY_SIZE = 256;
	static FrameCheckpointData FrameHistory[HISTORY_SIZE];
	static int CurrentHistoryIndex;
	static FrameCheckpointData CurrentFrame;

	// ---- Logging ----
	static void WriteCheckpointLog(FILE* pLogFile, int frameDigits);

private:
	// Snapshot object counts from game arrays into the given arrays.
	static void SnapshotObjectCounts(int counts[NUM_COUNT_CATEGORIES]);
};
