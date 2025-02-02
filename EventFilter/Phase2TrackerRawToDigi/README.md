=== Compile & Run Instructions ===

```
cmsrel CMSSW_15_0_0_pre2
cd CMSSW_15_0_0_pre2/src
cmsenv
git cms-checkout-topic -u P2-Tracker-BES-SW:unpackers_15_0_0_pre2
scram b -j
cd EventFilter/Phase2TrackerRawToDigi/test/
# Run cluster --> RAW --> cluster sequence for Outer Tracker.
cmsRun SLinkProducerAndUnpacker_cfg.py*
```

=== Rebase Instructions ===

If you have a personal branch of this code, and wish to update it with changes made by others to tomalin:masterP2TrackerUnpackers , then in new project area:

```
git cms-checkout-topic -u P2-Tracker-BES-SW:unpackers_15_0_0_pre2
git cms-rebase-topic -u myFork:myBranch
```


