#include "VoteKick.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include <Helpers/Macro.h>
#include <EventClass.h>
#include <GeneralDefinitions.h>
#include <HouseClass.h>
#include <MessageListClass.h>
#include <SessionClass.h>

#include "MessageColumn.h"
#include <Phobos.h>

// ============================================================
// Singleton
// ============================================================

VoteKickClass VoteKickClass::Instance;

// ============================================================
// Internal helpers
// ============================================================

static std::wstring ToLowerW(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

std::wstring VoteKickClass::GetPlayerHandle(int houseIdx)
{
    for (int i = 0; i < NodeNameType::Array.Count; ++i)
    {
        const auto pNode = NodeNameType::Array.Items[i];
        if (pNode && pNode->HouseIndex == houseIdx)
            return std::wstring(pNode->Name);
    }
    return L"Unknown";
}

void VoteKickClass::ShowSystemMessage(const wchar_t* text)
{
    MessageListClass::Instance.PrintMessage(text, 0x96);

    if (Phobos::Config::MessageDisplayInCenter)
        MessageColumnClass::Instance.AddMessage(nullptr, text, 0x96, false);
}

// ============================================================
// Public interface
// ============================================================

HouseClass* VoteKickClass::FindPlayerByName(const wchar_t* fragment)
{
    if (!fragment || !*fragment)
        return nullptr;

    const std::wstring needle = ToLowerW(std::wstring(fragment));
    HouseClass* found = nullptr;

    for (int i = 0; i < NodeNameType::Array.Count; ++i)
    {
        const auto pNode = NodeNameType::Array.Items[i];
        if (!pNode)
            continue;

        const int hIdx = pNode->HouseIndex;
        if (hIdx < 0 || hIdx >= HouseClass::Array.Count)
            continue;

        const auto pHouse = HouseClass::Array.Items[hIdx];
        if (!pHouse || !pHouse->IsHumanPlayer)
            continue;

        const std::wstring name = ToLowerW(std::wstring(pNode->Name));
        if (name.find(needle) != std::wstring::npos)
        {
            if (found)
                return nullptr;   // ambiguous match
            found = pHouse;
        }
    }

    return found;
}

int VoteKickClass::CountActivePlayers()
{
    int count = 0;
    for (int i = 0; i < HouseClass::Array.Count; ++i)
    {
        const auto pHouse = HouseClass::Array.Items[i];
        if (pHouse && pHouse->IsHumanPlayer && !pHouse->Defeated)
            ++count;
    }
    return count;
}

int VoteKickClass::MajorityThreshold(int activePlayers)
{
    // Voters = everyone except the target player.
    const int eligibleVoters = activePlayers - 1;
    const int majority = (eligibleVoters / 2) + 1;          // strictly more than half
    return majority < MinVotesNeeded ? MinVotesNeeded : majority;
}

bool VoteKickClass::IsLocalPlayerTarget() const
{
    if (TargetHouseIdx < 0)
        return false;

    const auto pLocal = HouseClass::CurrentPlayer;
    return pLocal && pLocal->ArrayIndex == TargetHouseIdx;
}

void VoteKickClass::ResetVote()
{
    TargetHouseIdx = -1;
    StartTimeMs    = 0;
    Voters.clear();
    CooldownEndMs  = GetTickCount64() + CooldownMs;
}

// ============================================================
// ProcessVoteKick  (/votekick <name>)
// ============================================================

void VoteKickClass::ProcessVoteKick(int senderHouseIdx, const wchar_t* targetName,
                                     wchar_t* outBuf, size_t outBufSize)
{
    const ULONGLONG now          = GetTickCount64();
    const std::wstring senderHdl = GetPlayerHandle(senderHouseIdx);

    // --- Re-use as /vote yes if a vote is already active. ---
    if (IsActive())
    {
        ProcessVoteYes(senderHouseIdx, outBuf, outBufSize);
        return;
    }

    // --- Cooldown guard ---
    if (now < CooldownEndMs)
    {
        const ULONGLONG secsLeft = (CooldownEndMs - now + 999ULL) / 1000ULL;
        const std::wstring msg =
            L"[Vote] " + senderHdl +
            L" wants to start a vote-kick but cooldown is active: " +
            std::to_wstring(secsLeft / 60ULL) + L"m " +
            std::to_wstring(secsLeft % 60ULL) + L"s remaining.";
        wcsncpy_s(outBuf, outBufSize, msg.c_str(), _TRUNCATE);
        return;
    }

    // --- Resolve target ---
    const auto pTarget = FindPlayerByName(targetName);
    if (!pTarget)
    {
        const std::wstring msg =
            L"[Vote] " + senderHdl +
            L" tried /votekick but \"" + std::wstring(targetName) +
            L"\" matches no player (or matches more than one).";
        wcsncpy_s(outBuf, outBufSize, msg.c_str(), _TRUNCATE);
        return;
    }

    // --- Can't vote to kick yourself ---
    if (pTarget->ArrayIndex == senderHouseIdx)
    {
        const std::wstring msg =
            L"[Vote] " + senderHdl + L" cannot start a vote to kick themselves.";
        wcsncpy_s(outBuf, outBufSize, msg.c_str(), _TRUNCATE);
        return;
    }

    // --- Start vote and immediately cast the initiator's yes vote ---
    TargetHouseIdx = pTarget->ArrayIndex;
    StartTimeMs    = now;
    Voters.clear();

    ProcessVoteYes(senderHouseIdx, outBuf, outBufSize);
}

// ============================================================
// ProcessVoteYes  (/vote yes)
// ============================================================

void VoteKickClass::ProcessVoteYes(int senderHouseIdx, wchar_t* outBuf, size_t outBufSize)
{
    const std::wstring senderHdl = GetPlayerHandle(senderHouseIdx);

    if (!IsActive())
    {
        const std::wstring msg =
            L"[Vote] " + senderHdl +
            L" voted yes, but there is no active vote-kick.";
        wcsncpy_s(outBuf, outBufSize, msg.c_str(), _TRUNCATE);
        return;
    }

    // Target cannot cast a vote on their own kick.
    if (senderHouseIdx == TargetHouseIdx)
    {
        const std::wstring msg =
            L"[Vote] " + senderHdl + L" cannot vote on their own kick.";
        wcsncpy_s(outBuf, outBufSize, msg.c_str(), _TRUNCATE);
        return;
    }

    // Record vote (deduplicated).
    const auto it = std::find(Voters.begin(), Voters.end(), senderHouseIdx);
    if (it == Voters.end())
        Voters.push_back(senderHouseIdx);

    const std::wstring targetHdl = GetPlayerHandle(TargetHouseIdx);
    const int active    = CountActivePlayers();
    const int threshold = MajorityThreshold(active);
    const int yesCount  = static_cast<int>(Voters.size());

    const std::wstring msg =
        L"[Vote] " + senderHdl + L" voted to kick " + targetHdl +
        L" (" + std::to_wstring(yesCount) + L"/" +
        std::to_wstring(threshold) + L" votes needed).";
    wcsncpy_s(outBuf, outBufSize, msg.c_str(), _TRUNCATE);

    CheckMajorityAndKick();
}

// ============================================================
// ProcessVoteNo  (/vote no)
// ============================================================

void VoteKickClass::ProcessVoteNo(int senderHouseIdx, wchar_t* outBuf, size_t outBufSize)
{
    const std::wstring senderHdl = GetPlayerHandle(senderHouseIdx);

    if (!IsActive())
    {
        const std::wstring msg =
            L"[Vote] " + senderHdl +
            L" voted no, but there is no active vote-kick.";
        wcsncpy_s(outBuf, outBufSize, msg.c_str(), _TRUNCATE);
        return;
    }

    const std::wstring targetHdl = GetPlayerHandle(TargetHouseIdx);
    const std::wstring msg =
        L"[Vote] " + senderHdl + L" voted NO on kicking " + targetHdl + L".";
    wcsncpy_s(outBuf, outBufSize, msg.c_str(), _TRUNCATE);

    // A /vote no just informs other players but does not cancel the ongoing vote.
}

// ============================================================
// CheckMajorityAndKick
// ============================================================

void VoteKickClass::CheckMajorityAndKick()
{
    const int active    = CountActivePlayers();
    const int threshold = MajorityThreshold(active);
    const int yesCount  = static_cast<int>(Voters.size());

    if (yesCount < threshold)
        return;

    const std::wstring targetHdl = GetPlayerHandle(TargetHouseIdx);
    const std::wstring broadcast =
        L"[Vote] Majority reached! " + targetHdl + L" has been kicked.";
    ShowSystemMessage(broadcast.c_str());

    // Only the kicked player's own game instance submits the exit event to
    // the synchronized queue.  This triggers a clean resignation on all
    // connected machines without any special host authority.
    if (IsLocalPlayerTarget())
    {
        const auto pLocal = HouseClass::CurrentPlayer;
        if (pLocal)
        {
            const EventClass exitEvent(pLocal->ArrayIndex, EventType::Exit);
            EventClass::OutList.Add(exitEvent);
        }
    }

    ResetVote();
}

// ============================================================
// Update  (called once per game frame from MessageColumn hook)
// ============================================================

void VoteKickClass::Update()
{
    if (!SessionClass::IsMultiplayer() || !IsActive())
        return;

    const ULONGLONG now = GetTickCount64();
    if (now - StartTimeMs <= VoteDurationMs)
        return;

    // Voting window expired.
    const std::wstring targetHdl = GetPlayerHandle(TargetHouseIdx);
    const std::wstring msg =
        L"[Vote] Vote to kick " + targetHdl +
        L" expired (not enough votes within 1 minute).";
    ShowSystemMessage(msg.c_str());

    ResetVote();
}

// ============================================================
// Hook: MessageListClass::AddMessage (0x5D3BA0)
//
// Intercepts every incoming chat message on all clients.
// If the message text looks like a vote command, the raw text
// is replaced in-flight with a formatted status string before
// the game renders or records it.
// ============================================================

DEFINE_HOOK(0x5D3BA0, MessageListClass_AddMessage_VoteKick, 0x6)
{
    // Only act in multiplayer games.
    if (!SessionClass::IsMultiplayer())
        return 0;

    // ECX = this (MessageListClass*), stack args follow normal __thiscall layout.
    GET_STACK(const wchar_t*, pMessage, 0xC);   // 3rd argument: message text
    GET_STACK(int,            nID,      0x8);   // 2nd argument: sender house index

    if (!pMessage || !*pMessage)
        return 0;

    // Lower-case copy for case-insensitive command matching.
    const std::wstring msgLower = ToLowerW(std::wstring(pMessage));

    // Static buffer that outlives the AddMessage call (thiscall, not a thread hazard).
    static wchar_t s_VoteMsgBuf[512];
    s_VoteMsgBuf[0] = L'\0';

    if (msgLower.substr(0, 10) == L"/votekick ")
    {
        VoteKickClass::Instance.ProcessVoteKick(
            nID, pMessage + 10, s_VoteMsgBuf, _countof(s_VoteMsgBuf));
        R->Stack(0xC, (const wchar_t*)s_VoteMsgBuf);
    }
    else if (msgLower == L"/vote yes")
    {
        VoteKickClass::Instance.ProcessVoteYes(
            nID, s_VoteMsgBuf, _countof(s_VoteMsgBuf));
        R->Stack(0xC, (const wchar_t*)s_VoteMsgBuf);
    }
    else if (msgLower == L"/vote no")
    {
        VoteKickClass::Instance.ProcessVoteNo(
            nID, s_VoteMsgBuf, _countof(s_VoteMsgBuf));
        R->Stack(0xC, (const wchar_t*)s_VoteMsgBuf);
    }

    return 0;   // Always continue into the original AddMessage body.
}
