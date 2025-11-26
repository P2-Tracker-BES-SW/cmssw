// ================================ The alpaka EDProducer that unpacks FEDRawData and produces a ClusterPropSoA, which ==================================
// ================================ contains Phase-2 Outer Tracker cluster information.==================================================================
#include "DataFormats/Phase2TrackerCluster/interface/ClusterPropHostCollection.h"
#include "DataFormats/Phase2TrackerCluster/interface/ClusterPropDeviceCollection.h"

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "DataFormats/Phase2TrackerCluster/interface/alpaka/ClusterPropSoACollection.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToDevice.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToHost.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/DTCELinkId.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"

#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"

#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/CommonTopologies/interface/PixelGeomDetUnit.h"
#include "Geometry/CommonTopologies/interface/PixelTopology.h"
#include <unordered_map>
#include <numeric> // exclusive_scan
#include <algorithm>
#include <limits>

#include "EventFilter/Phase2TrackerRawToDigi/interface/TrackerHeader.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/ChannelsOffset.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2TrackerSpecifications.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2TrackerRawToDigi/plugins/alpaka/RawToClusterAlgo.h"
#include <iomanip> // for std::setw
#include <future>
#include "FWCore/Framework/interface/ESWatcher.h"

//From the CPU based code
using namespace Phase2TrackerSpecifications;
using namespace Phase2DAQFormatSpecification;
using namespace Phase2RawToCluster;

