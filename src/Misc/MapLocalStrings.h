#pragma once

#include <StringTable.h>
#include <vector>
#include <string>
#include <map>

// Injects and rolls back per-map CSF labels into the game's StringTable.
class MapLocalStrings {
public:
    // Apply a set of labels -> wide strings into StringTable.
    static void Apply(const std::map<std::string, std::wstring>& labels);

    // Revert StringTable to its original arrays/counts and free our allocations.
    static void Clear();

private:
    static inline bool Applied = false;

    // Baseline snapshot (owned by the game).
    static inline CSFLabel* OrigLabels = nullptr;
    static inline wchar_t** OrigValues = nullptr;
    static inline char** OrigExtraValues = nullptr;
    static inline int OrigLabelCount = 0;
    static inline int OrigValueCount = 0;

    // Our substituted arrays when applied.
    static inline CSFLabel* NewLabels = nullptr;
    static inline wchar_t** NewValues = nullptr;
    static inline char** NewExtraValues = nullptr;

    // Own the memory of inserted wide strings.
    static inline std::vector<wchar_t*> OwnedTexts;
};
