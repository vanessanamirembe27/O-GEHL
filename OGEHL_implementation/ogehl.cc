//This file should be created at gem5/src/cpu/pred

/*
 * O-GEHL Branch Predictor
 *
 * Overview
 * --------
 * O-GEHL is an improved version of GEHL (Geometric History Length).
 * The base GEHL predictor uses multiple prediction tables, where each
 * table is indexed with a different global history length. These history
 * lengths grow geometrically, so some tables focus on very recent branch
 * behavior while others capture long-range correlations.
 *
 * In GEHL:
 *  - Each table stores saturating counters.
 *  - For a branch lookup, one counter is read from each table.
 *  - The counters are converted to signed values and summed.
 *  - If the total sum is >= 0, the predictor outputs taken.
 *    Otherwise it outputs not taken.
 *  - The predictor updates its counters only when:
 *      1) the prediction was wrong, or
 *      2) the prediction was weak (|sum| < theta).
 *
 * O-GEHL Improvements
 * -------------------
 * O-GEHL keeps the GEHL core, but adds two adaptive mechanisms:
 *
 * 1. Dynamic Threshold Fitting
 * ----------------------------
 * In plain GEHL, theta is fixed. Theta controls when the predictor should
 * update on a "weak" prediction.
 *
 * In O-GEHL, theta is adjusted at runtime:
 *  - if many wrong predictions happen, theta is increased 
 *  - if many correct-but-weak predictions happen, theta is decreased
 *
 * 2. Dynamic History-Length Fitting
 * ---------------------------------
 * In plain GEHL, each table always uses one fixed history length.
 *
 * In O-GEHL, some tables can switch between:
 *  - a short history length
 *  - a long history length
 *
 * This is controlled by an aliasing counter (AC).
 * The idea is:
 *  - if long histories appear useful, enable long-history mode
 *  - if long histories create too much aliasing/conflict, switch back
 *    to short-history mode
 *
 * A small tag table is used to estimate whether updates are coming from
 * the same branch/path pattern or from conflicting ones. That information
 * drives the aliasing counter.
 */

#include "cpu/pred/ogehl.hh"

#include <algorithm>
#include <cstdlib>

#include "base/bitfield.hh"
#include "base/intmath.hh"

