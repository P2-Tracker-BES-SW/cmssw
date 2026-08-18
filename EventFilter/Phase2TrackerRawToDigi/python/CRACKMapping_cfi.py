import FWCore.ParameterSet.Config as cms

crackMapping = cms.VPSet(
    # offset 0 (EMP Channel 04) -> GBT 70
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(0), gbtID = cms.int32(70), coreID = cms.int32(0)),
    # offset 1 (EMP Channel 05) -> GBT 67
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(1), gbtID = cms.int32(67), coreID = cms.int32(0)),
    # offset 2 (EMP Channel 06) -> GBT 63
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(2), gbtID = cms.int32(63), coreID = cms.int32(0)),
    # offset 3 (EMP Channel 07) -> GBT 71
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(3), gbtID = cms.int32(71), coreID = cms.int32(0)),
    # offset 4 (EMP Channel 08) -> GBT 69
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(4), gbtID = cms.int32(69), coreID = cms.int32(0)),
    # offset 5 (EMP Channel 09) -> GBT 65
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(5), gbtID = cms.int32(65), coreID = cms.int32(0)),
    # offset 6 (EMP Channel 10) -> GBT 61
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(6), gbtID = cms.int32(61), coreID = cms.int32(0)),
    # offset 7 (EMP Channel 11) -> GBT 62
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(7), gbtID = cms.int32(62), coreID = cms.int32(0)),
    # offset 8 (EMP Channel 12) -> GBT 66
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(8), gbtID = cms.int32(66), coreID = cms.int32(0)),
    # offset 9 (EMP Channel 13) -> GBT 60
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(9), gbtID = cms.int32(60), coreID = cms.int32(0)),
    # offset 10 (EMP Channel 14) -> GBT 64
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(10), gbtID = cms.int32(64), coreID = cms.int32(0)),
    # offset 11 (EMP Channel 15) -> GBT 68
    cms.PSet(dtc = cms.int32(1), offset = cms.int32(11), gbtID = cms.int32(68), coreID = cms.int32(0))
)