#ifndef __CPU_PRED_OGEHL_HH__
#define __CPU_PRED_OGEHL_HH__

#include <vector>

#include "base/sat_counter.hh"
#include "base/types.hh"
#include "cpu/pred/conditional.hh"
#include "params/OGEHLBP.hh"

namespace gem5
{

namespace branch_prediction
{

class OGEHLBP : public ConditionalPredictor
{
  public:
    struct BPHistory
    {
        std::vector<bool> globalHistoryReg;
        int outputSum;
        bool finalPred;
        bool usedLongHistories;
        std::vector<unsigned> tableIndices;
    };

    OGEHLBP(const OGEHLBPParams &params);

    bool lookup(ThreadID tid, Addr pc, void *&bp_history);

    void updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                         Addr target, const StaticInstPtr &inst,
                         void *&bp_history);

    void squash(ThreadID tid, void *&bp_history);

    void update(ThreadID tid, Addr pc, bool taken, void *&bp_history,
                bool squashed, const StaticInstPtr &inst, Addr target);

  private:
    void updateGlobalHistReg(ThreadID tid, bool taken);
    void uncondBranch(ThreadID tid, Addr pc, void *&bp_history);

    unsigned getHistoryLength(unsigned table) const;
    unsigned computeIndex(Addr pc, const std::vector<bool> &history,
                          unsigned table) const;

    void updateThreshold(bool wrong, bool weak);
    void updateHistoryMode(Addr pc, const BPHistory *history,
                           bool wrong, bool weak);

    bool isDynamicTable(unsigned table) const;

    // One arbitrary-length global history per thread.
    // Newest bit at index 0, oldest bit at index globalHistoryBits-1.
    std::vector<std::vector<bool>> globalHistoryReg;

    unsigned globalHistoryBits;

    unsigned numTables;
    unsigned tableSize;
    unsigned tableIndexMask;
    unsigned indexBits;

    unsigned ctrBits;

    unsigned theta;
    unsigned maxTheta;
    unsigned tcBits;
    int tc;

    std::vector<unsigned> shortHistoryLengths;
    std::vector<unsigned> longHistoryLengths;
    bool useLongHistories;

    unsigned acBits;
    int ac;
    std::vector<uint8_t> tagTable;

    std::vector<std::vector<SatCounter8>> tables;
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_OGEHL_HH__