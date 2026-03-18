#include "SyncCRC.h"
#include "SyncLogging.h"

#include <TechnoClass.h>
#include <HouseClass.h>
#include <BuildingClass.h>
#include <ScenarioClass.h>
#include <BulletClass.h>
#include <RadSiteClass.h>
#include <CellClass.h>
#include <InfantryClass.h>
#include <UnitClass.h>
#include <AircraftClass.h>
#include <AnimClass.h>
#include <FactoryClass.h>
#include <SuperClass.h>
#include <TriggerClass.h>
#include <TagClass.h>
#include <TemporalClass.h>
#include <CaptureManagerClass.h>
#include <SpawnManagerClass.h>
#include <SlaveManagerClass.h>
#include <ParasiteClass.h>
#include <AirstrikeClass.h>
#include <EMPulseClass.h>
#include <Unsorted.h>

#include <Ext/Techno/Body.h>
#include <Ext/House/Body.h>
#include <Ext/Building/Body.h>
#include <Ext/Scenario/Body.h>
#include <Ext/Team/Body.h>
#include <Ext/Bullet/Body.h>
#include <Ext/RadSite/Body.h>
#include <Ext/Anim/Body.h>
#include <Ext/Cell/Body.h>

#include <New/Entity/ShieldClass.h>
#include <New/Entity/AttachEffectClass.h>

// Static members
FrameCheckpointData SyncCRC::FrameHistory[SyncCRC::HISTORY_SIZE];
int SyncCRC::CurrentHistoryIndex = 0;
FrameCheckpointData SyncCRC::CurrentFrame;
int SyncCRC::FrameRNGCallCount = 0;

const char* CRCCategoryName(int cat)
{
	switch (cat)
	{
	case CAT_VANILLA:  return "Vanilla";
	case CAT_RNG:      return "RNG";
	case CAT_TECHNO:   return "Techno";
	case CAT_HOUSE:    return "House";
	case CAT_BUILDING: return "Building";
	case CAT_SCENARIO: return "Scenario";
	case CAT_TEAM:     return "Team";
	case CAT_BULLET:   return "Bullet";
	case CAT_RADSITE:  return "RadSite";
	case CAT_AE:       return "AE";
	case CAT_CELL:     return "Cell";
	case CAT_ANIM:     return "Anim";
	case CAT_FACTORY:  return "Factory";
	case CAT_SUPER:    return "Super";
	case CAT_TRIGGER:  return "Trigger";
	case CAT_TEMPORAL: return "Temporal";
	case CAT_MINDCTRL: return "MindCtrl";
	case CAT_SPAWN:    return "Spawn";
	case CAT_PARASITE: return "Parasite";
	default:           return "???";
	}
}

const char* ObjectCountCategoryName(int cat)
{
	switch (cat)
	{
	case CNT_TECHNO:   return "Techno";
	case CNT_INFANTRY: return "Infantry";
	case CNT_UNIT:     return "Unit";
	case CNT_AIRCRAFT: return "Aircraft";
	case CNT_BUILDING: return "Building";
	case CNT_BULLET:   return "Bullet";
	case CNT_ANIM:     return "Anim";
	case CNT_TEAM:     return "Team";
	case CNT_HOUSE:    return "House";
	case CNT_RADSITE:  return "RadSite";
	case CNT_FACTORY:  return "Factory";
	case CNT_SUPER:    return "Super";
	case CNT_TRIGGER:  return "Trigger";
	case CNT_TEMPORAL: return "Temporal";
	case CNT_CAPTURE:  return "Capture";
	case CNT_SPAWN:    return "Spawn";
	case CNT_SLAVE:    return "Slave";
	case CNT_PARASITE: return "Parasite";
	default:           return "???";
	}
}

// ============================================================================
// Comprehensive CRC - fold Phobos extension data into the frame CRC
// ============================================================================

