# add this to BranchPredictor.py located at gem5/src/cpu/pred

class OGEHLBP(ConditionalPredictor):
    type = 'OGEHLBP'
    cxx_class = 'gem5::branch_prediction::OGEHLBP'
    cxx_header = "cpu/pred/ogehl.hh"

    num_tables = Param.Unsigned(8, "Number of O-GEHL tables") # number of tables (table is array of counters)
    table_size = Param.Unsigned(2048, "Entries per table")    # size of tables
    counter_bits = Param.Unsigned(4, "Counter width")         # size of a single saturating counter

    theta = Param.Unsigned(8, "Initial update threshold")     # threshold
    max_theta = Param.Unsigned(31, "Maximum dynamic threshold") # set a limit for theta
    tc_bits = Param.Unsigned(7, "Threshold counter bits")       # theta only update when tc_bit reachs maximum or minimum

    global_history_bits = Param.Unsigned(200, "Global history length") 

    short_history_lengths = VectorParam.Unsigned(
        [0, 3, 5, 8, 12, 19, 31, 49],
        "Short history lengths"
    )

    long_history_lengths = VectorParam.Unsigned( #only T2, T4, T6 have longer version, same with the paper 
        [0, 0, 79, 0, 125, 0, 200, 0],
        "Long history lengths for dynamic tables"
    )

    ac_bits = Param.Unsigned(9, "Aliasing counter bits")
