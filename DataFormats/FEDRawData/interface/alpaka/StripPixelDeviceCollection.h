#ifndef DataFormats_FEDRawData_interface_alpaka_StripPixelDeviceCollection_h
#define DataFormats_FEDRawData_interface_alpaka_StripPixelDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "DataFormats/FEDRawData/interface/StripPixelSoA.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  namespace Phase2RawToCluster {

    // make the names from the top-level
    using namespace ::Phase2RawToCluster;

    // SoA in device global memory
    using StripPixelDeviceCollection = PortableCollection<StripPixelSoA>;

  }  // namespace Phase2RawToCluster

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#endif  // DataFormats_FEDRawData_interface_alpaka_StripPixelDeviceCollection_h
