#pragma once

#include <CRC.h>
#include <Unsorted.h>
#include <EventClass.h>

// Maximum number of mid-frame CRC checkpoints per frame.
// Each checkpoint stores a CRC of the full game state at a specific point in the frame's logic.
// When analyzing desync logs, comparing checkpoints narrows down which part of the frame diverged.
static constexpr int MAX_CHECKPOINTS = 16;

// Number of deterministic-random checkpoint slots used for statistical narrowing.
// Each frame, a subset of possible checkpoint locations are randomly (but deterministically) activated.
static constexpr int RANDOM_CHECKPOINT_SLOTS = 10;

struct FrameCheckpointData
{
	DWORD CRCs[MAX_CHECKPOINTS];
	int ActiveCount;      // How many checkpoints were taken this frame
	int Frame;

	// Labels for each checkpoint for log output
	const char* Labels[MAX_CHECKPOINTS];

	FrameCheckpointData() : ActiveCount(0), Frame(0)
	{
		memset(CRCs, 0, sizeof(CRCs));
		memset(Labels, 0, sizeof(Labels));
	}

	void Reset(int frame)
	{
		ActiveCount = 0;
		Frame = frame;
		memset(CRCs, 0, sizeof(CRCs));
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

	// CRC Techno extensions
	static void CRCTechnoExtensions(CRCEngine& crc);

	// CRC House extensions
	static void CRCHouseExtensions(CRCEngine& crc);

	// CRC Building extensions
	static void CRCBuildingExtensions(CRCEngine& crc);

	// CRC Scenario extensions (global Phobos state)
	static void CRCScenarioExtension(CRCEngine& crc);

	// ---- Mid-frame Checkpoints ----
	// Take a CRC snapshot at the current point in frame logic.
	// label: short identifier for this checkpoint location (e.g. "PostLogicAI", "PostTechnoAI")
	static void TakeCheckpoint(const char* label);

	// Reset checkpoint state for a new frame.
	static void BeginFrame();

	// ---- Deterministic Random Checkpoints ----
	// Returns true if a checkpoint at the given slot index should be active this frame.
	// Uses deterministic hash of frame number + slot to ensure all players agree.
	static bool IsSlotActiveThisFrame(int slotIndex);

	// ---- Data Access ----
	static const FrameCheckpointData& GetCurrentFrameData();

	// History buffer for last N frames of checkpoint data
	static constexpr int HISTORY_SIZE = 256;
	static FrameCheckpointData FrameHistory[HISTORY_SIZE];
	static int CurrentHistoryIndex;
	static FrameCheckpointData CurrentFrame;

	// ---- Logging ----
	static void WriteCheckpointLog(FILE* pLogFile, int frameDigits);
};
