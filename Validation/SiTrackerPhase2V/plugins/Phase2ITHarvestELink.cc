// -*- C++ -*-
// Package:    Validation/SiTrackerPhase2V
// Class:      ElinkOccupancyHarvester
// Description: Harvest the per-elink occupancy DQM
//
// author : your name and your email
// when you wrote this

#include <string>
#include <vector>

#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "DQMServices/Core/interface/DQMEDHarvester.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "DQM/SiTrackerPhase2/interface/TrackerPhase2HarvestingUtil.h"
#include "Validation/SiTrackerPhase2V/interface/TrackerPhase2PlotUtil.h"

#include "TProfile2D.h"

class ElinkOccupancyHarvester : public DQMEDHarvester {
public:
  explicit ElinkOccupancyHarvester(const edm::ParameterSet&);
  ~ElinkOccupancyHarvester() override;
  void dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  const edm::ParameterSet config_;
  const std::string topFolder_;
  const std::string occupancyMapName_;
  const bool savePlots_;  // Option to create pdf/png
  const std::string plotDir_;
  const std::vector<std::string> plotFormats_;
  const double zMax_;
};

ElinkOccupancyHarvester::ElinkOccupancyHarvester(const edm::ParameterSet& iConfig)
    : config_(iConfig),
      topFolder_(iConfig.getParameter<std::string>("TopFolder")),
      occupancyMapName_(iConfig.getParameter<std::string>("OccupancyMapName")),
      savePlots_(iConfig.getUntrackedParameter<bool>("savePlots", false)),
      plotDir_(iConfig.getUntrackedParameter<std::string>("plotDir", ".")),
      plotFormats_(iConfig.getUntrackedParameter<std::vector<std::string>>("plotFormats", {"pdf", "png"})),
      zMax_(iConfig.getUntrackedParameter<double>("plotZMax", 1.2)) {}

ElinkOccupancyHarvester::~ElinkOccupancyHarvester() {}

void ElinkOccupancyHarvester::dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) {
  MonitorElement* occMap = igetter.get(topFolder_ + "/" + occupancyMapName_);
  if (occMap == nullptr) {
    edm::LogWarning("ElinkOccupancyHarvester") << occupancyMapName_ << " not found in " << topFolder_;
    return;
  }
  TProfile2D* prof = occMap->getTProfile2D();

  MonitorElement* nGroupME = igetter.get(topFolder_ + "/nStreamGroups");
  const double nGroups = (nGroupME != nullptr) ? nGroupME->getTH1F()->GetBinContent(1) : 0.;
  if (nGroups <= 0) {
    edm::LogWarning("ElinkOccupancyHarvester")
        << "nStreamGroups missing or empty in " << topFolder_ << "; skipping all normalization.";
  }

  // Normalize the raw 1D occupancy to per stream group
  MonitorElement* elinkOcc = igetter.get(topFolder_ + "/eLinkOccupancy");
  if (elinkOcc != nullptr && nGroups > 0) {
    elinkOcc->getTH1F()->Scale(1.0 / nGroups);
    elinkOcc->getTH1F()->SetOption("HIST");
    elinkOcc->setAxisTitle("ELink entries / stream group", 2);
  }

  // Normalization for per-section / per-subtype
  for (auto* me : igetter.getAllContents(topFolder_)) {
    if (me == nullptr)
      continue;
    if (me->getName().rfind("eLinkOccupancyPer", 0) == 0 && nGroups > 0) {
      me->getTH1F()->Scale(1.0 / nGroups);
      me->getTH1F()->SetOption("HIST");
      me->setAxisTitle("ELink entries / stream group", 2);
    }
  }

  // Same normalization for 2D
  MonitorElement* occVsSection = igetter.get(topFolder_ + "/eLinkOccupancyVsSection");
  if (occVsSection != nullptr && nGroups > 0) {
    occVsSection->getTH2F()->Scale(1.0 / nGroups);
    occVsSection->getTH2F()->SetOption("COLZ");
    occVsSection->setAxisTitle("ELink entries / stream group", 3);  // 3 = z-axis
  }

  MonitorElement* occVsSubType = igetter.get(topFolder_ + "/eLinkOccupancyVsSubType");
  if (occVsSubType != nullptr && nGroups > 0) {
    occVsSubType->getTH2F()->Scale(1.0 / nGroups);
    occVsSubType->getTH2F()->SetOption("COLZ");
    occVsSubType->setAxisTitle("ELink entries / stream group", 3);
  }

  ibooker.cd();
  ibooker.setCurrentFolder(topFolder_);

  // Make new hist for event-averaged version of 1D occupancy
  MonitorElement* occAvg =
      phase2tkharvestutil::book1DFromPSet(config_.getParameter<edm::ParameterSet>("occupancyAvg"), ibooker);
  if (occAvg == nullptr)
    return;

  // Fill above with one entry per ELink
  for (int ix = 1; ix <= prof->GetNbinsX(); ix++) {
    for (int iy = 1; iy <= prof->GetNbinsY(); iy++) {
      if (prof->GetBinEntries(prof->GetBin(ix, iy)) > 0)
        occAvg->Fill(prof->GetBinContent(ix, iy));
    }
  }

  if (savePlots_) {
    TrackerPhase2PlotUtil::PlotConfig plotCfg;
    plotCfg.plotDir = plotDir_;
    plotCfg.formats = plotFormats_;
    plotCfg.occMapNamePrefix = "eLinkOccupancyMap";
    plotCfg.zMax = zMax_;
    const int nFiles = TrackerPhase2PlotUtil::saveFolderPlots(igetter, topFolder_, plotCfg);
    edm::LogInfo("ElinkOccupancyHarvester") << "wrote " << nFiles << " plot files to " << plotDir_;
  }
}

void ElinkOccupancyHarvester::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("TopFolder", "Phase2IT/RawData");
  desc.add<std::string>("OccupancyMapName", "eLinkOccupancyMap");
  desc.addUntracked<bool>("savePlots", false);
  desc.addUntracked<std::string>("plotDir", ".");
  desc.addUntracked<std::vector<std::string>>("plotFormats", {"png", "pdf"});
  desc.addUntracked<double>("plotZMax", 1.2);

  edm::ParameterSetDescription psd0;
  psd0.add<std::string>("name", "eLinkOccupancyAvg");
  psd0.add<std::string>("title", "Event-averaged ELink occupancy;occupancy;ELinks");
  psd0.add<int>("NxBins", 28);
  psd0.add<double>("xmax", 1.4);
  psd0.add<double>("xmin", 0.);
  psd0.add<bool>("switch", true);
  desc.add<edm::ParameterSetDescription>("occupancyAvg", psd0);

  descriptions.add("elinkOccupancyHarvester", desc);
}

DEFINE_FWK_MODULE(ElinkOccupancyHarvester);
