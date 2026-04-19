//This file should be created at gem5/src/cpu/pred
//I will include comments soon --Zhongkun

#include "cpu/pred/ogehl.hh"

#include <algorithm>
#include <cstdlib>

#include "base/bitfield.hh"
#include "base/intmath.hh"

namespace gem5
{

namespace branch_prediction
{

OGEHLBP::OGEHLBP(const OGEHLBPParams &params)
    : ConditionalPredictor(params),
      globalHistoryReg(params.numThreads, 0),
      globalHistoryBits(params.global_history_bits),
      numTables(params.num_tables),
      tableSize(params.table_size),
      ctrBits(params.counter_bits),
      theta(params.theta),
      maxTheta(params.max_theta),
      tcBits(params.tc_bits),
      tc(0),
      shortHistoryLengths(params.short_history_lengths),
      longHistoryLengths(params.long_history_lengths),
      useLongHistories(false),
      acBits(params.ac_bits),
      ac(0),
      tagTable(tableSize, 0),
      tables(numTables,
             std::vector<SatCounter8>(tableSize, SatCounter8(ctrBits)))
{
    if (!isPowerOf2(tableSize)) {
        fatal("OGEHL table size must be a power of 2.\n");
    }

    if (shortHistoryLengths.size() != numTables) {
        fatal("OGEHL short_history_lengths size must equal num_tables.\n");
    }

    if (longHistoryLengths.size() != numTables) {
        fatal("OGEHL long_history_lengths size must equal num_tables.\n");
    }

    historyRegisterMask = mask(globalHistoryBits);
    tableIndexMask = tableSize - 1;

    unsigned mid = (1 << (ctrBits - 1));
    for (unsigned i = 0; i < numTables; ++i) {
        for (unsigned j = 0; j < tableSize; ++j) {
            for (unsigned k = 0; k < mid; ++k) {
                tables[i][j]++;
            }
        }
    }
}

void
OGEHLBP::regStats()
{
    // ALWAYS call the parent class regStats first so it prints the default misprediction stats!
    ConditionalPredictor::regStats();

    thetaIncreases
        .name(name() + ".thetaIncreases")
        .desc("Number of times the dynamic threshold (theta) was increased");

    thetaDecreases
        .name(name() + ".thetaDecreases")
        .desc("Number of times the dynamic threshold (theta) was decreased");

    longHistoryUsed
        .name(name() + ".longHistoryUsed")
        .desc("Number of branch lookups that utilized long history mode");

    shortHistoryUsed
        .name(name() + ".shortHistoryUsed")
        .desc("Number of branch lookups that utilized short history mode");

    aliasingDetected
        .name(name() + ".aliasingDetected")
        .desc("Number of times aliasing was detected in the tag table");
}

bool
OGEHLBP::isDynamicTable(unsigned table) const
{
    return (table == 2 || table == 4 || table == 6);
}

unsigned
OGEHLBP::getHistoryLength(unsigned table) const
{
    if (isDynamicTable(table) && useLongHistories &&
        longHistoryLengths[table] > 0) {
        return longHistoryLengths[table];
    }

    return shortHistoryLengths[table];
}

void
OGEHLBP::uncondBranch(ThreadID tid, Addr pc, void *&bp_history)
{
    BPHistory *history = new BPHistory;

    history->globalHistoryReg = globalHistoryReg[tid];
    history->outputSum = 0;
    history->finalPred = true;
    history->usedLongHistories = useLongHistories;
    history->tableIndices.resize(numTables, 0);

    bp_history = static_cast<void *>(history);
}

void
OGEHLBP::updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                         Addr target, const StaticInstPtr &inst,
                         void *&bp_history)
{
    assert(uncond || bp_history);

    if (uncond) {
        uncondBranch(tid, pc, bp_history);
    }

    updateGlobalHistReg(tid, taken);
}

bool
OGEHLBP::lookup(ThreadID tid, Addr branchAddr, void *&bp_history)
{
    uint64_t historyValue = globalHistoryReg[tid];
    int sum = 0;

    BPHistory *history = new BPHistory;
    history->globalHistoryReg = historyValue;
    history->usedLongHistories = useLongHistories;
    history->tableIndices.resize(numTables);

    for (unsigned i = 0; i < numTables; ++i) {
        unsigned idx = computeIndex(branchAddr, historyValue, i);
        history->tableIndices[i] = idx;

        int raw = static_cast<int>(tables[i][idx]);
        int signedVal = raw - (1 << (ctrBits - 1));
        sum += signedVal;
    }

    history->outputSum = sum;
    history->finalPred = (sum >= 0);
    bp_history = static_cast<void *>(history);

    // --- NEW STAT TRACKING ---
    if (useLongHistories) {
        longHistoryUsed++;
    } else {
        shortHistoryUsed++;
    }

    return history->finalPred;
}

void
OGEHLBP::squash(ThreadID tid, void *&bp_history)
{
    if (bp_history == nullptr) {
        return;
    }

    BPHistory *history = static_cast<BPHistory *>(bp_history);
    globalHistoryReg[tid] = history->globalHistoryReg;

    delete history;
    bp_history = nullptr;
}

void
OGEHLBP::update(ThreadID tid, Addr branchAddr, bool taken,
                void *&bp_history, bool squashed,
                const StaticInstPtr &inst, Addr target)
{
    if (bp_history == nullptr) {
        return;
    }

    BPHistory *history = static_cast<BPHistory *>(bp_history);

    if (squashed) {
        globalHistoryReg[tid] =
            ((history->globalHistoryReg << 1) |
             (taken ? 1 : 0)) & historyRegisterMask;
        return;
    }

    if (history->tableIndices.size() != numTables) {
        delete history;
        bp_history = nullptr;
        return;
    }

    bool wrong = (history->finalPred != taken);
    bool weak = (std::abs(history->outputSum) < static_cast<int>(theta));

    if (wrong || weak) {
        for (unsigned i = 0; i < numTables; ++i) {
            unsigned idx = history->tableIndices[i];

            if (idx >= tableSize) {
                continue;
            }

            if (taken) {
                tables[i][idx]++;
            } else {
                tables[i][idx]--;
            }
        }
    }

    updateThreshold(wrong, weak);
    updateHistoryMode(branchAddr, history, wrong, weak);

    delete history;
    bp_history = nullptr;
}

void
OGEHLBP::updateGlobalHistReg(ThreadID tid, bool taken)
{
    globalHistoryReg[tid] =
        ((globalHistoryReg[tid] << 1) |
         (taken ? 1 : 0)) & historyRegisterMask;
}

unsigned // compute index with history folding
OGEHLBP::computeIndex(Addr pc, uint64_t history, unsigned table) const
{
    unsigned historyLen;

    // Determind how much history this specific table is allowed to look at
    if (isDynamicTable(table) && longHistoryLengths[table] > 0) {
        historyLen = useLongHistories ?
                     longHistoryLengths[table] :
                     shortHistoryLengths[table];
    } else {
        historyLen = shortHistoryLengths[table];
    }

    // Create a mask to grab only the requested history length
    uint64_t histMask;
    if (historyLen >= 64) {
        histMask = ~0ULL;
    } else if (historyLen == 0) {
        histMask = 0;
    } else {
        histMask = (1ULL << historyLen) - 1;
    }

    uint64_t truncatedHistory = history & histMask;

    // Folding Logic
    uint64_t foldedHistory = 0;

    unsigned indexBits = floorLog2(tableSize);

    while (truncatedHistory > 0) {
      foldedHistory ^= (truncatedHistory & tableIndexMask);
      truncatedHistory >>= indexBits; 
    }
    
    unsigned idx = ((pc >> instShiftAmt) ^ foldedHistory) & tableIndexMask;

    return idx;
}

void
OGEHLBP::updateThreshold(bool wrong, bool weak)
{
    int tcMax = (1 << (tcBits - 1)) - 1;
    int tcMin = -(1 << (tcBits - 1));

    if (wrong) {
        if (tc < tcMax) {
            tc++;
        }

        if (tc == tcMax) {
            if (theta < maxTheta) {
                theta++;
		thetaIncreases++; 
            }
            tc = 0;
        }
    }

    if (!wrong && weak) {
        if (tc > tcMin) {
            tc--;
        }

        if (tc == tcMin) {
            if (theta > 0) {
                theta--;
		thetaDecreases++;
            }
            tc = 0;
        }
    }
}

void 
OGEHLBP::updateHistoryMode(Addr pc, const OGEHLBP::BPHistory *history,
                           bool wrong, bool weak)
{
    if (!(wrong || weak)) {
        return;
    }
    // use longest table as reference since longest table aliasing more
    unsigned refTable = numTables - 1;
    if (refTable >= history->tableIndices.size()) {
        return;
    }

    unsigned idx = history->tableIndices[refTable];
    if (idx >= tagTable.size()) {
        return;
    }

    int acMax = (1 << (acBits - 1)) - 1;
    int acMin = -(1 << (acBits - 1));

    // --- NEW TAG HASHING LOGIC ---
    // Shift the PC down to drop the empty alignment bits, then XOR it 
    // with the global history to create a unique branch fingerprint.
    // Finally, mask it with 0xFF to grab exactly 8 bits for our uint8_t tag.
    uint8_t currentTag = static_cast<uint8_t>(((pc >> instShiftAmt) ^ history->globalHistoryReg) & 0xFF);
    // -----------------------------

    if (tagTable[idx] == currentTag) { 
        if (ac < acMax) {
            ac++;
        }
    } else {    // if aliasing happened, decrement ac by 4 to prefer short history length
        ac -= 4;
        aliasingDetected++; 
        if (ac < acMin) {
            ac = acMin;
        }
    }

    if (ac == acMax) { // update switch when ac reach max
        useLongHistories = true;
    }

    if (ac == acMin) { // same for min
        useLongHistories = false;
    }

    tagTable[idx] = currentTag;
}

} // namespace branch_prediction
} // namespace gem5
