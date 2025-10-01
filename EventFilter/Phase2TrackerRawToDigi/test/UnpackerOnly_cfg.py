import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

process = cms.Process("UNPACKONLY")

options = VarParsing.VarParsing('analysis')
options.register('input', 'packed_only.root', options.multiplicity.singleton, options.varType.string, "Input EDM from 01_pack_only_cfg.py")
options.register('dumpFED', 0, options.multiplicity.singleton, options.varType.int, "Set 1 to also run RawAnalyzer FED dump")
options.parseArguments()

GEOMETRY = "D98"

process.load('Configuration.StandardSequences.Services_cff')
process.load('Configuration.EventContent.EventContent_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('FWCore.MessageService.MessageLogger_cfi')

process.MessageLogger = cms.Service("MessageLogger",
    destinations = cms.untracked.vstring('logUnpacker'),
    categories = cms.untracked.vstring('RawToClusterProducer'),
    debugModules  = cms.untracked.vstring('*'),
    logUnpacker = cms.untracked.PSet(
        threshold = cms.untracked.string('DEBUG'),
        INFO =  cms.untracked.PSet(limit = cms.untracked.int32(0)),
        DEBUG = cms.untracked.PSet(limit = cms.untracked.int32(0)),
        RawToClusterProducer = cms.untracked.PSet(limit = cms.untracked.int32(999999999))
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

process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(-1))

# Read the RAW written by step 1. The FED product label in that file is "Packer".
process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring(options.input)
)

# Conditions for cabling map (same as step 1 to ensure identical unpacking)
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

# --- The focused stages you want to profile ---
# Optional analyzer to dump FED payloads
process.Analyzer = cms.EDAnalyzer("RawAnalyzer",
    fedRawDataCollection = cms.InputTag("Packer")   # product label as stored in the input file
)

process.Unpacker = cms.EDProducer("RawToClusterProducer",
    fedRawDataCollection = cms.InputTag("Packer")   # same: consume FED from file
)

# Services
process.Timing = cms.Service("Timing",
    summaryOnly = cms.untracked.bool(True),
    useJobReport = cms.untracked.bool(True)
)

# Path: ONLY Unpacker (+ optional Analyzer)
if options.dumpFED:
    process.unpack_path = cms.Path(process.Analyzer * process.Unpacker)
else:
    process.unpack_path = cms.Path(process.Unpacker)

# Output: keep only what this step produced/consumed
process.out = cms.OutputModule("PoolOutputModule",
    splitLevel = cms.untracked.int32(0),
    eventAutoFlushCompressedSize = cms.untracked.int32(5242880),
    outputCommands = cms.untracked.vstring(
        'drop *',
        'keep FEDRawDataCollection_*_*_*',     # in case you want it for cross-checks
        'keep *_Unpacker_*_*'                  # clusters from the unpacker
    ),
    fileName = cms.untracked.string('unpacked_only.root')
)

process.end = cms.EndPath(process.out)

# Optional: NV profiler marks (uncomment if you use Nsight)
# process.NVProfilerService = cms.Service("NVProfilerService",
#     showModulePrefetching = cms.untracked.bool(False)
# )

