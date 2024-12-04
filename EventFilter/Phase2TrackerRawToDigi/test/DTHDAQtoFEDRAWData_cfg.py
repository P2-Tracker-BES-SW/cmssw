import FWCore.ParameterSet.Config as cms

process = cms.Process("FEDRAW")

# Load the necessary services (message logger for printouts, etc.)
process.load("FWCore.MessageLogger.MessageLogger_cfi")

# Set the logging output for debugging
process.MessageLogger.cerr.FwkReport.reportEvery = 1  # print every event
process.MessageLogger.cerr.threshold = 'INFO'

# Define an empty source because this is a producer that reads from a file
process.source = cms.Source("EmptySource")

# Limit the number of events processed (since you're reading from a file, 1 is fine)
process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(1)
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




