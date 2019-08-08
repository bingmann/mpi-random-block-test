#!/bin/bash
#SBATCH --partition=normal
#SBATCH --nodes=8
#SBATCH --ntasks-per-node=1
#SBATCH --time=00:20:00
#SBATCH --mem=60gb

EXECUTABLE="$HOME/mpi-random-block-test/build/random_block 1 32768 1 128 4096"

module load mpi/openmpi/3.1

MPIRUN_OPTIONS=
#MPIRUN_OPTIONS="$MPIRUN_OPTIONS -mca btl_openib_allow_ib true"
MPIRUN_OPTIONS="$MPIRUN_OPTIONS -report-bindings"

NUM_CORES=${SLURM_NTASKS}*${SLURM_CPUS_PER_TASK}
date
echo "${EXECUTABLE} running on ${NUM_CORES} cores with ${SLURM_NTASKS} MPI-tasks and ${OMP_NUM_THREADS} threads"

startexe="mpirun ${MPIRUN_OPTIONS} ${EXECUTABLE}"
echo $startexe
exec $startexe
