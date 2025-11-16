#include "Body.h"

#include <Ext/House/Body.h>
#include <Ext/Rules/Body.h>

DEFINE_HOOK(0x4401BB, BuildingClass_AI_PickWithFreeDocks, 0x6)
{
	GET(BuildingClass*, pBuilding, ESI);

	auto const pOwner = pBuilding->Owner;
	const int index = pOwner->ProducingAircraftTypeIndex;
	auto const pType = index >= 0 ? AircraftTypeClass::Array.GetItem(index) : nullptr;

	if (RulesExt::Global()->AllowParallelAIQueues && !RulesExt::Global()->ForbidParallelAIQueues_Aircraft && (!pType || !TechnoTypeExt::ExtMap.Find(pType)->ForbidParallelAIQueues))
		return 0;

	if (pOwner->Type->MultiplayPassive
		|| pOwner->IsCurrentPlayer()
		|| pOwner->IsNeutral())
		return 0;

	if (pBuilding->Type->Factory == AbstractType::AircraftType)
	{
		if (pBuilding->Factory
			&& !BuildingExt::HasFreeDocks(pBuilding))
		{
			if (auto const pBldExt = BuildingExt::ExtMap.TryFind(pBuilding))
				pBldExt->UpdatePrimaryFactoryAI();
		}
	}

	return 0;
}

DEFINE_HOOK(0x4502F4, BuildingClass_Update_Factory_Phobos, 0x6)
{
	GET(BuildingClass*, pThis, ESI);
	const HouseClass* pOwner = pThis->Owner;

	if (pOwner->Production && RulesExt::Global()->AllowParallelAIQueues)
	{
		auto const pOwnerExt = HouseExt::ExtMap.Find(pOwner);
		auto const pFactory = pThis->Type->Factory;
		const bool naval = pThis->Type->Naval;
		BuildingClass** currFactory = nullptr;

		switch (pFactory)
		{
		case AbstractType::BuildingType:
			// If building-tab parallelism is enabled, track separate factories for Production vs Defensive tabs.
			if (RulesExt::Global()->AllowParallelAIQueues_BuildingTabs)
			{
				auto const pHouse = pThis->Owner;
				bool isDefenseFactory = false;
				// Prefer explicit virtual-factory classification when configured with a specific type
				if (RulesExt::Global()->AllowParallelAIQueues_BuildingTabs_VirtualFactory
					&& RulesExt::Global()->AllowParallelAIQueues_BuildingTabs_VirtualFactoryType
					&& pThis->Type == RulesExt::Global()->AllowParallelAIQueues_BuildingTabs_VirtualFactoryType)
				{
					isDefenseFactory = true;
				}
				else if (pHouse->Primary_ForDefenses && pHouse->Primary_ForDefenses == pThis->Factory)
				{
					// Explicit defense primary
					isDefenseFactory = true;
				}
				else if (RulesExt::Global()->AllowParallelAIQueues_BuildingTabs_VirtualFactory)
				{
					// Automatic virtual factory mode: first seen building factory becomes Production,
					// the next distinct building factory becomes Defense.
					if (pOwnerExt->Factory_BuildingType_Production && pOwnerExt->Factory_BuildingType_Production != pThis)
						isDefenseFactory = true;
				}
				currFactory = isDefenseFactory ? &pOwnerExt->Factory_BuildingType_Defense : &pOwnerExt->Factory_BuildingType_Production;
			}
			else
			{
				currFactory = &pOwnerExt->Factory_BuildingType;
			}
			break;
		case AbstractType::UnitType:
			currFactory = naval ? &pOwnerExt->Factory_NavyType : &pOwnerExt->Factory_VehicleType;
			break;
		case AbstractType::InfantryType:
			currFactory = &pOwnerExt->Factory_InfantryType;
			break;
		case AbstractType::AircraftType:
			currFactory = &pOwnerExt->Factory_AircraftType;
			break;
		default:
			break;
		}

		if (!*currFactory)
		{
			*currFactory = pThis;
			return 0;
		}
		else if (*currFactory != pThis)
		{
			enum { Skip = 0x4503CA };


			TechnoTypeClass* pType = nullptr;
			int index = -1;

			switch (pFactory)
			{
			case AbstractType::BuildingType:
				if (RulesExt::Global()->ForbidParallelAIQueues_Building || !RulesExt::Global()->AllowParallelAIQueues_BuildingTabs)
					return Skip;

				index = pOwner->ProducingBuildingTypeIndex;
				pType = index >= 0 ? BuildingTypeClass::Array.GetItem(index) : nullptr;
				break;
			case AbstractType::InfantryType:
				if (RulesExt::Global()->ForbidParallelAIQueues_Infantry)
					return Skip;

				index = pOwner->ProducingInfantryTypeIndex;
				pType = index >= 0 ? InfantryTypeClass::Array.GetItem(index) : nullptr;
				break;
			case AbstractType::AircraftType:
				if (RulesExt::Global()->ForbidParallelAIQueues_Aircraft)
					return Skip;

				index = pOwner->ProducingAircraftTypeIndex;
				pType = index >= 0 ? AircraftTypeClass::Array.GetItem(index) : nullptr;
				break;
			case AbstractType::UnitType:
				if (naval ? RulesExt::Global()->ForbidParallelAIQueues_Navy : RulesExt::Global()->ForbidParallelAIQueues_Vehicle)
					return Skip;

				index = naval ? HouseExt::ExtMap.Find(pOwner)->ProducingNavalUnitTypeIndex : pOwner->ProducingUnitTypeIndex;
				pType = index >= 0 ? UnitTypeClass::Array.GetItem(index) : nullptr;
				break;
			default:
				break;
			}

			if (pType && TechnoTypeExt::ExtMap.Find(pType)->ForbidParallelAIQueues)
				return Skip;
		}
	}

	return 0;
}

