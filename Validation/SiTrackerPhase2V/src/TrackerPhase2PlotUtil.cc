#include "Validation/SiTrackerPhase2V/interface/TrackerPhase2PlotUtil.h"

#include <algorithm>
#include <filesystem>
#include <memory>

#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "TCanvas.h"
#include "TColor.h"
#include "TH1.h"
#include "TStyle.h"

namespace {

  constexpr int kStdWidthPx = 900;
  constexpr int kStdHeightPx = 600;
  constexpr int kWideMinBins = 40;
  constexpr int kWidePxPerBin = 11;
  constexpr int kWidePadPx = 300;
  constexpr int kWideMinPx = 760;
  constexpr int kWideMaxPx = 4200;
  constexpr int kWideHeightPx = 400;

  class StyleGuard {
  public:
    StyleGuard() : optStat_(gStyle->GetOptStat()), nContours_(gStyle->GetNumberContours()) {
      const TArrayI& pal = TColor::GetPalette();
      palette_.assign(pal.GetArray(), pal.GetArray() + pal.GetSize());
    }
    ~StyleGuard() {
      gStyle->SetOptStat(optStat_);
      gStyle->SetNumberContours(nContours_);
      if (!palette_.empty())
        gStyle->SetPalette(static_cast<Int_t>(palette_.size()), palette_.data());
    }
    StyleGuard(const StyleGuard&) = delete;
    StyleGuard& operator=(const StyleGuard&) = delete;

  private:
    int optStat_;
    int nContours_;
    std::vector<Int_t> palette_;
  };

}  // namespace

int TrackerPhase2PlotUtil::saveFolderPlots(dqm::harvesting::DQMStore::IGetter& igetter,
                                           const std::string& folder,
                                           const PlotConfig& cfg) {
  std::error_code ec;
  std::filesystem::create_directories(cfg.plotDir, ec);
  if (ec) {
    edm::LogWarning("TrackerPhase2PlotUtil")
        << "cannot create plot directory '" << cfg.plotDir << "': " << ec.message() << "; no plots written";
    return 0;
  }

  StyleGuard styleGuard;
  gStyle->SetOptStat(0);

  int nWritten = 0;
  for (dqm::harvesting::MonitorElement* me : igetter.getAllContents(folder)) {
    if (me == nullptr)
      continue;

    bool is2D = false;
    switch (me->kind()) {
      case dqm::harvesting::MonitorElement::Kind::TH1F:
      case dqm::harvesting::MonitorElement::Kind::TH1D:
      case dqm::harvesting::MonitorElement::Kind::TPROFILE:
        break;
      case dqm::harvesting::MonitorElement::Kind::TH2F:
      case dqm::harvesting::MonitorElement::Kind::TH2D:
      case dqm::harvesting::MonitorElement::Kind::TPROFILE2D:
        is2D = true;
        break;
      default:
        continue;
    }
    TH1* stored = me->getTH1();
    if (stored == nullptr || stored->GetEntries() == 0)
      continue;

    std::unique_ptr<TH1> h(static_cast<TH1*>(stored->Clone()));
    h->SetDirectory(nullptr);

    const std::string& name = me->getName();
    const bool isOccMap = is2D && !cfg.occMapNamePrefix.empty() && name.rfind(cfg.occMapNamePrefix, 0) == 0;

    if (isOccMap) {
      gStyle->SetPalette(kRainBow);
      h->SetMinimum(0.);
      h->SetMaximum(cfg.zMax);
    } else {
      gStyle->SetPalette(kViridis);
    }

    const int nx = h->GetNbinsX();
    const bool wide = isOccMap && nx > kWideMinBins;
    const int width = wide ? std::clamp(nx * kWidePxPerBin + kWidePadPx, kWideMinPx, kWideMaxPx) : kStdWidthPx;
    const int height = wide ? kWideHeightPx : kStdHeightPx;

    TCanvas canvas(("c_" + name).c_str(), name.c_str(), width, height);
    if (isOccMap) {
      canvas.SetLeftMargin(0.075);
      canvas.SetRightMargin(0.115);
      canvas.SetTopMargin(0.13);
      canvas.SetBottomMargin(0.16);
    } else {
      canvas.SetLeftMargin(0.11);
      canvas.SetRightMargin(is2D ? 0.135 : 0.05);
      canvas.SetTopMargin(0.10);
      canvas.SetBottomMargin(0.13);
    }

    if (is2D) {
      if (isOccMap && h->GetXaxis()->GetLabels() != nullptr) {
        h->GetXaxis()->SetLabelSize(0.040);
        h->GetXaxis()->LabelsOption("h");
      }
      h->Draw("COLZ");
    } else if (me->kind() == dqm::harvesting::MonitorElement::Kind::TPROFILE) {
      h->SetLineWidth(2);
      h->SetMarkerStyle(20);
      h->SetMarkerSize(0.9);
      h->Draw("E1");
    } else {
      h->SetLineWidth(2);
      h->SetFillColorAlpha(kAzure + 1, 0.35);
      h->Draw("HIST");
    }

    for (const auto& fmt : cfg.formats) {
      if (fmt.empty())
        continue;
      canvas.SaveAs((cfg.plotDir + "/" + name + "." + fmt).c_str());
      ++nWritten;
    }
  }
  return nWritten;
}