void SyncCRC::ComputePhobosExtensionCRC(CRCEngine& crc)
{
	CRCTechnoExtensions(crc);
	CRCHouseExtensions(crc);
	CRCBuildingExtensions(crc);
	CRCScenarioExtension(crc);
	CRCTeamExtensions(crc);
	CRCBulletExtensions(crc);
	CRCRadSiteExtensions(crc);
	CRCAttachEffects(crc);
	CRCCellRadiation(crc);
	CRCAnimExtensions(crc);
	CRCFactories(crc);
	CRCSuperWeapons(crc);
	CRCTriggersAndTags(crc);
	CRCTemporals(crc);
	CRCMindControl(crc);
	CRCSpawnAndSlave(crc);
	CRCParasiteAirstrikeEMP(crc);
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
			auto const pShield = pTechnoExt->Shield.get();
			int shieldHP = pShield->GetHP();
			crc(shieldHP);
			crc(pShield->IsActive());
			crc(pShield->IsAvailable());

			// Shield internal state (accessed via friend)
			crc(pShield->Cloak);
			crc(pShield->Temporal);
			crc(pShield->Online);
			crc(pShield->IsSelfHealingEnabled);
			crc(pShield->LastBreakFrame);
			crc(pShield->SelfHealing_Rate_Warhead);
			crc(pShield->Respawn_Rate_Warhead);
			crc(pShield->Timers.SelfHealing.GetTimeLeft());
			crc(pShield->Timers.Respawn.GetTimeLeft());
			crc(pShield->Timers.SelfHealing_CombatRestart.GetTimeLeft());
			crc(pShield->Timers.Respawn_CombatRestart.GetTimeLeft());
			crc(pShield->Timers.SelfHealing_WHModifier.GetTimeLeft());
			crc(pShield->Timers.Respawn_WHModifier.GetTimeLeft());

			// Warhead-modified heal/respawn rates
			crc(static_cast<int>(pShield->SelfHealing_Warhead * 1000.0));
			crc(static_cast<int>(pShield->Respawn_Warhead * 1000.0));
			crc(pShield->SelfHealing_RestartInCombat_Warhead);
			crc(pShield->SelfHealing_RestartInCombatDelay_Warhead);
			crc(pShield->Respawn_RestartInCombat_Warhead);
			crc(pShield->Respawn_RestartInCombatDelay_Warhead);
			crc(static_cast<int>(pShield->LastTechnoHealthRatio * 1000.0));
		}

		// AttachedEffects count (state that affects gameplay decisions)
		int aeCount = static_cast<int>(pTechnoExt->AttachedEffects.size());
		crc(aeCount);
		crc(pTechnoExt->AttachedEffectInvokerCount);

		// Per-AE key state via public accessors
		for (auto const& pAE : pTechnoExt->AttachedEffects)
		{
			crc(pAE->GetRemainingDuration());
			crc(pAE->IsActive());
			crc(pAE->ShouldBeDiscarded);
			crc(pAE->NeedsRecalculateStat);

			// Internal state (accessed via friend)
			crc(pAE->Duration);
			crc(pAE->DurationOverride);
			crc(pAE->Delay);
			crc(pAE->CurrentDelay);
			crc(pAE->InitialDelay);
			crc(pAE->HasInitialized);
			crc(pAE->IsOnline);
			crc(pAE->NeedsDurationRefresh);

			if (pAE->InvokerHouse)
				crc(pAE->InvokerHouse->ArrayIndex);

			if (pAE->Type)
				crc(CRCEngine::String(pAE->Type->Name, 0));
		}

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

		// Gattling state
		crc(pTechnoExt->AccumulatedGattlingValue);
		crc(pTechnoExt->ShouldUpdateGattlingValue);

		// Harvester state
		crc(pTechnoExt->SubterraneanHarvStatus);

		// Target tracking
		crc(pTechnoExt->LastTargetID);
		crc(pTechnoExt->KeepTargetOnMove);

		// Locomotor / movement state
		crc(pTechnoExt->ResetLocomotor);
		crc(pTechnoExt->OnParachuted);
		crc(pTechnoExt->HoverShutdown);

		// Delayed fire state
		crc(pTechnoExt->DelayedFireSequencePaused);
		crc(pTechnoExt->DelayedFireWeaponIndex);

		// Timer states (CDTimerClass stores frame-synced data)
		crc(pTechnoExt->PassengerDeletionTimer.GetTimeLeft());
		crc(pTechnoExt->ChargeTurretTimer.GetTimeLeft());
		crc(pTechnoExt->AutoDeathTimer.GetTimeLeft());
		crc(pTechnoExt->DeployFireTimer.GetTimeLeft());
		crc(pTechnoExt->DelayedFireTimer.GetTimeLeft());
		crc(pTechnoExt->TiberiumEater_Timer.GetTimeLeft());
		crc(pTechnoExt->SimpleDeployerAnimationTimer.GetTimeLeft());

		// Sensor position (affects fog of war)
		crc(pTechnoExt->LastSensorsMapCoords.X);
		crc(pTechnoExt->LastSensorsMapCoords.Y);

		// Additional tracked pointers
		if (pTechnoExt->OriginalPassengerOwner)
			crc(pTechnoExt->OriginalPassengerOwner->ArrayIndex);

		if (pTechnoExt->Strafe_TargetCell)
			crc(pTechnoExt->Strafe_TargetCell->UniqueID);

		if (pTechnoExt->FiringObstacleCell)
			crc(pTechnoExt->FiringObstacleCell->UniqueID);

		crc(pTechnoExt->AirstrikeTargetingMe != nullptr);
		crc(pTechnoExt->BeControlledThreatFrame);
		crc(pTechnoExt->WHAnimRemainingCreationInterval);
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

		// AI targeting overrides (script-set)
		crc(pHouseExt->ForceOnlyTargetHouseEnemy);
		crc(pHouseExt->ForceOnlyTargetHouseEnemyMode);
		crc(pHouseExt->ForceEnemyIndex);

		// Limbo unit counters (affect build limits and owned counts)
		crc(pHouseExt->LimboAircraft.GetTotal());
		crc(pHouseExt->LimboBuildings.GetTotal());
		crc(pHouseExt->LimboInfantry.GetTotal());
		crc(pHouseExt->LimboVehicles.GetTotal());

		// Factory pointers (CRC their UniqueIDs if they exist)
		if (pHouseExt->Factory_BuildingType)
			crc(pHouseExt->Factory_BuildingType->UniqueID);
		if (pHouseExt->Factory_InfantryType)
			crc(pHouseExt->Factory_InfantryType->UniqueID);
		if (pHouseExt->Factory_VehicleType)
			crc(pHouseExt->Factory_VehicleType->UniqueID);
		if (pHouseExt->Factory_NavyType)
			crc(pHouseExt->Factory_NavyType->UniqueID);
		if (pHouseExt->Factory_AircraftType)
			crc(pHouseExt->Factory_AircraftType->UniqueID);

		// Timer states
		crc(pHouseExt->AISuperWeaponDelayTimer.GetTimeLeft());
		crc(pHouseExt->AIFireSaleDelayTimer.GetTimeLeft());

		// AI team delay
		crc(pHouseExt->TeamDelay);

		// SuperWeapon extension shot counts
		for (size_t i = 0; i < pHouseExt->SuperExts.size(); i++)
			crc(pHouseExt->SuperExts[i].ShotCount);

		// Radar state
		crc(pHouseExt->FreeRadar);
		crc(pHouseExt->ForceRadar);
		crc(pHouseExt->CombatAlertTimer.GetTimeLeft());

		// PowerPlant enhancers (affects power calculations)
		crc(static_cast<int>(pHouseExt->PowerPlantEnhancers.size()));
		for (auto const& [idx, bonus] : pHouseExt->PowerPlantEnhancers)
		{
			crc(idx);
			crc(bonus);
		}

		// Suspended EMP pulse SWs
		crc(static_cast<int>(pHouseExt->SuspendedEMPulseSWs.size()));

		// Restricted factory plants
		crc(static_cast<int>(pHouseExt->RestrictedFactoryPlants.size()));
		for (auto const pPlant : pHouseExt->RestrictedFactoryPlants)
		{
			if (pPlant)
				crc(pPlant->UniqueID);
		}
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
		crc(pBuildingExt->IsFiringNow);

		if (pBuildingExt->CurrentAirFactory)
			crc(pBuildingExt->CurrentAirFactory->UniqueID);

		if (pBuildingExt->CurrentEMPulseSW)
			crc(pBuildingExt->CurrentEMPulseSW->UniqueID);

		crc(pBuildingExt->CurrentLaserWeaponIndex.has_value());
		if (pBuildingExt->CurrentLaserWeaponIndex.has_value())
			crc(pBuildingExt->CurrentLaserWeaponIndex.value());
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

	// Limbo launchers and tracker vectors
	crc(static_cast<int>(pScenarioExt->LimboLaunchers.size()));
	crc(pScenarioExt->UndergroundTracker.Count);
	crc(pScenarioExt->FallingDownTracker.Count);
}

