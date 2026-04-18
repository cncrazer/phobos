#include <Phobos.h>
#include <Helpers/Macro.h>
#include <Unsorted.h>
#include <GameOptionsClass.h>
#include <Utilities/Debug.h>

// =====================================================================
// Lag detection and networking hooks for multiplayer
//
// These hooks patch sub_648710 (Wait_For_Players / reconnect handler),
// the core multiplayer synchronization function called every frame from
// Queue_AI (sub_6475F0). Two features are implemented:
//
// 1. Majority kick vote: replaces the vanilla "all minus one" unanimous
//    vote requirement with a majority vote (ceil(N/2)).
//
// 2. Lag detection: tracks consecutive frames where a specific player is
//    the slowest, and after a configurable threshold, reduces MaxAhead
//    tolerance so the reconnect dialog appears sooner.
// =====================================================================

namespace LagTracker
{
	static constexpr int MaxPlayers = 8;

	static int ConsecutiveLagFrames[MaxPlayers] = {};
	static int LastResetFrame = 0;

	// Approximate frames-per-second for each game speed index (0=fastest, 6=slowest).
	// Derived from the engine's multimedia timer: FPS = 60 / (GameSpeed + 1).
	static int GetTargetFPS()
	{
		int speed = GameOptionsClass::Instance.GameSpeed;

		if (speed < 0)
			speed = 0;

		if (speed > 6)
			speed = 6;

		return 60 / (speed + 1);
	}

	// Lag threshold in frames, scaled by game speed.
	// BaseThreshold (seconds) * FPS = frames.
	static int GetThreshold()
	{
		return Phobos::Config::LagDetection_BaseThreshold * GetTargetFPS();
	}

	// Reset interval in frames: minutes * 60 * FPS.
	static int GetResetIntervalFrames()
	{
		return Phobos::Config::LagDetection_ResetInterval * 60 * GetTargetFPS();
	}

	static void Update(int slowestPlayer, int currentFrame)
	{
		// Periodic reset every N minutes (converted to frames)
		int resetInterval = GetResetIntervalFrames();

		if (resetInterval > 0 && (currentFrame - LastResetFrame) >= resetInterval)
		{
			for (int i = 0; i < MaxPlayers; i++)
				ConsecutiveLagFrames[i] = 0;

			LastResetFrame = currentFrame;
		}

		if (slowestPlayer >= 0 && slowestPlayer < MaxPlayers)
		{
			ConsecutiveLagFrames[slowestPlayer]++;

			// Reset counters for all other players
			for (int i = 0; i < MaxPlayers; i++)
			{
				if (i != slowestPlayer)
					ConsecutiveLagFrames[i] = 0;
			}
		}
		else
		{
			// Nobody is behind - reset all counters
			for (int i = 0; i < MaxPlayers; i++)
				ConsecutiveLagFrames[i] = 0;
		}
	}

	static bool IsLagging(int player)
	{
		if (player < 0 || player >= MaxPlayers)
			return false;

		return ConsecutiveLagFrames[player] >= GetThreshold();
	}

	static void Reset()
	{
		for (int i = 0; i < MaxPlayers; i++)
			ConsecutiveLagFrames[i] = 0;

		LastResetFrame = 0;
	}
}

// =====================================================================
// Hook 1: Majority kick vote
//
// Address: 0x649164 in sub_648710 (Wait_For_Players)
// Size: 7 bytes (covers dec edx + mov eax,[eax+esi*4] + mov ecx,[eax+6Fh])
//
// Original code:
//   649164  dec     edx                       ; threshold = playerCount - 1
//   649165  mov     eax, [eax+esi*4]          ; pNode = nodeArray[i]
//   649168  mov     ecx, [eax+6Fh]            ; playerNumber = pNode->field_6F
//
// Continues at 0x64916B:
//   64916b  mov     ecx, dword_A8BB40[ecx*4]  ; votes = voteCount[playerNumber]
//   649172  cmp     ecx, edx                   ; if votes >= threshold
//   649174  jl      short loc_6491A8           ;   skip kick
//
// We replace the threshold with ceil(playerCount / 2) and reproduce the
// data access operations, then continue at the vote lookup.
// =====================================================================
DEFINE_HOOK(0x649164, WaitForPlayers_MajorityKickVote, 0x7)
{
	// EDX = NameNodeTypes.length (total players in the game)
	// EAX = NameNodeTypes.data (pointer to array of node pointers)
	// ESI = current loop index
	GET(int, playerCount, EDX);
	GET(DWORD, nodeArrayPtr, EAX);
	GET(int, playerIdx, ESI);

	// Majority vote: minimum half of player count, rounded up
	int threshold = (playerCount + 1) / 2;

	// Reproduce stolen instructions:
	// mov eax, [eax+esi*4] - get node pointer for current player
	DWORD nodePtr = *reinterpret_cast<DWORD*>(nodeArrayPtr + static_cast<DWORD>(playerIdx) * 4);
	// mov ecx, [eax+6Fh] - get player number from node struct
	int playerNumber = *reinterpret_cast<int*>(nodePtr + 0x6F);

	R->EDX(threshold);
	R->EAX(nodePtr);
	R->ECX(playerNumber);

	// Continue at vote lookup: mov ecx, dword_A8BB40[ecx*4]
	return 0x64916B;
}

// =====================================================================
// Hook 2: Lag detection with dynamic MaxAhead reduction
//
// Address: 0x6495F9 in sub_648710 (Wait_For_Players)
// Size: 0x14 (20 bytes, covers the entire tolerance comparison block)
//
// Original code:
//   6495f9  mov     ecx, [esp+730h+var_714]   ; ECX = MaxAhead (from stack cache)
//   6495fd  mov     eax, Frame                 ; EAX = CurrentFrame
//   649602  lea     edx, [ebx+ecx]             ; EDX = lowestFrame + MaxAhead
//   649605  cmp     eax, edx                   ; compare
//   649607  jl      loc_649BAC                 ; if within tolerance -> success
//
// At this point:
//   EBX = lowestFrame (lowest game frame among all connected players)
//   EBP = slowest player index (-1 if nobody is clearly behind)
//
// We track consecutive lag frames per player and reduce MaxAhead when
// a player has been consistently the slowest for longer than the
// configurable threshold.
// =====================================================================
DEFINE_HOOK(0x6495F9, WaitForPlayers_LagDetection, 0x14)
{
	enum
	{
		WithinTolerance = 0x649BAC,  // Normal path: return 0 from sub_648710
		OutsideTolerance = 0x64960D  // Reconnect path: show dialog
	};

	GET(int, lowestFrame, EBX);
	GET(int, slowestPlayer, EBP);

	int maxAhead = Game::Network.MaxAhead;
	int currentFrame = Unsorted::CurrentFrame;

	// Update the per-player lag tracking
	LagTracker::Update(slowestPlayer, currentFrame);

	// Determine effective MaxAhead tolerance
	int effectiveMaxAhead = maxAhead;

	if (slowestPlayer >= 0 && LagTracker::IsLagging(slowestPlayer))
	{
		effectiveMaxAhead = Phobos::Config::LagDetection_ReducedMaxAhead;

		Debug::Log("[LagDetection] Player slot %d has been lagging for %d frames "
			"(threshold: %d). Reducing MaxAhead from %d to %d.\n",
			slowestPlayer,
			LagTracker::ConsecutiveLagFrames[slowestPlayer],
			LagTracker::GetThreshold(),
			maxAhead,
			effectiveMaxAhead);
	}

	if (currentFrame < lowestFrame + effectiveMaxAhead)
		return WithinTolerance;

	return OutsideTolerance;
}
