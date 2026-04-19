#!/bin/bash -l
#$ -l h_rt=96:00:00    # Specify the hard time limit for the job
#$ -N spec_batch       # Give job a name
#$ -pe omp 8           # Number of cores
#$ -cwd

# compiler error fix 
module load gcc/12.2.0
module load python3/3.13.8

RESULTS_DIR=results
mkdir -p $RESULTS_DIR

echo "Directory created."

# Run 1
build/X86/gem5.opt \
configs/example/gem5_library/x86-spec-cpu2017-benchmarks.py \
--image ../disk-image/spec-2017/spec-2017-image/spec-2017 \
--partition 1 \
--benchmark 505.mcf_r \
--size ref \
--branchpred OGEHLBP
		   
cp -r m5out $RESULTS_DIR/OGEHLBP

echo "Script complete."