void SyncCRC::CRCTeamExtensions(CRCEngine& crc)
{
	for (auto const pTeam : TeamClass::Array)
	{
		auto const pTeamExt = TeamExt::ExtMap.Find(pTeam);
		if (!pTeamExt)
			continue;

		// AI script decision state
		crc(pTeamExt->WaitNoTargetAttempts);
		crc(pTeamExt->NextSuccessWeightAward);
		crc(pTeamExt->IdxSelectedObjectFromAIList);
		crc(pTeamExt->CloseEnough);
		crc(pTeamExt->Countdown_RegroupAtLeader);
		crc(pTeamExt->MoveMissionEndMode);
		crc(pTeamExt->WaitNoTargetCounter);

		// Timer states
		crc(pTeamExt->WaitNoTargetTimer.GetTimeLeft());
		crc(pTeamExt->ForceJump_Countdown.GetTimeLeft());
		crc(pTeamExt->ForceJump_InitialCountdown);
		crc(pTeamExt->ForceJump_RepeatMode);

		// Team leader identity
		if (pTeamExt->TeamLeader)
			crc(pTeamExt->TeamLeader->UniqueID);

		// Script history depth
		crc(static_cast<int>(pTeamExt->PreviousScriptList.size()));
	}
}

void SyncCRC::CRCBulletExtensions(CRCEngine& crc)
{
	for (auto const pBullet : BulletClass::Array)
	{
		auto const pBulletExt = BulletExt::ExtMap.Find(pBullet);
		if (!pBulletExt)
			continue;

		crc(pBulletExt->CurrentStrength);
		crc(static_cast<int>(pBulletExt->InterceptedStatus));
		crc(pBulletExt->DetonateOnInterception);
		crc(pBulletExt->SnappedToTarget);
		crc(pBulletExt->IsInstantDetonation);

		if (pBulletExt->FirerHouse)
			crc(pBulletExt->FirerHouse->ArrayIndex);
	}
}

