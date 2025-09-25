## cfg file to run the packing and unpacking steps for Phase2 OT clusters
## optionally, also run EDAnalyzer to dump the FEDRawData into a text file
## outputs an EDM file containing the original FEDRawData and the unpacked clusters

import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing
import FWCore.Utilities.FileUtils as FileUtils
import os

# flag for using the conversion
Legacy_Format = True

process = cms.Process("PACKANDUNPACK")
process.options.numberOfThreads = 8
process.options.numberOfStreams = 8
def get_input_mc_line(dataset_database, line_number):
    with open(dataset_database, 'r') as file:
        lines = file.readlines()
        if line_number < 0 or line_number >= len(lines):
            raise IndexError("Line number out of range")
        return lines[line_number].strip()

options = VarParsing.VarParsing('analysis')

# Add custom command-line arguments
options.register('cluster',
                 0, # default value
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 "Cluster ID from HTCondor")

options.register('process',
                 0, # default value
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 "Process ID from HTCondor")

# Parse command-line arguments
options.parseArguments()

GEOMETRY = "D98"

process.load('Configuration.StandardSequences.Services_cff')
process.load('Configuration.StandardSequences.Accelerators_cff')
process.load('HeterogeneousCore.AlpakaCore.ProcessAcceleratorAlpaka_cfi')
process.load('HeterogeneousCore.CUDACore.ProcessAcceleratorCUDA_cfi')
process.load('Configuration.EventContent.EventContent_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')

if GEOMETRY == "D88" or GEOMETRY == 'D98':
    process.load('Configuration.Geometry.GeometryExtendedRun4' + GEOMETRY + 'Reco_cff')
    process.load('Configuration.Geometry.GeometryExtendedRun4' + GEOMETRY + '_cff')
else:
    print("this is not a valid geometry!!!")

process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, '133X_mcRun4_realistic_v1', '')

process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(-1))

process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring(
        "/store/relval/CMSSW_14_0_0_pre2/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/"
        "PU_133X_mcRun4_realistic_v1_STD_2026D98_PU200_RV229-v1/"
        "2580000/0b2b0b0b-f312-48a8-9d46-ccbadc69bbfd.root"
    )
)

process.load("CondCore.CondDB.CondDB_cfi")
process.CondDB.connect = 'frontier://FrontierProd/CMS_CONDITIONS'

process.PoolDBESSource = cms.ESSource("PoolDBESSource",
    process.CondDB,
    DumpStat = cms.untracked.bool(True),
    toGet = cms.VPSet(cms.PSet(
        record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
        tag = cms.string("TrackerDetToDTCELinkCablingMap__OT800_IT711__T33__OTOnly"),
    )),
)

process.es_prefer_local_cabling = cms.ESPrefer("PoolDBESSource", "")

process.ClustersFromPhase2TrackerDigis = cms.EDProducer("Phase2TrackerClusterizer",
    src = cms.InputTag("mix","Tracker"),
)

process.Packer = cms.EDProducer("ClusterToRawProducer",
    Phase2Clusters = cms.InputTag("ClustersFromPhase2TrackerDigis"),
)

process.Analyzer = cms.EDAnalyzer("RawAnalyzer",
    fedRawDataCollection = cms.InputTag("Packer"),
)

process.Unpacker = cms.EDProducer("Phase2RawToClusterProducer@alpaka",
#process.Unpacker = cms.EDProducer("alpaka_serial_sync::Phase2RawToClusterProducer",
#process.Unpacker = cms.EDProducer("alpaka_cuda_async::Phase2RawToClusterProducer",
#process.Unpacker = cms.EDProducer("alpaka_rocm_async::Phase2RawToClusterProducer",
    fedRawDataCollection = cms.InputTag("Packer"),
)

## added: converter from SoA (produced by Unpacker) to legacy DetSetVector
process.ClusterConverter = cms.EDProducer("ClusterPropSoAToLegacyED",
    clusterSoASource = cms.InputTag("Unpacker")  # SoA from the alpaka producer
)

process.out = cms.OutputModule("PoolOutputModule",
    splitLevel = cms.untracked.int32(0),
    eventAutoFlushCompressedSize = cms.untracked.int32(5242880),
    outputCommands = cms.untracked.vstring(
        'drop *',
        'keep FEDRawDataCollection_*_*_*',
        'keep *_ClustersFromPhase2TrackerDigis_*_*',
        'keep *_Packer_*_*',
        'keep *_Unpacker_*_*',
        'keep *_mix_Tracker_*',
        ## added: keep the legacy clusters produced by the converter
        'keep *_ClusterConverter_*_*'
    ),
    fileName = cms.untracked.string('raw2clusters.root')
)

from Configuration.ProcessModifiers.premix_stage2_cff import premix_stage2
premix_stage2.toModify(process.ClustersFromPhase2TrackerDigis, rawHits = ["mixData:Tracker"])

process.Timing = cms.Service("Timing",
    summaryOnly = cms.untracked.bool(True),
    useJobReport = cms.untracked.bool(True)
)
# mark framework transitions in the NVIDIA profiler
process.NVProfilerService = cms.Service("NVProfilerService",
    showModulePrefetching = cms.untracked.bool(False)
)

if Legacy_Format:
    # run with legacy conversion step
    process.dtc = cms.Path(
        process.ClustersFromPhase2TrackerDigis *
        process.Packer *
        process.Unpacker *
        process.ClusterConverter   # only if Legacy_Format = True
    )
else:
    # run without legacy conversion, add analyzer if needed
    process.dtc = cms.Path(
        process.ClustersFromPhase2TrackerDigis *
        process.Packer *
        process.Analyzer *
        process.Unpacker
    )

process.output = cms.EndPath(process.out)
# process.dtc = cms.Path(process.ClustersFromPhase2TrackerDigis * process.Packer * process.Analyzer * process.Unpacker)
