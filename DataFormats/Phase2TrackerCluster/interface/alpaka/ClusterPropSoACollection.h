#ifndef DataFormats_Phase2TrackerCluster_interface_alpaka_ClusterPropSoACollection_h
#define DataFormats_Phase2TrackerCluster_interface_alpaka_ClusterPropSoACollection_h

#include <type_traits>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "DataFormats/Phase2TrackerCluster/interface/ClusterPropDeviceCollection.h"

//#include "DataFormats/TrackSoA/interface/TracksDevice.h"
#include "DataFormats/Phase2TrackerCluster/interface/ClusterPropHostCollection.h"
//#include "DataFormats/TrackSoA/interface/TracksHost.h"

//#include "Geometry/CommonTopologies/interface/SimplePixelTopology.h"

#include "HeterogeneousCore/AlpakaInterface/interface/AssertDeviceMatchesHostCollection.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToHost.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace Phase2RawToCluster {

 // using ::reco::TracksDevice;
//  using ::reco::TracksHost;
  using ClusterPropSoACollection =
      std::conditional_t<std::is_same_v<ALPAKA_ACCELERATOR_NAMESPACE::Device, alpaka::DevCpu>, Phase2RawToCluster::ClusterPropHostCollection, ALPAKA_ACCELERATOR_NAMESPACE::Phase2RawToCluster::ClusterPropDeviceCollection>;

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::reco

//ASSERT_DEVICE_MATCHES_HOST_COLLECTION(reco::TracksSoACollection, reco::TracksHost);

#endif  // DataFormats_Phase2TrackerCluster_interface_alpaka_ClusterPropSoACollection_h
