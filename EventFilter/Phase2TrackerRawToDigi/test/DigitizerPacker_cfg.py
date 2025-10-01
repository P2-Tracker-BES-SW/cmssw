import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

process = cms.Process("PACKONLY")

options = VarParsing.VarParsing('analysis')
options.register('cluster', 0, options.multiplicity.singleton, options.varType.int, "Cluster ID from HTCondor")
options.register('process', 0, options.multiplicity.singleton, options.varType.int, "Process ID from HTCondor")
options.parseArguments()

GEOMETRY = "D98"

process.load('Configuration.StandardSequences.Services_cff')
process.load('Configuration.EventContent.EventContent_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('FWCore.MessageService.MessageLogger_cfi')

process.MessageLogger = cms.Service("MessageLogger",
    destinations = cms.untracked.vstring('logPacker'),
    categories = cms.untracked.vstring('ClusterToRawProducer','Phase2TrackerClusterizer'),
    debugModules  = cms.untracked.vstring('*'),
    logPacker = cms.untracked.PSet(
        threshold = cms.untracked.string('INFO'),
        INFO =  cms.untracked.PSet(limit = cms.untracked.int32(100000)),
        DEBUG = cms.untracked.PSet(limit = cms.untracked.int32(0))
    ),
)

if GEOMETRY in ("D88","D98"):
    process.load('Configuration.Geometry.GeometryExtendedRun4' + GEOMETRY + 'Reco_cff')
    process.load('Configuration.Geometry.GeometryExtendedRun4' + GEOMETRY + '_cff')
else:
    raise RuntimeError("Invalid GEOMETRY {}".format(GEOMETRY))

process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, '133X_mcRun4_realistic_v1', '')

process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(1))

process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring(
        "/store/relval/CMSSW_14_0_0_pre2/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_133X_mcRun4_realistic_v1_STD_2026D98_PU200_RV229-v1/2580000/0b2b0b0b-f312-48a8-9d46-ccbadc69bbfd.root"
    )
)

# Conditions for cabling map
process.load("CondCore.CondDB.CondDB_cfi")
process.CondDB.connect = 'frontier://FrontierProd/CMS_CONDITIONS'
process.PoolDBESSource = cms.ESSource("PoolDBESSource",
    process.CondDB,
    DumpStat = cms.untracked.bool(True),
    toGet = cms.VPSet(
        cms.PSet(
            record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
            tag = cms.string("TrackerDetToDTCELinkCablingMap__OT800_IT711__T33__OTOnly"),
        )
    ),
)
process.es_prefer_local_cabling = cms.ESPrefer("PoolDBESSource", "")

# Producers
process.ClustersFromPhase2TrackerDigis = cms.EDProducer("Phase2TrackerClusterizer",
    src = cms.InputTag("mix","Tracker"),
)

process.Packer = cms.EDProducer("ClusterToRawProducer",
    Phase2Clusters = cms.InputTag("ClustersFromPhase2TrackerDigis"),
)

# Services
from Configuration.ProcessModifiers.premix_stage2_cff import premix_stage2
premix_stage2.toModify(process.ClustersFromPhase2TrackerDigis, rawHits = ["mixData:Tracker"])

process.Timing = cms.Service("Timing",
    summaryOnly = cms.untracked.bool(True),
    useJobReport = cms.untracked.bool(True)
)

# Path: ONLY up to Packer
process.pack_path = cms.Path(process.ClustersFromPhase2TrackerDigis * process.Packer)

# Output: keep RAW so next step can unpack; do NOT keep Unpacker products here
process.out = cms.OutputModule("PoolOutputModule",
    splitLevel = cms.untracked.int32(0),
    eventAutoFlushCompressedSize = cms.untracked.int32(5242880),
    outputCommands = cms.untracked.vstring(
        'drop *',
        'keep FEDRawDataCollection_*_*_*',      # includes Packer output
        'keep *_Packer_*_*',
        'keep *_mix_Tracker_*',                 # helpful for debugging/provenance
        # optional: keep original clusters if you want to compare pre/post
        'keep *_ClustersFromPhase2TrackerDigis_*_*'
    ),
    fileName = cms.untracked.string('packed_only.root')
)

process.end = cms.EndPath(process.out)

