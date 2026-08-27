## Cfg file to run the Phase2TrackerDumpClusters plugin:
## will produce ntuples with cluster properties
## Runs on .root file written by SLinkProducerAndUnpacker_cfg.py,
## either analyzing original clusters or those obtained from the packer+unpacker
## sequence, depending on value of ANALYZE_PACKUNPACK variable below.

import FWCore.ParameterSet.Config as cms

process = cms.Process("Analysis")

# If this is True, then the clusters created by running the packer + unpacker on
# the original clusters will be analyzed.
# If it is False, then the original clusters will be analyzed.
ANALYZE_PACKUNPACK = False

# Enable summary at the end of the job
process.options = cms.untracked.PSet( wantSummary = cms.untracked.bool(True) )

# Limit the number of events to process
process.maxEvents = cms.untracked.PSet( input = cms.untracked.int32(-1) )

# Define the EDAnalyzer with the correct product label
process.ClusterAnalyzer = cms.EDAnalyzer('ClusterAnalyzer',
    ProductLabel = cms.InputTag("hltSiPhase2Clusters")
)

process.source = cms.Source("PoolSource", fileNames = cms.untracked.vstring("file:raw2clusters.root"))

if ANALYZE_PACKUNPACK:

  print("\n === Analyzing clusters created by pack + unpack sequence ===\n")
  
  # Update label to match the output from the digi-raw-digi process
  process.ClusterAnalyzer.ProductLabel = cms.InputTag("Unpacker", "", "PACKANDUNPACK")

else:
  print("\n === Analyzing original clusters ===\n")
  # Read original clusters from input dataset
  process.ClusterAnalyzer.ProductLabel = cms.InputTag("hltSiPhase2Clusters")
  # Read clusters remade from digis by SLinkProducerAndUnpacker_cfg.py.
  #process.ClusterAnalyzer.ProductLabel = cms.InputTag("siPhase2Clusters", "", "PACKANDUNPACK")
  

# Create output root file for TTree.
process.TFileService = cms.Service('TFileService', 
    fileName = cms.string(
        'ClusterAnalyzer_TTree.root'
    ), 
    closeFileFast = cms.untracked.bool(True)
)

## Load Geometry for the D98 configuration
process.load('Configuration.Geometry.GeometryExtendedRun4D110Reco_cff')

# Load the standard sequences for conditions and global tags
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
from Configuration.AlCa.GlobalTag import GlobalTag

# Set the GlobalTag (adjust as necessary for your geometry)
#process.GlobalTag = GlobalTag(process.GlobalTag, '133X_mcRun4_realistic_v1', '')
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic', '')

process.load("CondCore.CondDB.CondDB_cfi")
process.CondDB.connect = 'frontier://FrontierProd/CMS_CONDITIONS'

#process.PoolDBESSource = cms.ESSource("PoolDBESSource",
#    process.CondDB,
#    DumpStat = cms.untracked.bool(True),
#    toGet = cms.VPSet(cms.PSet(
#        record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
#        tag = cms.string("TrackerDetToDTCELinkCablingMap__OT800_IT711__T33__OTOnly"),
#    )),
#)
#process.es_prefer_local_cabling = cms.ESPrefer("PoolDBESSource", "")


# Define the path to run the EDAnalyzer
process.p = cms.Path(process.ClusterAnalyzer)
