## cfg file to run the EDAnalyzer RawAnalyzer, to print the
## contents of the FedRawDataCollection EDProduct, which contains
## the binary raw data from the detector.

import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing
import FWCore.Utilities.FileUtils as FileUtils
import os

process = cms.Process("Analysis")

process.load('Configuration.StandardSequences.Services_cff')
process.load('Configuration.EventContent.EventContent_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('FWCore.MessageService.MessageLogger_cfi')


process.load('Configuration.Geometry.GeometryExtendedRun4D98Reco_cff')
process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, '133X_mcRun4_realistic_v1', '')

process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(-1))

process.source = cms.Source("PoolSource",
  fileNames = cms.untracked.vstring( "file:outputFEDRawData.root")
#  fileNames = cms.untracked.vstring( "file:srecko_output_dataset.root")
)

process.Analyzer = cms.EDAnalyzer("RawAnalyzer",

  # fedRawDataCollection = cms.InputTag("Packer") # RAW from running packer
                                  
  fedRawDataCollection = cms.InputTag("dthDAQToFEDRawData") # RAW came from Alaa's DTH to RAW converter.
  #fedRawDataCollection = cms.InputTag("rawDataCollector") # RAW came from Srecko's DTH to RAW converter.
)

process.anal = cms.Path(process.Analyzer)

