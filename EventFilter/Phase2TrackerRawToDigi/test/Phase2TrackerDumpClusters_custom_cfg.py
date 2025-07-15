## Cfg file to run the Phase2TrackerDumpClusters plugin:
## will produce ntuples with cluster properties

import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

process = cms.Process("DumpClusters")

## Enable summary at the end of the job
process.options = cms.untracked.PSet( wantSummary = cms.untracked.bool(True) )

## Add option to customise input tags if running on unpacked clusters 
options = VarParsing.VarParsing ('analysis')
options.register ("onUnpacked",  
                   False, 
                   VarParsing.VarParsing.multiplicity.singleton,
                   VarParsing.VarParsing.varType.bool,
                   "define if running on unpacked clusters or on original ones");   
                      
options.parseArguments()

## Limit the number of events to process
process.maxEvents = cms.untracked.PSet( input = cms.untracked.int32(1) )

## Define the EDAnalyzer with the correct product label
process.Phase2TrackerDumpClusters = cms.EDAnalyzer(
    'Phase2TrackerDumpClusters',
    ProductLabel = cms.InputTag("siPhase2Clusters")
)

process.TFileService = cms.Service('TFileService', 
    fileName = cms.string(options.outputFile), 
    closeFileFast = cms.untracked.bool(True)
)

process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring (options.inputFiles)
)

## Update the ProductLabel to match the output from the digi-raw-digi process
if options.onUnpacked:
  process.Phase2TrackerDumpClusters.ProductLabel = cms.InputTag("Unpacker", "", "PACKANDUNPACK")


## Load Geometry for the D98 configuration
process.load('Configuration.Geometry.GeometryExtendedRun4D98Reco_cff')

# Load the standard sequences for conditions and global tags
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
from Configuration.AlCa.GlobalTag import GlobalTag

# Set the GlobalTag (adjust as necessary for your geometry)
process.GlobalTag = GlobalTag(process.GlobalTag, '133X_mcRun4_realistic_v1', '')

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

# Define the path to run the EDAnalyzer
process.p = cms.EndPath(process.Phase2TrackerDumpClusters)
