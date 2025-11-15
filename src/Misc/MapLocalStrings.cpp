#include "MapLocalStrings.h"

#include <cstring>
#include <algorithm>

// Helper to allocate and copy a wide string into a new NUL-terminated buffer
static wchar_t* dup_wide(const std::wstring& s) {
    size_t len = s.size();
    auto* buf = new wchar_t[len + 1];
    std::wmemcpy(buf, s.c_str(), len);
    buf[len] = L'\0';
    return buf;
}

void MapLocalStrings::Apply(const std::map<std::string, std::wstring>& labels) {
    if (labels.empty()) {
        return;
    }

    // If already applied, clear first to reset to baseline
    if (Applied) {
        Clear();
    }

    // Snapshot baseline
    OrigLabels = StringTable::Labels;
    OrigValues = StringTable::Values;
    OrigExtraValues = StringTable::ExtraValues;
    OrigLabelCount = StringTable::LabelCount;
    OrigValueCount = StringTable::ValueCount;

    const int extra = static_cast<int>(labels.size());

    // Allocate new, larger arrays
    NewLabels = new CSFLabel[OrigLabelCount + extra]{};
    NewValues = new wchar_t*[OrigValueCount + extra]{};
    NewExtraValues = new char*[OrigValueCount + extra]{};

    // Copy original contents
    if (OrigLabelCount > 0 && OrigLabels) {
        std::memcpy(NewLabels, OrigLabels, sizeof(CSFLabel) * OrigLabelCount);
    }
    if (OrigValueCount > 0 && OrigValues) {
        std::memcpy(NewValues, OrigValues, sizeof(wchar_t*) * OrigValueCount);
    }
    if (OrigValueCount > 0 && OrigExtraValues) {
        std::memcpy(NewExtraValues, OrigExtraValues, sizeof(char*) * OrigValueCount);
    }

    // Prepare to append values at the end; maintain sorted order of labels by Name.
    int valueIdx = OrigValueCount;
    int currentLabelCount = OrigLabelCount; // labels actually used in NewLabels

    OwnedTexts.reserve(OwnedTexts.size() + labels.size());

    auto insertLabelSorted = [&](const CSFLabel& lbl) {
        // Find insertion point using linear search (label count small); fall back to end.
        int pos = 0;
        while (pos < currentLabelCount) {
            if (std::strcmp(lbl.Name, NewLabels[pos].Name) <= 0) {
                break;
            }
            ++pos;
        }
        if (pos < currentLabelCount) {
            // Shift existing labels one position to the right
            std::memmove(&NewLabels[pos + 1], &NewLabels[pos], sizeof(CSFLabel) * (currentLabelCount - pos));
            NewLabels[pos] = lbl;
        } else {
            NewLabels[currentLabelCount] = lbl;
        }
        ++currentLabelCount;
    };

    for (const auto& kv : labels) {
        CSFLabel lbl{};
        std::memset(lbl.Name, 0, sizeof(lbl.Name));
        if (!kv.first.empty()) {
            std::strncpy(lbl.Name, kv.first.c_str(), sizeof(lbl.Name) - 1);
        }
        lbl.NumValues = 1;
        lbl.FirstValueIndex = valueIdx;

        // Store value at append position
        wchar_t* w = dup_wide(kv.second);
        OwnedTexts.push_back(w);
        NewValues[valueIdx] = w;
        NewExtraValues[valueIdx] = nullptr;
        ++valueIdx;

        insertLabelSorted(lbl);
    }

    // Publish to the engine with updated counts
    StringTable::Labels = NewLabels;
    StringTable::Values = NewValues;
    StringTable::ExtraValues = NewExtraValues;
    StringTable::LabelCount = currentLabelCount;
    StringTable::ValueCount = valueIdx;

    Applied = true;
}

void MapLocalStrings::Clear() {
    if (!Applied) {
        return;
    }

    // Restore baseline pointers and counts
    StringTable::Labels = OrigLabels;
    StringTable::Values = OrigValues;
    StringTable::ExtraValues = OrigExtraValues;
    StringTable::LabelCount = OrigLabelCount;
    StringTable::ValueCount = OrigValueCount;

    // Free our allocations
    if (NewLabels) { delete[] NewLabels; NewLabels = nullptr; }
    if (NewValues) { delete[] NewValues; NewValues = nullptr; }
    if (NewExtraValues) { delete[] NewExtraValues; NewExtraValues = nullptr; }

    for (auto* p : OwnedTexts) {
        delete[] p;
    }
    OwnedTexts.clear();

    // Reset baseline snapshot
    OrigLabels = nullptr;
    OrigValues = nullptr;
    OrigExtraValues = nullptr;
    OrigLabelCount = 0;
    OrigValueCount = 0;

    Applied = false;
}
