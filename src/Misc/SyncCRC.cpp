#include "SyncCRC.h"
#include "SyncLogging.h"

#include <TechnoClass.h>
#include <HouseClass.h>
#include <BuildingClass.h>
#include <ScenarioClass.h>
#include <Unsorted.h>

#include <Ext/Techno/Body.h>
#include <Ext/House/Body.h>
#include <Ext/Building/Body.h>
#include <Ext/Scenario/Body.h>

#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

// Static members
FrameCheckpointData SyncCRC::FrameHistory[SyncCRC::HISTORY_SIZE];
int SyncCRC::CurrentHistoryIndex = 0;
FrameCheckpointData SyncCRC::CurrentFrame;

// ============================================================================
// Comprehensive CRC - fold Phobos extension data into the frame CRC
// ============================================================================

void SyncCRC::ComputePhobosExtensionCRC(CRCEngine& crc)
{
	CRCTechnoExtensions(crc);
	CRCHouseExtensions(crc);
	CRCBuildingExtensions(crc);
	CRCScenarioExtension(crc);
}

void SyncCRC::CRCTechnoExtensions(CRCEngine& crc)
{
	for (auto const pTechno : TechnoClass::Array)
	{
		auto const pTechnoExt = TechnoExt::ExtMap.Find(pTechno);
		if (!pTechnoExt)
			continue;

		// Shield state
		if (pTechnoExt->Shield)
		{
			int shieldHP = pTechnoExt->Shield->GetHP();
			crc(shieldHP);
			crc(pTechnoExt->Shield->IsActive());
			crc(pTechnoExt->Shield->IsAvailable());
		}

		// AttachedEffects count (state that affects gameplay decisions)
		int aeCount = static_cast<int>(pTechnoExt->AttachedEffects.size());
		crc(aeCount);
		crc(pTechnoExt->AttachedEffectInvokerCount);

		// Critical gameplay state fields
		crc(pTechnoExt->IsInTunnel);
		crc(pTechnoExt->IsBurrowed);
		crc(pTechnoExt->HasBeenPlacedOnMap);
		crc(pTechnoExt->IsBeingChronoSphered);
		crc(pTechnoExt->HasRemainingWarpInDelay);
		crc(pTechnoExt->LastWarpInDelay);
		crc(pTechnoExt->LastWarpDistance);
		crc(pTechnoExt->CurrentAircraftWeaponIndex);
		crc(pTechnoExt->Strafe_BombsDroppedThisRound);
		crc(pTechnoExt->ForceFullRearmDelay);
		crc(pTechnoExt->LastRearmWasFullDelay);
		crc(pTechnoExt->CanCloakDuringRearm);
		crc(pTechnoExt->ReceiveDamage);
		crc(pTechnoExt->LastKillWasTeamTarget);
		crc(pTechnoExt->SkipTargetChangeResetSequence);
		crc(pTechnoExt->JumpjetStraightAscend);

		// Timer states (CDTimerClass stores frame-synced data)
		crc(pTechnoExt->PassengerDeletionTimer.GetTimeLeft());
		crc(pTechnoExt->ChargeTurretTimer.GetTimeLeft());
		crc(pTechnoExt->AutoDeathTimer.GetTimeLeft());
		crc(pTechnoExt->DeployFireTimer.GetTimeLeft());

		// Sensor position (affects fog of war)
		crc(pTechnoExt->LastSensorsMapCoords.X);
		crc(pTechnoExt->LastSensorsMapCoords.Y);
	}
}

void SyncCRC::CRCHouseExtensions(CRCEngine& crc)
{
	for (auto const pHouse : HouseClass::Array)
	{
		auto const pHouseExt = HouseExt::ExtMap.Find(pHouse);
		if (!pHouseExt)
			continue;

		crc(static_cast<int>(pHouseExt->OwnedLimboDeliveredBuildings.size()));
		crc(static_cast<int>(pHouseExt->OwnedCountedHarvesters.size()));

		// Production state
		crc(pHouseExt->LastBuiltNavalVehicleType);
		crc(pHouseExt->ProducingNavalUnitTypeIndex);

		// Factory counts (affects MFB logic)
		crc(pHouseExt->NumAirpads_NonMFB);
		crc(pHouseExt->NumBarracks_NonMFB);
		crc(pHouseExt->NumWarFactories_NonMFB);
		crc(pHouseExt->NumConYards_NonMFB);
		crc(pHouseExt->NumShipyards_NonMFB);

		// Timer states
		crc(pHouseExt->AISuperWeaponDelayTimer.GetTimeLeft());
		crc(pHouseExt->AIFireSaleDelayTimer.GetTimeLeft());

		// SuperWeapon extension shot counts
		for (size_t i = 0; i < pHouseExt->SuperExts.size(); i++)
			crc(pHouseExt->SuperExts[i].ShotCount);
	}
}

void SyncCRC::CRCBuildingExtensions(CRCEngine& crc)
{
	for (auto const pBuilding : BuildingClass::Array)
	{
		auto const pBuildingExt = BuildingExt::ExtMap.Find(pBuilding);
		if (!pBuildingExt)
			continue;

		crc(pBuildingExt->DeployedTechno);
		crc(pBuildingExt->LimboID);
		crc(pBuildingExt->GrindingWeapon_LastFiredFrame);
		crc(pBuildingExt->GrindingWeapon_AccumulatedCredits);
		crc(pBuildingExt->PoweredUpToLevel);
	}
}

