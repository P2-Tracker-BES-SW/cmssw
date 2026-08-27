#ifndef Validation_SiTrackerPhase2V_TrackerPhase2PlotUtil_H
#define Validation_SiTrackerPhase2V_TrackerPhase2PlotUtil_H

// Renders the MonitorElements of one DQM folder to image files

#include <string>
#include <vector>

#include "DQMServices/Core/interface/DQMStore.h"

namespace TrackerPhase2PlotUtil {

  struct PlotConfig {
    std::string plotDir = ".";
    std::vector<std::string> formats = {"png", "pdf"};
    // MEs starting with 'occMapNamePrefix' prefix gets the rendering
    std::string occMapNamePrefix;
    double zMax = 1.2;
  };

  int saveFolderPlots(dqm::harvesting::DQMStore::IGetter& igetter, const std::string& folder, const PlotConfig& cfg);

}  // namespace TrackerPhase2PlotUtil

#endif  // Validation_SiTrackerPhase2V_TrackerPhase2PlotUtil_H
