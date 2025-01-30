The converter code is in [../plugins/DTHDAQToFEDRawDataConverter.cc](https://github.com/P2-Tracker-BES-SW/cmssw/blob/A-A-Abdelhamid/EventFilter/Phase2TrackerRawToDigi/plugins/DTHDAQToFEDRawDataConverter.cc)

To create a dummy .raw file run the python script in [dummy_raw_generator.py](https://github.com/P2-Tracker-BES-SW/cmssw/blob/A-A-Abdelhamid/EventFilter/Phase2TrackerRawToDigi/test/dummy_raw_generator.py)

[DTHDAQtoFEDRAWData_cfg.py](https://github.com/P2-Tracker-BES-SW/cmssw/blob/A-A-Abdelhamid/EventFilter/Phase2TrackerRawToDigi/test/DTHDAQtoFEDRAWData_cfg.py) is the configuration file.

To run the code, cd to the test directory then `cmsRun DTHDAQtoFEDRAWData_cfg.py`
