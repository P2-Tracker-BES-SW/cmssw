#ifndef DataFormats_FEDRawData_interface_StripPixelHostCollection_h
#define DataFormats_FEDRawData_interface_StripPixelHostCollection_h

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/FEDRawData/interface/StripPixelSoA.h"

namespace Phase2RawToCluster {

  // SoA with x, y, z, id fields in host memory
  using StripPixelHostCollection = PortableHostCollection<StripPixelSoA>;

}  // namespace Phase2RawToCluster

#endif  // DataFormats_FEDRawData_interface_StripPixelHostCollection_h
