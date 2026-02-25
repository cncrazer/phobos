#pragma once

#include <string>
#include <vector>

#include <HouseClass.h>
#include <SessionClass.h>
#include <GeneralDefinitions.h>

// Manages vote-to-kick in Internet multiplayer games.
//
// Transport: votes ride the existing synchronized chat-message channel.
//   Phobos hooks MessageListClass::AddMessage on every client, detects the two
//   special command strings, rewrites them in-flight to user-friendly text, and
//   updates vote state deterministically on all machines simultaneously.
//
// Player commands (typed in the in-game chat box):
//   /votekick <partial_name>  — start a new vote (or cast yes on the active one)
//   /vote yes                 — cast yes on the active vote
//   /vote no                  — explicitly abstain (informational; does not cancel vote)
//
// Rules:
//   • 5-minute cooldown between votes (wall-clock time).
//   • 1-minute window to collect votes.
//   • Majority = more than half of all active non-target human players,
//     minimum 2 votes.
//   • When majority is reached the targeted player's own client quietly
//     queues an EventType::Exit event, gracefully removing them from the game.

class VoteKickClass
{
public:
    static VoteKickClass Instance;

    // ---- Tunables ----
    static constexpr ULONGLONG VoteDurationMs = 60'000ULL;   // 1-minute voting window
    static constexpr ULONGLONG CooldownMs     = 300'000ULL;  // 5-minute between-vote cooldown
    static constexpr int       MinVotesNeeded = 2;            // floor for majority threshold

    // ---- Active-vote state ----
    int           TargetHouseIdx = -1;   // HouseClass::ArrayIndex of candidate; -1 = none
    ULONGLONG     StartTimeMs    = 0;    // GetTickCount64() when current vote began
    std::vector<int> Voters;             // ArrayIndex of each player who voted yes

    // ---- Cooldown state ----
    ULONGLONG     CooldownEndMs  = 0;    // GetTickCount64() after which a new vote may start

    // ---- Public interface ----

    bool IsActive() const { return TargetHouseIdx >= 0; }

    // Called once per game frame to detect vote-window expiry.
    void Update();

    // Called from the AddMessage hook for /votekick <name>.
    // Writes a human-readable status into outBuf[outBufSize].
    void ProcessVoteKick(int senderHouseIdx, const wchar_t* targetName,
                         wchar_t* outBuf, size_t outBufSize);

    // Called from the AddMessage hook for /vote yes.
    void ProcessVoteYes(int senderHouseIdx, wchar_t* outBuf, size_t outBufSize);

    // Called from the AddMessage hook for /vote no.
    void ProcessVoteNo(int senderHouseIdx, wchar_t* outBuf, size_t outBufSize);

    // Clears all vote state (and arms the cooldown timer).
    void ResetVote();

    // True when the local human player is the current vote target.
    bool IsLocalPlayerTarget() const;

    // Find a human-controlled HouseClass* by partial WOL-handle match.
    // Returns nullptr on no-match or ambiguous (multiple) match.
    static HouseClass* FindPlayerByName(const wchar_t* fragment);

    // Count active (alive, human) players.
    static int CountActivePlayers();

    // Minimum yes-votes needed to reach majority, given active player count.
    static int MajorityThreshold(int activePlayers);

private:
    void CheckMajorityAndKick();

    // Retrieve a player's WOL handle from NodeNameType::Array by house index.
    static std::wstring GetPlayerHandle(int houseIdx);

    // Print a system-level (non-player-attributed) message in all chat panels.
    static void ShowSystemMessage(const wchar_t* text);
};
