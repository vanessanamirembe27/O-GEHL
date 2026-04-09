# add this to BranchPredictor.py

class OGEHLBP(ConditionalPredictor):
    type = 'OGEHLBP'
    cxx_class = 'gem5::branch_prediction::OGEHLBP'
    cxx_header = "cpu/pred/ogehl.hh"

    num_tables = Param.Unsigned(8, "Number of O-GEHL tables")
    table_size = Param.Unsigned(2048, "Entries per table")
    counter_bits = Param.Unsigned(4, "Counter width")

    theta = Param.Unsigned(8, "Initial update threshold")
    max_theta = Param.Unsigned(31, "Maximum dynamic threshold")
    tc_bits = Param.Unsigned(7, "Threshold counter bits")

    global_history_bits = Param.Unsigned(200, "Global history length")

    short_history_lengths = VectorParam.Unsigned(
        [0, 3, 5, 8, 12, 19, 31, 49],
        "Short history lengths"
    )

    long_history_lengths = VectorParam.Unsigned(
        [0, 0, 79, 0, 125, 0, 200, 0],
        "Long history lengths for dynamic tables"
    )

    ac_bits = Param.Unsigned(9, "Aliasing counter bits")