namespace gem5
{

namespace branch_prediction
{

OGEHLBP::OGEHLBP(const OGEHLBPParams &params)    //Constructor: initialize parameters
    : ConditionalPredictor(params),
      globalHistoryReg(params.numThreads, 0),    //register that stores actual branch outcomes
      globalHistoryBits(params.global_history_bits),    //size of global history, i took 200
      numTables(params.num_tables),              //# of tables in GEHL, I took 8
      tableSize(params.table_size),              //size of table (or size of array of counters), I took 2048             
      ctrBits(params.counter_bits),              //number of bits of a counter
      theta(params.theta),                       //threshold theta, initialized with 12
      maxTheta(params.max_theta),                //maximum value for theta, 31
      tcBits(params.tc_bits),                    //control how often theta changes. I took 7 bits so that it ranges from [-64, 63]
      tc(0),
      shortHistoryLengths(params.short_history_lengths),    //[0, 3, 5, 8, 12, 19, 31, 49], roughly geometric growth. 
      longHistoryLengths(params.long_history_lengths),      //[0, 0, 79, 0, 125, 0, 200, 0] T2, T4, T6 have long history length option so they are non zero, others are fixed so length = 0. 
      useLongHistories(false),                    //long history switch
      acBits(params.ac_bits),                    //Aliasing counter, I took 9 bits. 
      ac(0),
      tagTable(tableSize, 0),
      tables(numTables,
             std::vector<SatCounter8>(tableSize, SatCounter8(ctrBits)))
{
    if (!isPowerOf2(tableSize)) {
        fatal("OGEHL table size must be a power of 2.\n"); //make sure table size is power of 2
    }

    if (shortHistoryLengths.size() != numTables) {
        fatal("OGEHL short_history_lengths size must equal num_tables.\n"); //make sure history length is equal to num tables
    }

    if (longHistoryLengths.size() != numTables) {
        fatal("OGEHL long_history_lengths size must equal num_tables.\n"); //same as above for long history length
    }

    historyRegisterMask = mask(globalHistoryBits);                         //mask for history length
    tableIndexMask = tableSize - 1;

    unsigned mid = (1 << (ctrBits - 1));                                   //initialize counters to 0
    for (unsigned i = 0; i < numTables; ++i) {
        for (unsigned j = 0; j < tableSize; ++j) {
            for (unsigned k = 0; k < mid; ++k) {
                tables[i][j]++;
            }
        }
    }
}

bool //switch for tables with dynamic length (same as paper written, T2, T4 and T6)
OGEHLBP::isDynamicTable(unsigned table) const 
{
    return (table == 2 || table == 4 || table == 6);
}

unsigned //return history length used by table depending on mode (long or short). 
OGEHLBP::getHistoryLength(unsigned table) const
{
    if (isDynamicTable(table) && useLongHistories &&
        longHistoryLengths[table] > 0) {
        return longHistoryLengths[table];
    }

    return shortHistoryLengths[table];
}

void //handle unconditional branch -> always taken
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

void //update global history speculatively after prediction
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

bool //Main prediction logic
OGEHLBP::lookup(ThreadID tid, Addr branchAddr, void *&bp_history)
{
    uint64_t historyValue = globalHistoryReg[tid];
    int sum = 0;

    BPHistory *history = new BPHistory;
    history->globalHistoryReg = historyValue;
    history->usedLongHistories = useLongHistories;
    history->tableIndices.resize(numTables);
    
    //summation of all tables
    for (unsigned i = 0; i < numTables; ++i) { 
        unsigned idx = computeIndex(branchAddr, historyValue, i);
        history->tableIndices[i] = idx;

        int raw = static_cast<int>(tables[i][idx]);
        int signedVal = raw - (1 << (ctrBits - 1));
        sum += signedVal;
    }

    history->outputSum = sum;
    history->finalPred = (sum >= 0); //taken if S >= 0
    bp_history = static_cast<void *>(history);

    return history->finalPred;
}

void //Restore history on misprediction
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

void //update predictor after branch resolves
OGEHLBP::update(ThreadID tid, Addr branchAddr, bool taken,
                void *&bp_history, bool squashed,
                const StaticInstPtr &inst, Addr target)
{
    if (bp_history == nullptr) {
        return;
    }

    BPHistory *history = static_cast<BPHistory *>(bp_history);

    //if squashed, just restore correct history 
    if (squashed) { 
        globalHistoryReg[tid] =
            ((history->globalHistoryReg << 1) |
             (taken ? 1 : 0)) & historyRegisterMask;
        return;
    }

    //safety check
    if (history->tableIndices.size() != numTables) {
        delete history;
        bp_history = nullptr;
        return;
    }
    
    bool wrong = (history->finalPred != taken);
    bool weak = (std::abs(history->outputSum) < static_cast<int>(theta)); //weak = S < theta

    //update tables only when predcition wrong *or* S < theta
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
    //update dynamic features (adaptive theta and long/short mode), this is what distinguishes O-GEHL and GEHL 
    updateThreshold(wrong, weak);
    updateHistoryMode(branchAddr, history, wrong, weak);

    delete history;
    bp_history = nullptr;
}

void //shift new branch outcome into global history 
OGEHLBP::updateGlobalHistReg(ThreadID tid, bool taken)
{
    globalHistoryReg[tid] =
        ((globalHistoryReg[tid] << 1) | //global history register is a shift register
         (taken ? 1 : 0)) & historyRegisterMask; 
}

unsigned // compute index = (PC ^ history) & table size (mask)
OGEHLBP::computeIndex(Addr pc, uint64_t history, unsigned table) const
{
    unsigned historyLen;

    if (isDynamicTable(table) && longHistoryLengths[table] > 0) {
        historyLen = useLongHistories ?
                     longHistoryLengths[table] :
                     shortHistoryLengths[table];
    } else {
        historyLen = shortHistoryLengths[table];
    }

    uint64_t histMask;
    if (historyLen >= 64) {
        histMask = ~0ULL;
    } else if (historyLen == 0) {
        histMask = 0;
    } else {
        histMask = (1ULL << historyLen) - 1;
    }

    uint64_t truncatedHistory = history & histMask;

    unsigned idx =
        ((pc >> instShiftAmt) ^ truncatedHistory) & tableIndexMask;

    return idx;
}

//dynamic theta update ( history is being updated when S < theta, so it update more often when theta is large and vice versa)
//the goal of this part is to let update_due_to_miss / update_due_to_threshold ~=1
void 
OGEHLBP::updateThreshold(bool wrong, bool weak)
{
    int tcMax = (1 << (tcBits - 1)) - 1;
    int tcMin = -(1 << (tcBits - 1));

    if (wrong) {          //when mispredict -> increment tc so that it update more frequent
        if (tc < tcMax) {
            tc++;
        }

        if (tc == tcMax) { //only update theta when theta counter reach maximum
            if (theta < maxTheta) {
                theta++;
            }
            tc = 0; 
        }
    }

    if (!wrong && weak) { //when prediction correct but low confidence -> decrease theta so it update less frequent
        if (tc > tcMin) {
            tc--;
        }

        if (tc == tcMin) { //same for decrement theta
            if (theta > 0) {
                theta--;
            }
            tc = 0;
        }
    }
}


void //dynamic history length switching
OGEHLBP::updateHistoryMode(Addr pc, const OGEHLBP::BPHistory *history,
                           bool wrong, bool weak)
{
    if (!(wrong || weak)) {
        return;
    }
    //use longest table as reference since longest table aliasing more
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

    uint8_t currentTag = static_cast<uint8_t>(pc & 1);

    if (tagTable[idx] == currentTag) { //if aliasing not happened, increment ac so that prefer to long history length
        if (ac < acMax) {
            ac++;
        }
    } else {    //if aliasing happened, decrement ac by 4 to prefer short history length
        ac -= 4;
        if (ac < acMin) {
            ac = acMin;
        }
    }

    if (ac == acMax) { //update switch when ac reach max
        useLongHistories = true;
    }

    if (ac == acMin) { //same for min
        useLongHistories = false;
    }

    tagTable[idx] = currentTag;
}

} // namespace branch_prediction
} // namespace gem5