void SyncCRC::CRCScenarioExtension(CRCEngine& crc)
{
	auto const pScenarioExt = ScenarioExt::Global();
	if (!pScenarioExt)
		return;

	// Variable state (global and local) - these drive triggers which affect game logic
	for (int scope = 0; scope < 2; scope++)
	{
		int varCount = static_cast<int>(pScenarioExt->Variables[scope].size());
		crc(varCount);
		for (auto const& [idx, var] : pScenarioExt->Variables[scope])
		{
			crc(idx);
			crc(var.Value);
		}
	}

	// Auto-death and transport reloader tracking
	crc(static_cast<int>(pScenarioExt->AutoDeathObjects.size()));
	crc(static_cast<int>(pScenarioExt->TransportReloaders.size()));
}

// ============================================================================
// Mid-frame Checkpoint System
// ============================================================================

void SyncCRC::BeginFrame()
{
	// Archive previous frame's data
	if (CurrentFrame.ActiveCount > 0)
	{
		FrameHistory[CurrentHistoryIndex] = CurrentFrame;
		CurrentHistoryIndex = (CurrentHistoryIndex + 1) % HISTORY_SIZE;
	}

	CurrentFrame.Reset(Unsorted::CurrentFrame);
}

void SyncCRC::TakeCheckpoint(const char* label)
{
	if (CurrentFrame.ActiveCount >= MAX_CHECKPOINTS)
		return;

	// Compute a full CRC of the game state at this point
	CRCEngine crc;

	// Include the vanilla frame CRC accumulated so far
	crc(EventClass::CurrentFrameCRC);

	// Include current RNG state (critical for desync detection)
	crc(ScenarioClass::Instance->Random.Next1);
	crc(ScenarioClass::Instance->Random.Next2);

	// Include Phobos extension state
	ComputePhobosExtensionCRC(crc);

	int idx = CurrentFrame.ActiveCount;
	CurrentFrame.CRCs[idx] = static_cast<DWORD>(static_cast<int>(crc));
	CurrentFrame.Labels[idx] = label;
	CurrentFrame.ActiveCount++;
}

bool SyncCRC::IsSlotActiveThisFrame(int slotIndex)
{
	// Deterministic "random" selection using frame number and slot index.
	// All players compute the same result because they share the same frame number.
	// We do NOT use the game RNG to avoid perturbing it.
	// Instead we use a simple hash of frame + slot.
	unsigned int seed = Unsorted::CurrentFrame * 2654435761u + static_cast<unsigned int>(slotIndex) * 40503u;
	seed ^= (seed >> 16);
	seed *= 0x45d9f3b;
	seed ^= (seed >> 16);

	// Activate ~RANDOM_CHECKPOINT_SLOTS out of MAX_CHECKPOINTS slots per frame
	return (seed % MAX_CHECKPOINTS) < static_cast<unsigned int>(RANDOM_CHECKPOINT_SLOTS);
}

const FrameCheckpointData& SyncCRC::GetCurrentFrameData()
{
	return CurrentFrame;
}

// ============================================================================
// Augment CurrentFrameCRC with Phobos extension data.
// Called from the Queue_AI desync log hook (SyncLogging.cpp) right before
// the CRC is used, so the augmented CRC is what gets compared and logged.
// ============================================================================

void SyncCRC::AugmentCurrentFrameCRC()
{
	CRCEngine crc;
	crc(EventClass::CurrentFrameCRC);
	ComputePhobosExtensionCRC(crc);
	EventClass::CurrentFrameCRC = static_cast<DWORD>(static_cast<int>(crc));
}

// ============================================================================
// Logging
// ============================================================================

void SyncCRC::WriteCheckpointLog(FILE* pLogFile, int frameDigits)
{
	fprintf(pLogFile, "CRC Checkpoints (current frame %*d):\n", frameDigits, Unsorted::CurrentFrame);

	// Write current frame's checkpoints
	if (CurrentFrame.ActiveCount > 0)
	{
		fprintf(pLogFile, "  Current frame (%*d): %d checkpoints\n",
			frameDigits, CurrentFrame.Frame, CurrentFrame.ActiveCount);

		for (int i = 0; i < CurrentFrame.ActiveCount; i++)
		{
			fprintf(pLogFile, "    [%2d] %08X  %s\n",
				i, CurrentFrame.CRCs[i],
				CurrentFrame.Labels[i] ? CurrentFrame.Labels[i] : "unnamed");
		}
	}

	// Write recent history (last 8 frames with checkpoints)
	int written = 0;
	for (int offset = 1; offset < HISTORY_SIZE && written < 8; offset++)
	{
		int idx = (CurrentHistoryIndex - offset + HISTORY_SIZE) % HISTORY_SIZE;
		auto const& data = FrameHistory[idx];

		if (data.ActiveCount <= 0)
			continue;

		fprintf(pLogFile, "  Frame %*d: %d checkpoints\n",
			frameDigits, data.Frame, data.ActiveCount);

		for (int i = 0; i < data.ActiveCount; i++)
		{
			fprintf(pLogFile, "    [%2d] %08X  %s\n",
				i, data.CRCs[i],
				data.Labels[i] ? data.Labels[i] : "unnamed");
		}

		written++;
	}

	fprintf(pLogFile, "\n");
}
