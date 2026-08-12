// -*- C++ -*-
// Package:    Validation/SiTrackerPhase2V
// Class:      SlinkOccupancyHarvester
// Description: Harvest the per-SLink occupancy DQM
//

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

class SlinkOccupancyHarvester : public DQMEDHarvester {
public:
  explicit SlinkOccupancyHarvester(const edm::ParameterSet&);
  ~SlinkOccupancyHarvester() override;
  void dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  const edm::ParameterSet config_;
  const std::string topFolder_;
  const std::string occupancyMapName_;
  const bool savePlots_;
  const std::string plotDir_;
  const std::vector<std::string> plotFormats_;
  const double zMax_;
};

SlinkOccupancyHarvester::SlinkOccupancyHarvester(const edm::ParameterSet& iConfig)
    : config_(iConfig),
      topFolder_(iConfig.getParameter<std::string>("TopFolder")),
      occupancyMapName_(iConfig.getParameter<std::string>("OccupancyMapName")),
      savePlots_(iConfig.getUntrackedParameter<bool>("savePlots", false)),
      plotDir_(iConfig.getUntrackedParameter<std::string>("plotDir", ".")),
      plotFormats_(iConfig.getUntrackedParameter<std::vector<std::string>>("plotFormats", {"png", "pdf"})),
      zMax_(iConfig.getUntrackedParameter<double>("plotZMax", 1.2)) {}

SlinkOccupancyHarvester::~SlinkOccupancyHarvester() {}

void SlinkOccupancyHarvester::dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) {
  MonitorElement* occMap = igetter.get(topFolder_ + "/" + occupancyMapName_);
  if (occMap == nullptr) {
    edm::LogWarning("SlinkOccupancyHarvester") << occupancyMapName_ << " not found in " << topFolder_;
    return;
  }
  TProfile2D* prof = occMap->getTProfile2D();

  MonitorElement* nEvtME = igetter.get(topFolder_ + "/nEvents");
  const double nevents = (nEvtME != nullptr) ? nEvtME->getTH1F()->GetBinContent(1) : 0.;
  if (nevents <= 0) {
    edm::LogWarning("SlinkOccupancyHarvester")
        << "nEvents missing or empty in " << topFolder_ << "; skipping all normalization.";
  }

  // Normalize the raw 1D occupancy to per-event
  MonitorElement* slinkOcc = igetter.get(topFolder_ + "/slinkOccupancy");
  if (slinkOcc != nullptr && nevents > 0) {
    slinkOcc->getTH1F()->Scale(1.0 / nevents);
    slinkOcc->getTH1F()->SetOption("HIST");
    slinkOcc->setAxisTitle("SLink entries / nevents", 2);
  }

  // Normalization for per-DTC full spectrum occupancy
  for (auto* me : igetter.getAllContents(topFolder_)) {
    if (me == nullptr)
      continue;
    if (me->getName().rfind("slinkSpectrumOccupancyPerDTC_", 0) == 0 && nevents > 0) {
      me->getTH1F()->Scale(1.0 / nevents);
      me->getTH1F()->SetOption("HIST");
      me->setAxisTitle("SLink entries / nevents", 2);
    }
  }

  // Same normalization for 2D
  MonitorElement* occVsDTC = igetter.get(topFolder_ + "/slinkOccupancyVsDTC");
  if (occVsDTC != nullptr && nevents > 0) {
    occVsDTC->getTH2F()->Scale(1.0 / nevents);
    occVsDTC->getTH2F()->SetOption("COLZ");
    occVsDTC->setAxisTitle("SLink entries / nevents", 3);
  }

  // Make new hist for event-averaged version of 1D occupancy
  ibooker.cd();
  ibooker.setCurrentFolder(topFolder_);
  MonitorElement* occAvg =
      phase2tkharvestutil::book1DFromPSet(config_.getParameter<edm::ParameterSet>("occupancyAvg"), ibooker);
  if (occAvg == nullptr)
    return;

  // Fill above with one entry per SLink
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
    plotCfg.occMapNamePrefix = "slinkOccupancyMap";
    plotCfg.zMax = zMax_;
    const int nFiles = TrackerPhase2PlotUtil::saveFolderPlots(igetter, topFolder_, plotCfg);
    edm::LogInfo("SlinkOccupancyHarvester") << "wrote " << nFiles << " plot files to " << plotDir_;
  }
}

void SlinkOccupancyHarvester::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("TopFolder", "Phase2IT/RawData");
  desc.add<std::string>("OccupancyMapName", "slinkOccupancyMap");
  desc.addUntracked<bool>("savePlots", false);
  desc.addUntracked<std::string>("plotDir", ".");
  desc.addUntracked<std::vector<std::string>>("plotFormats", {"png", "pdf"});
  desc.addUntracked<double>("plotZMax", 1.2);

  edm::ParameterSetDescription psd0;
  psd0.add<std::string>("name", "slinkOccupancyAvg");
  psd0.add<std::string>("title", "Event-averaged SLink occupancy;occupancy;SLinks");
  psd0.add<int>("NxBins", 28);
  psd0.add<double>("xmax", 1.4);
  psd0.add<double>("xmin", 0.);
  psd0.add<bool>("switch", true);
  desc.add<edm::ParameterSetDescription>("occupancyAvg", psd0);

  descriptions.add("slinkOccupancyHarvester", desc);
}

DEFINE_FWK_MODULE(SlinkOccupancyHarvester);