//const byte old_empty_log[] = { 0xC3 };
DEFINE_JUMP(CALL, 0x4CA016, 0x4CA19F); // randomly chosen 0xC3

DEFINE_HOOK(0x4CA07A, FactoryClass_AbandonProduction_Phobos, 0x8)
{
	GET(FactoryClass*, pFactory, ESI);
	GET_STACK(DWORD const, calledby, 0x18);

	auto const pTechno = pFactory->Object;

	if (calledby < 0x7F0000) // Replace the old log with this to figure out where keeps flushing the stream
	{
		Debug::LogGame("(%08x) : %s is abandoning production of %s[%s]\n"
			, calledby - 5
			, pFactory->Owner->PlainName
			, pTechno->GetType()->Name
			, pTechno->get_ID());
	}

	if (!RulesExt::Global()->AllowParallelAIQueues)
		return 0;

	auto const pOwnerExt = HouseExt::ExtMap.Find(pFactory->Owner);
	auto const pType = pTechno->GetTechnoType();
	const bool forbid = TechnoTypeExt::ExtMap.Find(pType)->ForbidParallelAIQueues;

	switch (pTechno->WhatAmI())
	{
	case AbstractType::Building:
		if (RulesExt::Global()->AllowParallelAIQueues_BuildingTabs)
		{
			// Host building for this factory
			auto const pBuildingHost = static_cast<BuildingClass*>(pTechno);
			bool isDefense = false;
			if (RulesExt::Global()->AllowParallelAIQueues_BuildingTabs_VirtualFactory
				&& RulesExt::Global()->AllowParallelAIQueues_BuildingTabs_VirtualFactoryType)
			{
				isDefense = (pBuildingHost->Type == RulesExt::Global()->AllowParallelAIQueues_BuildingTabs_VirtualFactoryType);
			}
			else if (pFactory->Owner->Primary_ForDefenses && pFactory->Owner->Primary_ForDefenses == pFactory)
			{
				isDefense = true;
			}
			else if (RulesExt::Global()->AllowParallelAIQueues_BuildingTabs_VirtualFactory)
			{
				// Automatic virtual factory: if production factory exists and differs, this is defense
				auto const pHouseExt = HouseExt::ExtMap.Find(pFactory->Owner);
				if (pHouseExt->Factory_BuildingType_Production && pHouseExt->Factory_BuildingType_Production != pBuildingHost)
					isDefense = true;
			}
			if (RulesExt::Global()->ForbidParallelAIQueues_Building || forbid)
			{
				if (isDefense)
					pOwnerExt->Factory_BuildingType_Defense = nullptr;
				else
					pOwnerExt->Factory_BuildingType_Production = nullptr;
			}

			// Also clear defense producing index if this was a defense factory abandoning a building
			if (isDefense)
				pOwnerExt->ProducingDefenseBuildingTypeIndex = -1;
		}
		else
		{
			if (RulesExt::Global()->ForbidParallelAIQueues_Building || forbid)
				pOwnerExt->Factory_BuildingType = nullptr;
		}
		break;
	case AbstractType::Unit:
		if (!pType->Naval)
		{
			if (RulesExt::Global()->ForbidParallelAIQueues_Vehicle || forbid)
				pOwnerExt->Factory_VehicleType = nullptr;
		}
		else
		{
			if (RulesExt::Global()->ForbidParallelAIQueues_Navy || forbid)
				pOwnerExt->Factory_NavyType = nullptr;
		}
		break;
	case AbstractType::Infantry:
		if (RulesExt::Global()->ForbidParallelAIQueues_Infantry || forbid)
			pOwnerExt->Factory_InfantryType = nullptr;
		break;
	case AbstractType::Aircraft:
		if (RulesExt::Global()->ForbidParallelAIQueues_Aircraft || forbid)
			pOwnerExt->Factory_AircraftType = nullptr;
		break;
	default:
		break;
	}

	return 0;
}

