#include "BuildLastOfTab.h"

#include <Utilities/GeneralUtils.h>
#include <Ext/House/Body.h>
#include <HouseClass.h>
#include <EventClass.h>
#include <SidebarClass.h>

static constexpr const char* BuildLastTabNames[4] =
{
	"BuildLastOfTab_Power",
	"BuildLastOfTab_Defense",
	"BuildLastOfTab_Infantry",
	"BuildLastOfTab_Vehicles",
};

static constexpr const char* BuildLastTabDescKeys[4] =
{
	"BuildLastOfTab_Power_Desc",
	"BuildLastOfTab_Defense_Desc",
	"BuildLastOfTab_Infantry_Desc",
	"BuildLastOfTab_Vehicles_Desc",
};

static constexpr const wchar_t* BuildLastTabUINames[4] =
{
	L"Build Last (Power/Resources)",
	L"Build Last (Defense/Combat)",
	L"Build Last (Infantry)",
	L"Build Last (Vehicles/Aircraft)",
};

static constexpr const wchar_t* BuildLastTabUIDescs[4] =
{
	L"Re-queue the last produced Power/Resources building.",
	L"Re-queue the last produced Defense/Combat building.",
	L"Re-queue the last produced Infantry unit.",
	L"Re-queue the last produced Vehicle or Aircraft.",
};

template<int TabIndex>
const char* BuildLastOfTabCommandClass<TabIndex>::GetName() const
{
	return BuildLastTabNames[TabIndex];
}

template<int TabIndex>
const wchar_t* BuildLastOfTabCommandClass<TabIndex>::GetUIName() const
{
	return GeneralUtils::LoadStringUnlessMissing(BuildLastTabNames[TabIndex], BuildLastTabUINames[TabIndex]);
}

template<int TabIndex>
const wchar_t* BuildLastOfTabCommandClass<TabIndex>::GetUICategory() const
{
	return CATEGORY_INTERFACE;
}

template<int TabIndex>
const wchar_t* BuildLastOfTabCommandClass<TabIndex>::GetUIDescription() const
{
	static_assert(TabIndex >= 0 && TabIndex < 4, "TabIndex out of range");
	return GeneralUtils::LoadStringUnlessMissing(BuildLastTabDescKeys[TabIndex], BuildLastTabUIDescs[TabIndex]);
}

template<int TabIndex>
void BuildLastOfTabCommandClass<TabIndex>::Execute(WWKey eInput) const
{
	auto const pPlayer = HouseClass::CurrentPlayer;
	if (!pPlayer)
		return;

	auto const pExt = HouseExt::ExtMap.Find(pPlayer);
	if (!pExt)
		return;

	auto const typeIndex = pExt->LastBuiltPerTab[TabIndex];
	if (typeIndex < 0)
		return;

	// Focus the sidebar to the corresponding tab.
	if (SidebarClass::Instance.IsSidebarActive)
	{
		SidebarClass::Instance.ActiveTabIndex = TabIndex;
		SidebarClass::Instance.SidebarNeedsRepaint();
	}

	auto const rtti = pExt->LastBuiltRTTIPerTab[TabIndex];
	auto const isNaval = pExt->LastBuiltIsNavalPerTab[TabIndex];

	EventClass::OutList.Add(EventClass(
		pPlayer->ArrayIndex,
		EventType::Produce,
		static_cast<int>(rtti),
		typeIndex,
		isNaval ? TRUE : FALSE
	));
}

// Explicit instantiations
template class BuildLastOfTabCommandClass<0>;
template class BuildLastOfTabCommandClass<1>;
template class BuildLastOfTabCommandClass<2>;
template class BuildLastOfTabCommandClass<3>;