void SyncCRC::CRCRadSiteExtensions(CRCEngine& crc)
{
	for (auto const pRadSite : RadSiteClass::Array)
	{
		auto const pRadSiteExt = RadSiteExt::ExtMap.Find(pRadSite);
		if (!pRadSiteExt)
			continue;

		// CRC the identity of the rad type (affects damage calculations)
		if (pRadSiteExt->Type)
			crc(CRCEngine::String(pRadSiteExt->Type->Name, 0));

		// CRC ownership (affects friend/foe damage)
		if (pRadSiteExt->RadHouse)
			crc(pRadSiteExt->RadHouse->ArrayIndex);

		// CRC the vanilla rad level for this site
		crc(pRadSite->RadLevel);
	}
}

void SyncCRC::CRCAttachEffects(CRCEngine& crc)
{
	// Aggregate CRC over all active attach effects
	// Individual per-AE state is already CRC'd in CRCTechnoExtensions;
	// here we CRC the global array count as a consistency check
	crc(static_cast<int>(AttachEffectClass::Array.size()));
}

void SyncCRC::CRCCellRadiation(CRCEngine& crc)
{
	// CRC aggregate radiation state rather than per-cell to avoid cost
	// RadSite extensions are already CRC'd individually above;
	// here we CRC the total count of cells with radiation as a consistency check
	int cellsWithRad = 0;
	int totalRadLevel = 0;

	for (auto const pRadSite : RadSiteClass::Array)
	{
		totalRadLevel += pRadSite->RadLevel;
		if (pRadSite->RadLevel > 0)
			cellsWithRad++;
	}

	crc(cellsWithRad);
	crc(totalRadLevel);
}

void SyncCRC::CRCAnimExtensions(CRCEngine& crc)
{
	for (auto const pAnim : AnimClass::Array)
	{
		auto const pAnimExt = AnimExt::ExtMap.Find(pAnim);
		if (!pAnimExt)
			continue;

		crc(static_cast<int>(pAnimExt->DeathUnitFacing));
		crc(pAnimExt->FromDeathUnit);
		crc(pAnimExt->DeathUnitHasTurret);
		crc(pAnimExt->IsTechnoTrailerAnim);
		crc(pAnimExt->DelayedFireRemoveOnNoDelay);
		crc(pAnimExt->IsAttachedEffectAnim);
		crc(pAnimExt->IsShieldIdleAnim);

		if (pAnimExt->Invoker)
			crc(pAnimExt->Invoker->UniqueID);

		if (pAnimExt->InvokerHouse)
			crc(pAnimExt->InvokerHouse->ArrayIndex);

		if (pAnimExt->ParentBuilding)
			crc(pAnimExt->ParentBuilding->UniqueID);
	}
}

void SyncCRC::CRCFactories(CRCEngine& crc)
{
	for (auto const pFactory : FactoryClass::Array)
	{
		if (pFactory->Object)
			crc(pFactory->Object->UniqueID);

		crc(pFactory->OnHold);
		crc(pFactory->IsSuspended);
		crc(pFactory->Balance);
		crc(pFactory->OriginalBalance);
		crc(pFactory->QueuedObjects.Count);

		if (pFactory->Owner)
			crc(pFactory->Owner->ArrayIndex);
	}
}

