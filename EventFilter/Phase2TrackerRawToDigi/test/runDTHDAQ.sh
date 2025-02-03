#!/bin/bash
# runDTHDAQ.sh
# Usage: ./runDTHDAQ.sh <raw_file>
# This script counts the events in the raw file and creates a CMSSW config file 
# that limits the number of events processed accordingly, then runs cmsRun.

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <raw_file>"
  exit 1
fi

RAWFILE="$1"

# Run the Python script to count events.
# Assumes countEvents.py prints a line like "Total events found: <number>"
EVENTNUM=$(python3 countEvents.py "$RAWFILE" | grep "Total events found:" | awk '{print $NF}')

if [ -z "$EVENTNUM" ]; then
  echo "Error: Could not determine the number of events from $RAWFILE"
  exit 1
fi

echo "Number of events found: $EVENTNUM"

# Write out the DTHDAQtoFEDRAWDATA_cfg.py file with the computed event count.
cat > DTHDAQtoFEDRAWDATA_cfg.py <<EOF
# CMS job to run the DTHDAQToFEDRawDataConverter module
# It converts .raw files containing orbits bitstream raw data from the TIF to FEDRawDataCollection
import FWCore.ParameterSet.Config as cms

process = cms.Process("FEDRAW")

process.load("FWCore.MessageLogger.MessageLogger_cfi")

# Set the logging output for debugging
# process.MessageLogger.cerr.FwkReport.reportEvery = 1  # print every event
# process.MessageLogger.cerr.threshold = 'INFO'

process.MessageLogger = cms.Service(
    "MessageLogger",
    destinations=cms.untracked.vstring('logFile', 'cout'),
    logFile=cms.untracked.PSet(
        threshold=cms.untracked.string('INFO'),  # Change to 'DEBUG' if needed
    ),
    cout=cms.untracked.PSet(
        threshold=cms.untracked.string('WARNING'),  # Only show warnings/errors on console
    ),
    categories=cms.untracked.vstring('DTHDAQToFEDRawDataConverter')
)

# Define an empty source because this is a producer that reads from a file
process.source = cms.Source("EmptySource")

# Limit the number of events processed based on the raw file content
process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(${EVENTNUM})
)

# Define the DTHDAQToFEDRawDataConverter module
process.dthDAQToFEDRawData = cms.EDProducer('DTHDAQToFEDRawDataConverter',
    inputFile = cms.string('_4orbit_data.raw'),  # Path to your input raw file
    fedId = cms.uint32(1234)  # Example FED ID, adjust as necessary
)

# Define the output module to write FEDRawData to a ROOT file
process.output = cms.OutputModule("PoolOutputModule",
    fileName = cms.untracked.string("outputFEDRawData.root"),  # Output ROOT file
    outputCommands = cms.untracked.vstring('keep *')  # Keep everything for now
)

# Define the path to run the producer
process.p = cms.Path(process.dthDAQToFEDRawData)

# Define the end path to write the output to the ROOT file
process.e = cms.EndPath(process.output)
EOF

echo "Configuration file DTHDAQtoFEDRAWDATA_cfg.py created."

# Run the configuration using cmsRun
cmsRun DTHDAQtoFEDRAWDATA_cfg.py
