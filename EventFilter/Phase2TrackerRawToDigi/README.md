=== Instructions to compile & run outer tracker packer/unpacker (alpaka based) === This converts between RAW & Cluster EDProducts in CMSSW, where the RAW data is represented by the FEDRawDataCollection EDProduct.
```
cmsrel CMSSW_15_0_4
cd CMSSW_15_0_4/src
cmsenv
git cms-checkout-topic -u P2-Tracker-BES-SW:alpaka_Phase2_OT_Unpacker
scram b -j
cd EventFilter/Phase2TrackerRawToDigi/test/
# Run cluster --> RAW --> cluster sequence for Outer Tracker.
cmsRun SLinkProducerAndUnpacker_cfg.py
```
The product will give you raw2clusters.root file with an SoA branch and a Phase2TrackerCluster1D object (only if the flag Legacy_Format is set to True in the SLinkProducerAndUnpacker_cfg.py which is by default) In case you want to validate the workflow with a job in HT-condor environemnt:
```
cd CMSSW_15_0_4/src/EventFilter/Phase2TrackerRawToDigi/test/
condor_submit validation.sub
```
The result will give you a folder validation_plots where you can find the of round operation packing plus unpacking(reclustered) matched with the original input. Or alternatively feel free to check one of the recent test made at: (plots)[https://cernbox.cern.ch/files/spaces/eos/user/m/mmomed/2025-09-25_14-56-47_dtc208?items-per-page=100&view-mode=resource-table&tiles-size=1]
