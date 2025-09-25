// Host-only converter: ClusterProp SoA  ->  edmNew::DetSetVector<Phase2TrackerCluster1D>
// Produces legacy AoS clusters from the unpacker SoA output.
//
// This module deliberately avoids any device-side/alpaka queue usage.
// It consumes a PortableHostCollection<Phase2RawToCluster::ClusterPropSoALayout<> >
// and emits Phase2TrackerCluster1DCollectionNew via FastFiller.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// FWCore
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"

// Legacy AoS clusters
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"

// SoA layout
#include "DataFormats/Phase2TrackerCluster/interface/ClusterPropSoA.h"

// Portable host collection
#include "DataFormats/Portable/interface/PortableHostCollection.h"

class ClusterPropSoAToLegacyED : public edm::stream::EDProducer<> {
public:
  explicit ClusterPropSoAToLegacyED(const edm::ParameterSet& pset);
  ~ClusterPropSoAToLegacyED() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descs);

  void produce(edm::Event& iEvent, edm::EventSetup const&) override;

private:
  using SoALayout = Phase2RawToCluster::ClusterPropSoALayout<>;
  using HostSoA = ::PortableHostCollection<SoALayout>;

  edm::EDGetTokenT<HostSoA> soaToken_;
  edm::EDPutTokenT<Phase2TrackerCluster1DCollectionNew> legacyOutToken_;
};

ClusterPropSoAToLegacyED::ClusterPropSoAToLegacyED(const edm::ParameterSet& pset)
    : soaToken_{consumes<HostSoA>(pset.getParameter<edm::InputTag>("clusterSoASource"))},
      legacyOutToken_{produces<Phase2TrackerCluster1DCollectionNew>()} {}

void ClusterPropSoAToLegacyED::fillDescriptions(edm::ConfigurationDescriptions& descs) {
  edm::ParameterSetDescription d;
  // by default read the SoA produced by the Unpacker in this same process
  d.add<edm::InputTag>("clusterSoASource", edm::InputTag("Unpacker", "", "PACKANDUNPACK"));
  descs.addWithDefaultLabel(d);
}

void ClusterPropSoAToLegacyED::produce(edm::Event& iEvent, edm::EventSetup const&) {
  auto const& soa  = iEvent.get(soaToken_);
  auto const view  = soa.view();
  const int nHits = view.metadata().size();

  // group legacy clusters by detId
  std::unordered_map<uint32_t, std::vector<Phase2TrackerCluster1D>> perDet;
  perDet.reserve(1024);
  perDet.max_load_factor(0.7f);

  for (int i = 0; i < nHits; ++i) {
    const uint32_t detId  = view[i].detId();
    const uint32_t x      = view[i].x();       // legacy "firstStrip"
    const uint32_t y      = view[i].y();       // legacy "firstRow"
    const uint32_t width  = view[i].width();   // cluster size (already 0->8 fixed upstream)
    const bool     isSeed = view[i].isSeed();  // for 2S this was the threshold bit

    // Build Phase2TrackerCluster1D exactly like the CPU unpacker:
    Phase2TrackerDigi firstDigi{x, y};
    Phase2TrackerCluster1D cl{firstDigi, width, isSeed ? 1u : 0u};

    perDet[detId].push_back(std::move(cl));
  }

  // Move into edmNew::DetSetVector with FastFiller
  auto out = std::make_unique<Phase2TrackerCluster1DCollectionNew>();
  out->reserve(perDet.size(), nHits);

  for (auto& kv : perDet) {
    const uint32_t detId = kv.first;
    auto& vec = kv.second;

    // Keep same ordering convention as legacy: sort by firstStrip()
    std::sort(vec.begin(), vec.end());

    edmNew::DetSetVector<Phase2TrackerCluster1D>::FastFiller filler(*out, detId, vec.size());
    for (auto const& c : vec)
      filler.push_back(c);
  }

  iEvent.put(legacyOutToken_, std::move(out));
}

DEFINE_FWK_MODULE(ClusterPropSoAToLegacyED);
