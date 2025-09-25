#ifndef DataFormats_FEDRawData_interface_StripPixelSoA_h
#define DataFormats_FEDRawData_interface_StripPixelSoA_h

#include <Eigen/Core>
#include <Eigen/Dense>

#include "DataFormats/SoATemplate/interface/SoACommon.h"
#include "DataFormats/SoATemplate/interface/SoALayout.h"
#include "DataFormats/SoATemplate/interface/SoAView.h"

namespace Phase2RawToCluster {

  // Generate structure of arrays (SoA) layout
  GENERATE_SOA_LAYOUT(StripPixelSoALayout,
                      // columns: one value per element
                      SOA_COLUMN(uint32_t, stripClustersWords),
                      SOA_COLUMN(uint32_t, pixelClustersWords)
  )
  using StripPixelSoA = StripPixelSoALayout<>;
 
}  // namespace Phase2RawToCluster

#endif  // DataFormats_FEDRawData_interface_StripPixelSoA_h
