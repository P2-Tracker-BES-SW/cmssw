#!/bin/bash
source  /cvmfs/cms.cern.ch/cmsset_default.sh
eval `scram runtime -sh`

## Run the packer-unpacker steps
filename='/store/relval/CMSSW_14_0_0_pre2/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_133X_mcRun4_realistic_v1_STD_2026D98_PU200_RV229-v1/2580000/0b2b0b0b-f312-48a8-9d46-ccbadc69bbfd.root'
# cmsRun SLinkProducerAndUnpacker_cfg.py inputFiles_clear inputFiles=$filename 

## Run the EDanalyzer on the original cluster collection
output_original='clusterNtuple_original_TTBar.root'
# cmsRun Phase2TrackerDumpClusters_custom_cfg.py inputFiles_clear inputFiles=$filename outputFile=$output_original onUnpacked=false

## Run the EDanalyzer on the unpacked cluster collection
output_recluster='clusterNtuple_reclustered_TTBar.root'
filename='file:raw2clusters.root'
# cmsRun Phase2TrackerDumpClusters_custom_cfg.py inputFiles_clear inputFiles=$filename outputFile=$output_recluster onUnpacked=true

if [ ! -d "validation_plots" ]; then
  mkdir -p "validation_plots"
  echo "Folder 'validation_plots' created."
fi

## Run the validation script, by selecting only clusters from a specific DTC as an example
python3 compareClusters.py $output_original $output_recluster --dtcid 208