// debug flag
//#define Debug_CPU

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  using namespace cms::alpakatools;

  class Phase2RawToClusterProducer : public stream::EDProducer<> {
  public:
    explicit Phase2RawToClusterProducer(const edm::ParameterSet&);
    static void fillDescriptions(edm::ConfigurationDescriptions&);
    void beginRun(edm::Run const&, edm::EventSetup const&) override;

    // enumeration declaration for the module types
    enum WhichModule:int { undef=0, TwoS=1, PS=2 };

  private:
    void produce(device::Event&, device::EventSetup const&) override;

    // Tokens for acquiring the RAW data
    const edm::EDGetTokenT<FEDRawDataCollection> fedRawDataToken_;
    const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
    const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord>                       trackerGeometryToken_;
    const edm::ESGetToken<TrackerTopology, TrackerTopologyRcd>                               trackerTopologyToken_;

    // SoA output token
    device::EDPutToken<Phase2RawToCluster::ClusterPropDeviceCollection> outputToken_;

    // cached ES pointers
    const TrackerDetToDTCELinkCablingMap* cablingMap_      = nullptr;
    const TrackerGeometry*                trackerGeometry_ = nullptr;
    const TrackerTopology*                trackerTopology_ = nullptr;

    std::map<int, std::pair<int,int>> stackMap_; // detId -> (inner, outer)

    // device/platform
    Device devAcc = alpaka::getDevByIdx(Platform{}, 0u);
    // Make new queue to access a queue before the produce method 
    Queue  myqueue;

    // Global maps sized as (#DTC * #SLINK * #CIC)
    cms::alpakatools::host_buffer<int[]> detIdxModuleTypeMap_;
    cms::alpakatools::device_buffer<Device, int[]> detIdxModuleTypeDevice_;

    // stack map mirrored into aligned flatIdx arrays (inner/outer)
    cms::alpakatools::host_buffer<uint32_t[]> innerDetIdHost_;
    cms::alpakatools::host_buffer<uint32_t[]> outerDetIdHost_;
    cms::alpakatools::device_buffer<Device, uint32_t[]> innerDetIdDevice_;
    cms::alpakatools::device_buffer<Device, uint32_t[]> outerDetIdDevice_;

    // flat detId per flatIdx
    cms::alpakatools::host_buffer<int[]> detIdMapHost_;
    cms::alpakatools::device_buffer<Device, int[]> detIdMapDevice_;
  };

  Phase2RawToClusterProducer::Phase2RawToClusterProducer(const edm::ParameterSet& iConfig)
  : stream::EDProducer<>(iConfig)
  , fedRawDataToken_(consumes<FEDRawDataCollection>(iConfig.getParameter<edm::InputTag>("fedRawDataCollection")))
  , cablingMapToken_(esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>())
  , trackerGeometryToken_(esConsumes<TrackerGeometry, TrackerDigiGeometryRecord, edm::Transition::BeginRun>())
  , trackerTopologyToken_(esConsumes<TrackerTopology, TrackerTopologyRcd, edm::Transition::BeginRun>())
  , outputToken_{ produces() }  // SoA only
  , myqueue(devAcc)
  , detIdxModuleTypeMap_{ cms::alpakatools::make_host_buffer<int[], Platform>((MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK) }
  , detIdxModuleTypeDevice_{ cms::alpakatools::make_device_buffer<int[]>(myqueue, (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK) }
  , innerDetIdHost_{ cms::alpakatools::make_host_buffer<uint32_t[], Platform>((MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK) }
  , outerDetIdHost_{ cms::alpakatools::make_host_buffer<uint32_t[], Platform>((MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK) }
  , innerDetIdDevice_{ cms::alpakatools::make_device_buffer<uint32_t[]>(myqueue, (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK) }
  , outerDetIdDevice_{ cms::alpakatools::make_device_buffer<uint32_t[]>(myqueue, (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK) }
  , detIdMapHost_{ cms::alpakatools::make_host_buffer<int[], Platform>((MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK) }
  , detIdMapDevice_{ cms::alpakatools::make_device_buffer<int[]>(myqueue, (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK) }
  {}

  void Phase2RawToClusterProducer::beginRun(edm::Run const&, edm::EventSetup const& iSetup) {
    cablingMap_      = &iSetup.getData(cablingMapToken_);
    trackerGeometry_ = &iSetup.getData(trackerGeometryToken_);
    trackerTopology_ = &iSetup.getData(trackerTopologyToken_);

    // Build the stack map (detId -> (inner, outer))
    stackMap_.clear();
    for (auto iu = trackerGeometry_->detUnits().begin(); iu != trackerGeometry_->detUnits().end(); ++iu) {
      const unsigned int detId_raw = (*iu)->geographicalId().rawId();
      DetId detId(detId_raw);
      if (detId.det() == DetId::Detector::Tracker) {
        if (trackerTopology_->isLower(detId) != 0) {
          stackMap_[trackerTopology_->stack(detId)].first = detId;
        }
        if (trackerTopology_->isUpper(detId) != 0) {
          stackMap_[trackerTopology_->stack(detId)].second = detId;
        }
      }
    }

// Total number of logical "channels" to initialize:
// (number of DTCs) × (number of Slinks per DTC) × (number of CICs per Slink)
// This defines the maximum flat index space for detId/module mapping
 const unsigned M = (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC * CICs_PER_SLINK;


// Initialize per channel lookup tables with invalid defaults
//
// Unlike the RawToCluster CPU producer (which queried cablingMap/stackMap) the SoA path uses flat pre allocated arrays indexed
// by channel. This ensures disconnected or unused channels are ignored by the kernels.
    for (unsigned i = 0; i < M; ++i) {
      detIdxModuleTypeMap_[i] = WhichModule::undef;
      innerDetIdHost_[i] = outerDetIdHost_[i] = 0u; // 0 == invalid
      detIdMapHost_[i] = -1;
    }

    // Fill per-flatIdx (module type, inner/outer detIds)
    for (int dtcID = MIN_DTC_ID; dtcID < MAX_DTC_ID + 1; ++dtcID) {
      for (unsigned iSlink = 0; iSlink < SLINKS_PER_DTC; ++iSlink) {
        for (unsigned iChannel = 0; iChannel < CICs_PER_SLINK; ++iChannel) {
          const unsigned int gbt_id = iSlink * MODULES_PER_SLINK + std::div(iChannel, 2).quot;
          DTCELinkId thisDTCElinkId(dtcID, gbt_id, 0);

          const unsigned flatIdx = iChannel + (CICs_PER_SLINK * iSlink) +
                                   (CICs_PER_SLINK * SLINKS_PER_DTC * (dtcID - MIN_DTC_ID));

          if (cablingMap_->knowsDTCELinkId(thisDTCElinkId)) {
            auto possibleDetIds = cablingMap_->dtcELinkIdToDetId(thisDTCElinkId);
            const int thisDetId = possibleDetIds->second;
            detIdMapHost_[flatIdx] = thisDetId;

            const bool is2S = trackerGeometry_->getDetectorType(stackMap_[thisDetId].first) == TrackerGeometry::ModuleType::Ph2SS;
            detIdxModuleTypeMap_[flatIdx] = is2S ? WhichModule::TwoS : WhichModule::PS;

            const auto it = stackMap_.find(thisDetId);
            if (it != stackMap_.end()) {
              innerDetIdHost_[flatIdx] = static_cast<uint32_t>(it->second.first);
              outerDetIdHost_[flatIdx] = static_cast<uint32_t>(it->second.second);
            }
          }
        }
      }
    }

    // Copy maps to device
    alpaka::memcpy(myqueue, detIdxModuleTypeDevice_, detIdxModuleTypeMap_, M);
    alpaka::memcpy(myqueue, innerDetIdDevice_,       innerDetIdHost_,     M);
    alpaka::memcpy(myqueue, outerDetIdDevice_,       outerDetIdHost_,     M);
    alpaka::memcpy(myqueue, detIdMapDevice_,         detIdMapHost_,       M);
    alpaka::wait(myqueue);
  }

  void Phase2RawToClusterProducer::produce(device::Event& iEvent, device::EventSetup const&) {
    auto queue = iEvent.queue();

    // Upper bound on the total number of clusters across the full readout:
    // (max clusters per channel) × (channels per Slink) × (Slinks per DTC × number of DTCs).
    static constexpr size_t MaxTotalClusters =
      (N_CLUSTER_MASK + 1) * CICs_PER_SLINK * (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC;

    // 1) Flatten FED buffers into a single contiguous memory block.
    //    Each Slink buffer (raw FED data) is concatenated into one linearData vector.
    auto const& rawColl = iEvent.get(fedRawDataToken_);
    const size_t numSlinks = (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC;

    // Track FED IDs, payload sizes, and offsets for each Slink.
    std::vector<unsigned int> totIDs(numSlinks);
    std::vector<size_t> size(numSlinks, 0);
    std::vector<size_t> offset(numSlinks, 0);
    std::vector<unsigned char> linearData;

    // Loop over all DTCs and their Slinks to collect FED IDs and buffer sizes.
    size_t slinkIdx = 0;
    for (int dtcID = MIN_DTC_ID; dtcID < MAX_DTC_ID + 1; ++dtcID) {
      for (unsigned iSlink = 0; iSlink < SLINKS_PER_DTC; ++iSlink) {
        const unsigned totID = iSlink + SLINKS_PER_DTC * (dtcID - 1) + CMSSW_TRACKER_ID;
        totIDs[slinkIdx] = totID;
        const FEDRawData& fedData = rawColl.FEDData(totID);
        size[slinkIdx] = fedData.size();  // payload size in bytes
        ++slinkIdx;
      }
    }

    // Compute starting offsets for each Slink buffer inside the linear array.
    std::exclusive_scan(size.begin(), size.end(), offset.begin(), 0);
    const size_t totalBytes = offset.back() + (numSlinks ? size.back() : 0);
    linearData.resize(totalBytes, 0u);

    // Copy each FED buffer into its slot inside the linearData vector.
    unsigned char* start = linearData.data();
    for (size_t idx = 0; idx < numSlinks; ++idx) {
      if (size[idx] == 0) continue;  // skip empty FED buffers
      const FEDRawData& data = rawColl.FEDData(totIDs[idx]);
      if (offset[idx] + size[idx] > totalBytes) {
        throw std::runtime_error("BUFFER OVERFLOW DETECTED IN RAW DATA COPYING");
      }
      std::memcpy(start + offset[idx], data.data(), size[idx]);
    }

    // 2) Device buffers and copies
    auto linearData_HostView = cms::alpakatools::make_host_view<unsigned char>(linearData.data(), static_cast<unsigned long>(linearData.size()));
    auto linearData_DevBuffer = cms::alpakatools::make_device_buffer<unsigned char[]>(queue, static_cast<unsigned long>(linearData.size()));
    alpaka::memset(queue, linearData_DevBuffer, 0x00);
    alpaka::memcpy(queue, linearData_DevBuffer, linearData_HostView, static_cast<unsigned int>(linearData.size()));
    alpaka::wait(queue);

    auto size_HostView = cms::alpakatools::make_host_view<size_t>(size.data(), static_cast<unsigned long>(size.size()));
    auto size_DevBuffer = cms::alpakatools::make_device_buffer<size_t[]>(queue, static_cast<unsigned long>(size.size()));
    alpaka::memset(queue, size_DevBuffer, 0x00);
    alpaka::memcpy(queue, size_DevBuffer, size_HostView, static_cast<unsigned int>(size.size()));
    alpaka::wait(queue);

    auto offset_HostView = cms::alpakatools::make_host_view<size_t>(offset.data(), static_cast<unsigned long>(offset.size()));
    auto offset_DevBuffer = cms::alpakatools::make_device_buffer<size_t[]>(queue, static_cast<unsigned long>(offset.size()));
    alpaka::memset(queue, offset_DevBuffer, 0x00);
    alpaka::memcpy(queue, offset_DevBuffer, offset_HostView, static_cast<unsigned int>(offset.size()));
    alpaka::wait(queue);

    // 3) Output SoA and counter
    auto devClusterProp = Phase2RawToCluster::ClusterPropDeviceCollection(MaxTotalClusters, queue);
    auto&& devClusterPropBuffer = devClusterProp.buffer();
    alpaka::memset(queue, devClusterPropBuffer, 0x00);
    // to track number of clusters filled
    auto globalCounter = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, 1u);
    alpaka::memset(queue, globalCounter, 0u);
    alpaka::wait(queue);

    // 4) Kernel launch
    launchUnpacker(
      queue,
      linearData_DevBuffer,
      size_DevBuffer,
      offset_DevBuffer,
      detIdxModuleTypeDevice_,
      innerDetIdDevice_,   
      outerDetIdDevice_,   
      devClusterProp.view(),
      globalCounter.data()
    );

    // 5) Copy output to host SoA
    Phase2RawToCluster::ClusterPropDeviceCollection hostClusterPropSoA(MaxTotalClusters, queue);
    auto hostBuf = hostClusterPropSoA.buffer();
    alpaka::memset(queue, hostBuf, 0x00);
    alpaka::memcpy(queue, hostBuf, devClusterProp.const_buffer());
    alpaka::wait(queue);

    // 6) Put SoA into the event  (keep the SoA output)
    iEvent.emplace(outputToken_, std::move(hostClusterPropSoA));
  }

  void Phase2RawToClusterProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("fedRawDataCollection");
    descriptions.addWithDefaultLabel(desc);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

// define as a plugin
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(Phase2RawToClusterProducer);