#!/bin/bash
set -euo pipefail

echo "===== START ====="
echo "Host:    $(hostname)"
echo "Date:    $(date)"
echo "Job dir: $(pwd)"

echo
echo "===== CMS ENVIRONMENT SETUP ====="
source /cvmfs/cms.cern.ch/cmsset_default.sh || { echo "ERROR: CMS env failed"; exit 1; }

cd /afs/cern.ch/user/m/mmomed/unpacker-13-05-25/CMSSW_15_0_4/src/
cmsenv

cd /afs/cern.ch/user/m/mmomed/unpacker-13-05-25/CMSSW_15_0_4/src/EventFilter/Phase2TrackerRawToDigi/test

############################################
# Nsight Systems timeline capture
############################################
#echo "===== LOADING CUDA & Nsight Systems ====="
#module load cuda//11.8        || { echo "ERROR: could not load cuda/12.4"; exit 1; }
#module load nsight-systems/2023.4.4 || { echo "ERROR: could not load nsight-systems"; exit 1; }

#echo "===== RUNNING Nsight Systems PROFILE ====="
#CUDA_VISIBLE_DEVICES=0 /opt/nvidia/nsight-systems/2024.6.2/bin/nsys profile \
 # --stats=true \
 # cmsRun /afs/cern.ch/user/m/mmomed/unpacker-13-05-25/CMSSW_15_0_4/src/EventFilter/Phase2TrackerRawToDigi/test/SLinkProducerAndUnpacker_cfg.py \
 # &> nsys.log

#echo "→ raw2cluster_nsys.qdrep generated"
#echo

############################################
# NCU timeline capture
############################################
echo "===== RUNNING Nsight compute  PROFILE ====="
#CUDA_VISIBLE_DEVICES=0 numactl -N 0 -m 0 /opt/nvidia/nsight-compute/2025.1.1/ncu --kernel-name-base demangled --nvtx --target-processes application-only \
 #   --set full -f -o profile_baseline%i cmsRun SLinkProducerAndUnpacker_alpaka_cfg.py
CUDA_VISIBLE_DEVICES=0 numactl -N 0 -m 0 /opt/nvidia/nsight-compute/2025.1.1/ncu --nvtx --target-processes=application-only --import-source on --set full -f -o profile_baseline%i cmsRun SLinkProducerAndUnpacker_cfg.py
############################################
#  perf stat for FLOPs & bandwidth
############################################

#echo "===== RUNNING perf stat FOR FLOPS & BANDWIDTH ====="
#perf stat -e \
 # fp_arith_inst_retired.scalar_double,fp_arith_inst_retired.128b_packed_double,\
#cache-misses,cache-references \
#  cmsRun ${CMSSW_SRC}/EventFilter/Phase2TrackerRawToDigi/test/raw_to_cluster_nsights_cfg.py \
#  &> perf.log

#echo "perf.log generated"
#echo

#echo "======= Getting the right CPU model in this node ====="
#lscpu 


echo "===== DONE ====="
echo "Date: $(date)"