void SyncCRC::CRCSuperWeapons(CRCEngine& crc)
{
	for (auto const pSuper : SuperClass::Array)
	{
		crc(pSuper->IsReady);
		crc(pSuper->IsPresent);
		crc(pSuper->IsSuspended);
		crc(pSuper->RechargeTimer.GetTimeLeft());
		crc(static_cast<int>(pSuper->ChargeDrainState));
		crc(pSuper->ReadyFrame);

		if (pSuper->Owner)
			crc(pSuper->Owner->ArrayIndex);
	}
}

void SyncCRC::CRCTriggersAndTags(CRCEngine& crc)
{
	for (auto const pTrigger : TriggerClass::Array)
		pTrigger->ComputeCRC(crc);

	for (auto const pTag : TagClass::Array)
		pTag->ComputeCRC(crc);
}

void SyncCRC::CRCTemporals(CRCEngine& crc)
{
	for (auto const pTemporal : TemporalClass::Array)
	{
		if (pTemporal->Target)
			crc(pTemporal->Target->UniqueID);

		if (pTemporal->Owner)
			crc(pTemporal->Owner->UniqueID);

		crc(pTemporal->WarpRemaining);
		crc(pTemporal->WarpPerStep);
		crc(pTemporal->LifeTimer.GetTimeLeft());
	}
}

void SyncCRC::CRCMindControl(CRCEngine& crc)
{
	for (auto const pCapture : CaptureManagerClass::Array)
	{
		if (pCapture->Owner)
			crc(pCapture->Owner->UniqueID);

		crc(pCapture->ControlNodes.Count);
		crc(pCapture->MaxControlNodes);
		crc(pCapture->InfiniteMindControl);

		for (int i = 0; i < pCapture->ControlNodes.Count; i++)
		{
			auto const pNode = pCapture->ControlNodes[i];
			if (pNode->Unit)
				crc(pNode->Unit->UniqueID);

			if (pNode->OriginalOwner)
				crc(pNode->OriginalOwner->ArrayIndex);
		}
	}
}

void SyncCRC::CRCSpawnAndSlave(CRCEngine& crc)
{
	for (auto const pSpawn : SpawnManagerClass::Array)
	{
		crc(static_cast<int>(pSpawn->Status));
		crc(pSpawn->SpawnCount);
		crc(pSpawn->SpawnedNodes.Count);
		crc(pSpawn->UpdateTimer.GetTimeLeft());
		crc(pSpawn->SpawnTimer.GetTimeLeft());

		if (pSpawn->Owner)
			crc(pSpawn->Owner->UniqueID);

		if (pSpawn->Target)
			crc(pSpawn->Target->UniqueID);
	}

	for (auto const pSlave : SlaveManagerClass::Array)
	{
		crc(static_cast<int>(pSlave->State));
		crc(pSlave->SlaveCount);
		crc(pSlave->SlaveNodes.Count);
		crc(pSlave->RespawnTimer.GetTimeLeft());

		if (pSlave->Owner)
			crc(pSlave->Owner->UniqueID);
	}
}

void SyncCRC::CRCParasiteAirstrikeEMP(CRCEngine& crc)
{
	for (auto const pParasite : ParasiteClass::Array)
	{
		if (pParasite->Owner)
			crc(pParasite->Owner->UniqueID);

		if (pParasite->Victim)
			crc(pParasite->Victim->UniqueID);

		crc(static_cast<int>(pParasite->GrappleState));
		crc(pParasite->SuppressionTimer.GetTimeLeft());
		crc(pParasite->DamageDeliveryTimer.GetTimeLeft());
	}

	for (auto const pAirstrike : AirstrikeClass::Array)
	{
		if (pAirstrike->Owner)
			crc(pAirstrike->Owner->UniqueID);

		if (pAirstrike->Target)
			crc(pAirstrike->Target->UniqueID);

		crc(pAirstrike->IsOnMission);
		crc(pAirstrike->AirstrikeTeam);
	}

	for (auto const pEMP : EMPulseClass::Array)
	{
		crc(pEMP->BaseCoords.X);
		crc(pEMP->BaseCoords.Y);
		crc(pEMP->Spread);
		crc(pEMP->Duration);
		crc(pEMP->CreationTime);
	}
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
	FrameRNGCallCount = 0;
}