DEFINE_HOOK(0x444119, BuildingClass_KickOutUnit_UnitType_Phobos, 0x6)
{
	GET(UnitClass*, pUnit, EDI);
	GET(BuildingClass*, pFactory, ESI);

	auto const pHouseExt = HouseExt::ExtMap.Find(pFactory->Owner);

	if (pUnit->Type->Naval && pHouseExt->Factory_NavyType == pFactory)
		pHouseExt->Factory_NavyType = nullptr;
	else if (pHouseExt->Factory_VehicleType == pFactory)
		pHouseExt->Factory_VehicleType = nullptr;

	return 0;
}

DEFINE_HOOK(0x444131, BuildingClass_KickOutUnit_InfantryType_Phobos, 0x6)
{
	GET(BuildingClass*, pFactory, ESI);

	auto const pHouseExt = HouseExt::ExtMap.Find(pFactory->Owner);

	if (pHouseExt->Factory_InfantryType == pFactory)
		pHouseExt->Factory_InfantryType = nullptr;

	return 0;
}

DEFINE_HOOK(0x44531F, BuildingClass_KickOutUnit_BuildingType_Phobos, 0xA)
{
	GET(BuildingClass*, pFactory, ESI);

	auto const pHouseExt = HouseExt::ExtMap.Find(pFactory->Owner);

	if (RulesExt::Global()->AllowParallelAIQueues && RulesExt::Global()->AllowParallelAIQueues_BuildingTabs)
	{
		if (pHouseExt->Factory_BuildingType_Defense == pFactory)
			pHouseExt->Factory_BuildingType_Defense = nullptr;
		else if (pHouseExt->Factory_BuildingType_Production == pFactory)
			pHouseExt->Factory_BuildingType_Production = nullptr;
	}
	else
	{
		if (pHouseExt->Factory_BuildingType == pFactory)
			pHouseExt->Factory_BuildingType = nullptr;
	}

	return 0;
}

DEFINE_HOOK(0x443CCA, BuildingClass_KickOutUnit_AircraftType_Phobos, 0xA)
{
	GET(BuildingClass*, pFactory, ESI);

	auto const pHouseExt = HouseExt::ExtMap.Find(pFactory->Owner);

	if (pHouseExt->Factory_AircraftType == pFactory)
		pHouseExt->Factory_AircraftType = nullptr;

	return 0;
}
