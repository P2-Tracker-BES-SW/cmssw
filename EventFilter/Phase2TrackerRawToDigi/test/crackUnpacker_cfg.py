## cfg file to run the packing and unpacking steps for Phase2 OT clusters
## optionally, also run EDAnalyzer to dump the FEDRawData into a text file
## outputs an EDM file containing the original FEDRawData and the unpacked clusters

import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing
import FWCore.Utilities.FileUtils as FileUtils
import os

process = cms.Process("UNPACK")

process.load('Configuration.StandardSequences.Services_cff')
process.load('Configuration.EventContent.EventContent_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')

process.MessageLogger = cms.Service("MessageLogger",
    destinations = cms.untracked.vstring('logUnpacker','cout'),
    categories = cms.untracked.vstring('RawToClusterProducer'),
    debugModules  = cms.untracked.vstring('*'),
    cout = cms.untracked.PSet( # Writes event number also to cout.
        threshold = cms.untracked.string("INFO"),
        INFO = cms.untracked.PSet(limit = cms.untracked.int32(0)),
    ),                                     
    logUnpacker = cms.untracked.PSet( # Write output to file
        enableStatistics = cms.untracked.bool(True),     
        threshold = cms.untracked.string('DEBUG'),
        INFO =  cms.untracked.PSet(limit = cms.untracked.int32(9999999)),
        DEBUG = cms.untracked.PSet(limit = cms.untracked.int32(9999999)),
        RawToClusterProducer = cms.untracked.PSet(limit = cms.untracked.int32(-1))
    ),
)

process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.MagneticField_0T_cff')

## customise for C-rack geometry
process.load('Configuration.Geometry.GeometryExtendedRun4D500Reco_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic_0T', '')

## sara: not sure it's needed here
process.trackerGeometry.applyAlignment = False

## to read local cabling map you need to use the following configuration lines: 
process.load("CondCore.CondDB.CondDB_cfi")
process.CondDB.connect = 'sqlite_file:/afs/cern.ch/work/f/fiorendi/private/l1tt/unpacker/crack/again/CMSSW_16_0_0_pre4/src/Geometry/TrackerCommonData/data/CRack_PhaseII/crackCablingMap.db'
process.PoolDBESSource = cms.ESSource("PoolDBESSource",
    process.CondDB,
    toGet = cms.VPSet(cms.PSet(
        record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
        tag = cms.string("DTCCablingMapProducerUserRun"))
    )
)
process.es_prefer_local_TrackerDetToDTCELinkCablingMapRcd = cms.ESPrefer("PoolDBESSource","")



process.source = cms.Source("PoolSource",
#      fileNames = cms.untracked.vstring("/store/relval/CMSSW_15_1_0_pre5/RelValDoubleMuFlatPt1p5To8/GEN-SIM-DIGI-RAW/150X_mcRun4_realistic_v1_RV269_Run4D110_noPU-v1/2590000/1172421f-823f-420f-8ec9-3de20dd6dda4.root")
     fileNames = cms.untracked.vstring(
       "file:/afs/cern.ch/work/f/fiorendi/private/l1tt/unpacker/crack/CMSSW_15_0_4/src/EventFilter/Utilities/test/output_dataset.root"
     )
)
process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(1))


# algo = process.mix.digitizers.pixel.SSDigitizerAlgorithm
# if hasattr(algo, "LorentzAngle_DB"):
#         algo.LorentzAngle_DB = cms.bool(False)
# if hasattr(algo, "TanLorentzAnglePerTesla_Barrel"):
#         algo.TanLorentzAnglePerTesla_Barrel = cms.double(0.0)
# if hasattr(algo, "TanLorentzAnglePerTesla_Endcap"):
#         algo.TanLorentzAnglePerTesla_Endcap = cms.double(0.0)


process.Unpacker = cms.EDProducer("RawToClusterProducer",
    fedRawDataCollection = cms.InputTag("rawDataCollector")
)

process.out = cms.OutputModule("PoolOutputModule",
    splitLevel = cms.untracked.int32(0),
    eventAutoFlushCompressedSize = cms.untracked.int32(5242880),                              
    outputCommands = cms.untracked.vstring('drop *',
      'keep FEDRawDataCollection_*_*_*',
      'keep Phase2TrackerCluster1D*_*_*_*',
      'keep *_remadeSiPhase2Clusters_*_*',
      'keep *_Unpacker_*_*',
      'keep *_mix_Tracker_*',
      ),
    fileName = cms.untracked.string('crackClusters.root')
)

process.Timing = cms.Service("Timing",
    summaryOnly = cms.untracked.bool(True),  # If true, only the summary is printed.
    useJobReport = cms.untracked.bool(True)  # This will also log timings in the job report.
)

process.dtc = cms.Path(process.Unpacker)
process.output = cms.EndPath(process.out)
