#include "cpu/pred/ogehl.hh"

#include <algorithm>
#include <cstdlib>

#include "base/intmath.hh"

namespace gem5
{

namespace branch_prediction
{

OGEHLBP::OGEHLBP(const OGEHLBPParams &params)
    : ConditionalPredictor(params),
      globalHistoryReg(params.numThreads,
                       std::vector<bool>(params.global_history_bits, false)),
      globalHistoryBits(params.global_history_bits),
      numTables(params.num_tables),
      tableSize(params.table_size),
      tableIndexMask(params.table_size - 1),
      indexBits(floorLog2(params.table_size)),
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

    if (globalHistoryBits == 0) {
        fatal("OGEHL global_history_bits must be > 0.\n");
    }

    // Initialize counters to weakly taken.
    unsigned mid = (1U << (ctrBits - 1));
    for (unsigned i = 0; i < numTables; ++i) {
        for (unsigned j = 0; j < tableSize; ++j) {
            for (unsigned k = 0; k < mid; ++k) {
                tables[i][j]++;
            }
        }
    }
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
    const std::vector<bool> &historyValue = globalHistoryReg[tid];
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
    useLongHistories = history->usedLongHistories;

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
        globalHistoryReg[tid] = history->globalHistoryReg;
        updateGlobalHistReg(tid, taken);
        useLongHistories = history->usedLongHistories;

        delete history;
        bp_history = nullptr;
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
    if (globalHistoryBits == 0) {
        return;
    }

    for (int i = globalHistoryBits - 1; i > 0; --i) {
        globalHistoryReg[tid][i] = globalHistoryReg[tid][i - 1];
    }
    globalHistoryReg[tid][0] = taken;
}

unsigned
OGEHLBP::computeIndex(Addr pc, const std::vector<bool> &history,
                      unsigned table) const
{
    unsigned historyLen = getHistoryLength(table);
    historyLen = std::min(historyLen, globalHistoryBits);

    unsigned idx = (pc >> instShiftAmt) & tableIndexMask;

    unsigned foldedHist = 0;
    for (unsigned i = 0; i < historyLen; ++i) {
        if (history[i]) {
            foldedHist ^= (1U << (i % indexBits));
        }
    }

    idx ^= foldedHist;
    idx &= tableIndexMask;

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

    uint8_t currentTag =
        static_cast<uint8_t>((pc >> instShiftAmt) & 0xFF);

    if (tagTable[idx] == currentTag) {
        if (ac < acMax) {
            ac++;
        }
    } else {
        ac -= 4;
        if (ac < acMin) {
            ac = acMin;
        }
    }

    if (ac == acMax) {
        useLongHistories = true;
    }

    if (ac == acMin) {
        useLongHistories = false;
    }

    tagTable[idx] = currentTag;
}

} // namespace branch_prediction
} // namespace gem5