void SyncCRC::TakeCheckpoint(const char* label)
{
	if (CurrentFrame.ActiveCount >= MAX_CHECKPOINTS)
		return;

	int idx = CurrentFrame.ActiveCount;

	// Compute per-category CRCs
	CRCEngine catCRCs[NUM_CRC_CATEGORIES] {};

	// Vanilla frame CRC
	catCRCs[CAT_VANILLA](EventClass::CurrentFrameCRC);

	// RNG state
	catCRCs[CAT_RNG](ScenarioClass::Instance->Random.Next1);
	catCRCs[CAT_RNG](ScenarioClass::Instance->Random.Next2);

	// Phobos extensions - one CRCEngine per category
	CRCTechnoExtensions(catCRCs[CAT_TECHNO]);
	CRCHouseExtensions(catCRCs[CAT_HOUSE]);
	CRCBuildingExtensions(catCRCs[CAT_BUILDING]);
	CRCScenarioExtension(catCRCs[CAT_SCENARIO]);
	CRCTeamExtensions(catCRCs[CAT_TEAM]);
	CRCBulletExtensions(catCRCs[CAT_BULLET]);
	CRCRadSiteExtensions(catCRCs[CAT_RADSITE]);
	CRCAttachEffects(catCRCs[CAT_AE]);
	CRCCellRadiation(catCRCs[CAT_CELL]);
	CRCAnimExtensions(catCRCs[CAT_ANIM]);

	// Vanilla game array CRCs
	CRCFactories(catCRCs[CAT_FACTORY]);
	CRCSuperWeapons(catCRCs[CAT_SUPER]);
	CRCTriggersAndTags(catCRCs[CAT_TRIGGER]);
	CRCTemporals(catCRCs[CAT_TEMPORAL]);
	CRCMindControl(catCRCs[CAT_MINDCTRL]);
	CRCSpawnAndSlave(catCRCs[CAT_SPAWN]);
	CRCParasiteAirstrikeEMP(catCRCs[CAT_PARASITE]);

	// Store per-category CRCs and compute combined
	CRCEngine combined {};
	for (int c = 0; c < NUM_CRC_CATEGORIES; c++)
	{
		DWORD catVal = static_cast<DWORD>(static_cast<int>(catCRCs[c]));
		CurrentFrame.CategoryCRCs[idx][c] = catVal;
		combined(catVal);
	}

	CurrentFrame.CRCs[idx] = static_cast<DWORD>(static_cast<int>(combined));
	CurrentFrame.Labels[idx] = label;

	// Snapshot RNG call count
	CurrentFrame.RNGCallCount[idx] = FrameRNGCallCount;

	// Snapshot object counts
	SnapshotObjectCounts(CurrentFrame.ObjectCounts[idx]);

	CurrentFrame.ActiveCount++;
}

void SyncCRC::SnapshotObjectCounts(int counts[NUM_COUNT_CATEGORIES])
{
	counts[CNT_TECHNO]   = TechnoClass::Array.Count;
	counts[CNT_INFANTRY] = InfantryClass::Array.Count;
	counts[CNT_UNIT]     = UnitClass::Array.Count;
	counts[CNT_AIRCRAFT] = AircraftClass::Array.Count;
	counts[CNT_BUILDING] = BuildingClass::Array.Count;
	counts[CNT_BULLET]   = BulletClass::Array.Count;
	counts[CNT_ANIM]     = AnimClass::Array.Count;
	counts[CNT_TEAM]     = TeamClass::Array.Count;
	counts[CNT_HOUSE]    = HouseClass::Array.Count;
	counts[CNT_RADSITE]  = RadSiteClass::Array.Count;
	counts[CNT_FACTORY]  = FactoryClass::Array.Count;
	counts[CNT_SUPER]    = SuperClass::Array.Count;
	counts[CNT_TRIGGER]  = TriggerClass::Array.Count;
	counts[CNT_TEMPORAL] = TemporalClass::Array.Count;
	counts[CNT_CAPTURE]  = CaptureManagerClass::Array.Count;
	counts[CNT_SPAWN]    = SpawnManagerClass::Array.Count;
	counts[CNT_SLAVE]    = SlaveManagerClass::Array.Count;
	counts[CNT_PARASITE] = ParasiteClass::Array.Count;
}

const FrameCheckpointData& SyncCRC::GetCurrentFrameData()
{
	return CurrentFrame;
}

// ============================================================================
// Per-Object CRC Dump (for desync diagnosis)
// ============================================================================

