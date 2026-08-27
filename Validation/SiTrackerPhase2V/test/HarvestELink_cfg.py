import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

options = VarParsing.VarParsing('analysis')
options.register('inputDQM', 'file:elink_output.root',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'DQM file produced by ValidateELink_cfg.py')
options.parseArguments()

process = cms.Process('HARVEST')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('DQMServices.Core.DQMStore_cfi')
process.load('Configuration.StandardSequences.DQMSaverAtRunEnd_cff')

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(-1))

process.source = cms.Source('DQMRootSource',
    fileNames=cms.untracked.vstring(options.inputDQM),
)

process.elinkOccupancyHarvester = cms.EDProducer('ElinkOccupancyHarvester',
    TopFolder        = cms.string('Phase2IT/RawData'),
    OccupancyMapName = cms.string('eLinkOccupancyMap'),
    occupancyAvg = cms.PSet(
        switch = cms.bool(True),
        name   = cms.string('eLinkOccupancyAvg'),
        title  = cms.string('Event-Averaged ELink Occupancy;<Occupancy>;ELink entries'),
        NxBins = cms.int32(60),
        xmin   = cms.double(0.),
        xmax   = cms.double(1.2),
    ),
)

process.harvest_step = cms.Path(process.elinkOccupancyHarvester)
process.dqmsave_step = cms.Path(process.DQMSaver)
process.schedule = cms.Schedule(process.harvest_step, process.dqmsave_step)

process.options.numberOfThreads = cms.untracked.uint32(1)
