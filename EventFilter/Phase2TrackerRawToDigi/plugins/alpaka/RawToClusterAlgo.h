//====================== Header with helper functions and shared algorithmic components used by the unpacker. ==========================
#ifndef EventFilter_Phase2TrackerRawToDigi_RawToClusterAlgo_h
#define EventFilter_Phase2TrackerRawToDigi_RawToClusterAlgo_h

#include "DataFormats/Phase2TrackerCluster/interface/ClusterPropDeviceCollection.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h" 

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  // number of possible DAQ path core IDs (4-bit field of the tracker header):
  // with CRACK the module tables are indexed by (DTC, core ID, channel), used in beginRun and in the kernel
  inline constexpr unsigned int N_CORE_IDS = 1u << Phase2DAQFormatSpecification::C_NUM_BITS_CORE_ID;

  void launchUnpacker(Queue& queue,
                      cms::alpakatools::device_buffer<Device, unsigned char[]> const& rawdatabuff,
                      cms::alpakatools::device_buffer<Device, size_t[]> const& sizedatabuff,
                      cms::alpakatools::device_buffer<Device, size_t[]> const& offsetdatabuff,
                      cms::alpakatools::device_buffer<Device, int[]> const& detIdxModuleTypeDevice,
                      cms::alpakatools::device_buffer<Device, uint32_t[]> const& innerDetIdDevice,
                      cms::alpakatools::device_buffer<Device, uint32_t[]> const& outerDetIdDevice,
                      const bool analyzeCRACK,  
                      Phase2RawToCluster::ClusterPropDeviceCollection::View out,
                      uint32_t* globalCounter);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#endif  // EventFilter_Phase2TrackerRawToDigi_RawToClusterAlgo_h
