# O-GEHL
Implementation of the O-GEHL(Optimized Geometric History Length) branch predictor in gem5 


# O-GEHL
Implementation of the O-GEHL(Optimized Geometric History Length) branch predictor in gem5 


The original O-GEHL implementation used a 64-bit integer to store global branch history. While the configuration exposed larger history lengths (e.g., 80–200 bits), the implementation could only retain the most recent 64 branch outcomes. As a result, longer history lengths were effectively truncated, and tables configured to use long histories did not receive the intended information.

I replaced the fixed 64-bit history with a variable-length history representation using std::vector<bool>. This allows the predictor to store and utilize arbitrarily long global histories, ensuring that configured history lengths are faithfully implemented.

This change enables:

* Accurate support for long-history tables in O-GEHL
* Proper multi-scale history modeling across predictor tables
* More realistic evaluation of long-range branch correlations

Note that this implementation prioritizes correctness and flexibility over low-level optimization.