void SyncCRC::WritePerObjectCRCDump(FILE* pLogFile)
{
	fprintf(pLogFile, "Per-Object CRC Dump (Frame %d):\n\n", Unsorted::CurrentFrame);

	// Technos (Infantry, Units, Aircraft, Buildings + all via TechnoClass::Array)
	fprintf(pLogFile, "  TechnoClass::Array (%d objects):\n", TechnoClass::Array.Count);
	for (int i = 0; i < TechnoClass::Array.Count; i++)
	{
		auto const pTechno = TechnoClass::Array[i];
		if (!pTechno)
			continue;

		// Vanilla CRC via GetCRC
		CRCEngine vanillaCrc {};
		pTechno->ComputeCRC(vanillaCrc);
		DWORD vanillaVal = static_cast<DWORD>(static_cast<int>(vanillaCrc));

		// Phobos ext CRC
		CRCEngine extCrc {};
		auto const pExt = TechnoExt::ExtMap.Find(pTechno);
		if (pExt)
		{
			// Re-CRC this single techno's extension data
			if (pExt->Shield)
			{
				auto const pShield = pExt->Shield.get();
				int shieldHP = pShield->GetHP();
				extCrc(shieldHP);
				extCrc(pShield->IsActive());
				extCrc(pShield->IsAvailable());
				extCrc(pShield->Cloak);
				extCrc(pShield->Temporal);
				extCrc(pShield->Online);
			}

			int aeCount = static_cast<int>(pExt->AttachedEffects.size());
			extCrc(aeCount);
			extCrc(pExt->IsInTunnel);
			extCrc(pExt->IsBurrowed);
			extCrc(pExt->HasBeenPlacedOnMap);
			extCrc(pExt->CurrentAircraftWeaponIndex);
			extCrc(pExt->ReceiveDamage);
		}

		DWORD extVal = static_cast<DWORD>(static_cast<int>(extCrc));

		auto const pType = pTechno->GetTechnoType();
		const char* typeID = pType ? pType->get_ID() : "?";
		int owner = pTechno->Owner ? pTechno->Owner->ArrayIndex : -1;
		auto coords = pTechno->GetCoords();

		fprintf(pLogFile, "    [%5d] UID:%08d RTTI:%02d Type:%-24s Owner:%2d Coords:%6d,%6d,%6d VanillaCRC:%08X ExtCRC:%08X\n",
			i, pTechno->UniqueID, static_cast<int>(pTechno->WhatAmI()), typeID, owner,
			coords.X, coords.Y, coords.Z, vanillaVal, extVal);
	}

	fprintf(pLogFile, "\n");

	// Houses
	fprintf(pLogFile, "  HouseClass::Array (%d objects):\n", HouseClass::Array.Count);
	for (int i = 0; i < HouseClass::Array.Count; i++)
	{
		auto const pHouse = HouseClass::Array[i];
		if (!pHouse)
			continue;

		CRCEngine vanillaCrc {};
		pHouse->ComputeCRC(vanillaCrc);
		DWORD vanillaVal = static_cast<DWORD>(static_cast<int>(vanillaCrc));

		CRCEngine extCrc {};
		auto const pExt = HouseExt::ExtMap.Find(pHouse);
		if (pExt)
		{
			extCrc(static_cast<int>(pExt->OwnedLimboDeliveredBuildings.size()));
			extCrc(pExt->LastBuiltNavalVehicleType);
			extCrc(pExt->NumAirpads_NonMFB);
			extCrc(pExt->NumBarracks_NonMFB);
			extCrc(pExt->NumWarFactories_NonMFB);
			extCrc(pExt->ForceOnlyTargetHouseEnemy);
			extCrc(pExt->ForceEnemyIndex);
		}

		DWORD extVal = static_cast<DWORD>(static_cast<int>(extCrc));

		fprintf(pLogFile, "    [%2d] UID:%08d Type:%-24s Idx:%2d VanillaCRC:%08X ExtCRC:%08X\n",
			i, pHouse->UniqueID, pHouse->Type ? pHouse->Type->get_ID() : "?",
			pHouse->ArrayIndex, vanillaVal, extVal);
	}

	fprintf(pLogFile, "\n");

	// Bullets
	fprintf(pLogFile, "  BulletClass::Array (%d objects):\n", BulletClass::Array.Count);
	for (int i = 0; i < BulletClass::Array.Count; i++)
	{
		auto const pBullet = BulletClass::Array[i];
		if (!pBullet)
			continue;

		CRCEngine vanillaCrc {};
		pBullet->ComputeCRC(vanillaCrc);
		DWORD vanillaVal = static_cast<DWORD>(static_cast<int>(vanillaCrc));

		CRCEngine extCrc {};
		auto const pExt = BulletExt::ExtMap.Find(pBullet);
		if (pExt)
		{
			extCrc(pExt->CurrentStrength);
			extCrc(static_cast<int>(pExt->InterceptedStatus));
			extCrc(pExt->SnappedToTarget);
		}

		DWORD extVal = static_cast<DWORD>(static_cast<int>(extCrc));

		auto const pType = pBullet->Type;
		auto coords = pBullet->GetCoords();

		fprintf(pLogFile, "    [%5d] UID:%08d Type:%-24s Coords:%6d,%6d,%6d VanillaCRC:%08X ExtCRC:%08X\n",
			i, pBullet->UniqueID, pType ? pType->get_ID() : "?",
			coords.X, coords.Y, coords.Z, vanillaVal, extVal);
	}

	fprintf(pLogFile, "\n");

	// Teams
	fprintf(pLogFile, "  TeamClass::Array (%d objects):\n", TeamClass::Array.Count);
	for (int i = 0; i < TeamClass::Array.Count; i++)
	{
		auto const pTeam = TeamClass::Array[i];
		if (!pTeam)
			continue;

		CRCEngine vanillaCrc {};
		pTeam->ComputeCRC(vanillaCrc);
		DWORD vanillaVal = static_cast<DWORD>(static_cast<int>(vanillaCrc));

		CRCEngine extCrc {};
		auto const pExt = TeamExt::ExtMap.Find(pTeam);
		if (pExt)
		{
			extCrc(pExt->WaitNoTargetAttempts);
			extCrc(pExt->IdxSelectedObjectFromAIList);
			extCrc(pExt->MoveMissionEndMode);
		}

		DWORD extVal = static_cast<DWORD>(static_cast<int>(extCrc));

		fprintf(pLogFile, "    [%5d] UID:%08d Type:%-24s Owner:%2d VanillaCRC:%08X ExtCRC:%08X\n",
			i, pTeam->UniqueID, pTeam->Type ? pTeam->Type->get_ID() : "?",
			pTeam->Owner ? pTeam->Owner->ArrayIndex : -1, vanillaVal, extVal);
	}

	fprintf(pLogFile, "\n");
}

