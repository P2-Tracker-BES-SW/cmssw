//====================== Header with helper functions and shared algorithmic components used by the unpacker. ==========================
#ifndef EventFilter_Phase2TrackerRawToDigi_RawToClusterAlgo_h
#define EventFilter_Phase2TrackerRawToDigi_RawToClusterAlgo_h

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "DataFormats/Phase2TrackerCluster/interface/ClusterPropDeviceCollection.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2TrackerSpecifications.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"

using namespace Phase2TrackerSpecifications;
using namespace Phase2DAQFormatSpecification;

namespace ALPAKA_ACCELERATOR_NAMESPACE {

    void launchUnpacker(
        Queue& queue,
        cms::alpakatools::device_buffer<Device, unsigned char[]> rawdatabuff,
        cms::alpakatools::device_buffer<Device, size_t[]>        sizedatabuff,
        cms::alpakatools::device_buffer<Device, size_t[]>        offsetdatabuff,
        cms::alpakatools::device_buffer<Device, int[]>           detIdxModuleTypeDevice,
        cms::alpakatools::device_buffer<Device, uint32_t[]>      innerDetIdDevice,  // uint32_t
        cms::alpakatools::device_buffer<Device, uint32_t[]>      outerDetIdDevice,  // uint32_t
        Phase2RawToCluster::ClusterPropDeviceCollection::View out,
        uint32_t* globalCounter
    );

} // namespace ALPAKA_ACCELERATOR_NAMESPACE

#endif // EventFilter_Phase2TrackerRawToDigi_RawToClusterAlgo_h