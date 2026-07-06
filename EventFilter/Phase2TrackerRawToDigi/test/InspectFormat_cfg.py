import FWCore.ParameterSet.Config as cms
process = cms.Process("FEDRAW")
process.load("FWCore.MessageLogger.MessageLogger_cfi")

process.MessageLogger = cms.Service(
    "MessageLogger",
    destinations=cms.untracked.vstring('logFile', 'cout'),
    logFile=cms.untracked.PSet(
        threshold=cms.untracked.string('INFO'),
    ),
    cout=cms.untracked.PSet(
        threshold=cms.untracked.string('INFO'),  # Changed from 'WARNING' to 'INFO'
    ),
    categories=cms.untracked.vstring(
        'FormatInspector'  # Add your analyzer category
    )
)

process.source = cms.Source("EmptySource")

process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(1)
)

process.dthDAQToFEDRawData = cms.EDProducer('DTHDAQToFEDRawDataConverter',
    inputFile = cms.string('/home/hep/am2023/raw_data_buffer/CMSSW_16_0_8/src/DAQ_FMT_v1_0_Noise_Test_1_Ladder.raw'), 
)

process.formatInspector = cms.EDAnalyzer('FormatInspector',
    fedRawDataCollectionTag = cms.InputTag('dthDAQToFEDRawData')
)

process.p = cms.Path(
    process.dthDAQToFEDRawData +
    process.formatInspector
)