// ============================================================================
// Augment CurrentFrameCRC with Phobos extension data.
// Called from the Queue_AI desync log hook (SyncLogging.cpp) right before
// the CRC is used, so the augmented CRC is what gets compared and logged.
// ============================================================================

void SyncCRC::AugmentCurrentFrameCRC()
{
	CRCEngine crc {};
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

	auto writeCheckpoints = [&](const FrameCheckpointData& data, const char* prefix)
	{
		fprintf(pLogFile, "  %s (%*d): %d checkpoints\n",
			prefix, frameDigits, data.Frame, data.ActiveCount);

		for (int i = 0; i < data.ActiveCount; i++)
		{
			fprintf(pLogFile, "    [%2d] %08X  %-28s  RNG#:%5d",
				i, data.CRCs[i],
				data.Labels[i] ? data.Labels[i] : "unnamed",
				data.RNGCallCount[i]);

			// Per-category CRC breakdown
			fprintf(pLogFile, "  |");
			for (int c = 0; c < NUM_CRC_CATEGORIES; c++)
			{
				fprintf(pLogFile, " %s:%08X", CRCCategoryName(c), data.CategoryCRCs[i][c]);
			}

			fprintf(pLogFile, "\n");

			// Object counts
			fprintf(pLogFile, "         Counts:");
			for (int c = 0; c < NUM_COUNT_CATEGORIES; c++)
			{
				fprintf(pLogFile, " %s:%d", ObjectCountCategoryName(c), data.ObjectCounts[i][c]);
			}
			fprintf(pLogFile, "\n");
		}
	};

	// Write current frame's checkpoints
	if (CurrentFrame.ActiveCount > 0)
		writeCheckpoints(CurrentFrame, "Current frame");

	// Write recent history (last 8 frames with checkpoints)
	int written = 0;
	for (int offset = 1; offset < HISTORY_SIZE && written < 8; offset++)
	{
		int idx = (CurrentHistoryIndex - offset + HISTORY_SIZE) % HISTORY_SIZE;
		auto const& data = FrameHistory[idx];

		if (data.ActiveCount <= 0)
			continue;

		char prefix[32];
		_snprintf_s(prefix, _TRUNCATE, "Frame");
		writeCheckpoints(data, prefix);

		written++;
	}

	fprintf(pLogFile, "\n");
}
