#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DataFormats/FEDRawData/interface/SLinkRocketHeaders.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2TrackerSpecifications.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"

#include <iostream>
#include <bitset>

using namespace Phase2TrackerSpecifications;

//==================================================================
// Analyzer to print the contents of the FEDRawDataCollection
// EDProduct, which contains the RAW binary data produced by
// the CMS detector.
//==================================================================

class RawAnalyzer : public edm::one::EDAnalyzer<> {
public:
  explicit RawAnalyzer(const edm::ParameterSet&);
  ~RawAnalyzer() override = default;

  void analyze(const edm::Event&, const edm::EventSetup&) override;
  void endJob() override;

private:
  const edm::EDGetTokenT<RawDataBuffer> rawDataBufferToken_;
};

RawAnalyzer::RawAnalyzer(const edm::ParameterSet& iConfig)
    : rawDataBufferToken_(consumes<RawDataBuffer>(iConfig.getParameter<edm::InputTag>("fedDataBuffer"))) {}

void RawAnalyzer::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) {

  const auto& rawDataBuffer = iEvent.get(rawDataBufferToken_);

  auto slink_header_size = sizeof(SLinkRocketHeader_v3);
  auto slink_trailer_size = sizeof(SLinkRocketTrailer_v3);

  unsigned int nDTCs = rawDataBuffer.data().size() / SLINKS_PER_DTC;
  std::cout << "Number of DTCs = " << std::dec << nDTCs << std::endl;

  for (unsigned int dtc_id = 0; dtc_id < nDTCs; dtc_id++) {
    // Read only the 0th FED position as per the producer logic
    std::vector<std::span<const unsigned char>> payloadDataVec(SLINKS_PER_DTC);

    for (unsigned int slink_id = 0; slink_id < SLINKS_PER_DTC; slink_id++) {
      auto const& fedData = rawDataBuffer.fragmentData(slink_id + SLINKS_PER_DTC * dtc_id);
      if (fedData.size() > 0) 
        payloadDataVec.push_back(fedData.payload(slink_header_size, slink_trailer_size));      
    }
    // ** Below is the logic to read out the 32bit words from the fedRawData object.

    // Determine the maximum size among all FEDRawData objects
    size_t maxWords = 0;
    for (const auto& payloadData : payloadDataVec) {
      maxWords = std::max(maxWords, payloadData.size() / 4);  // Divide by 4 to get 32-bit words
    }

    if (maxWords == 0)
      continue;  // No data for this DTC.

    std::cout << std::dec << "==================== " << iEvent.id() << " DTC ID: " << dtc_id
              << " =====================" << std::endl;

    // Prepare column headers
    std::cout << "       --------- SLink 0 ----------              --------- SLink 1 ---------              --------- "
                 "SLink 2 ----------              --------- SLink 3 ---------"
              << std::endl;

    // Loop through all rows (up to maxWords) and print each 32-bit word
    for (size_t row = 0; row < maxWords; ++row) {
      for (const auto& payloadData : payloadDataVec) {
        if (row * 4 < payloadData.size()) {
          const unsigned char* dataPtr = payloadData.data();
          uint32_t word =
              (static_cast<uint32_t>(dataPtr[row * 4]) << 24) | (static_cast<uint32_t>(dataPtr[row * 4 + 1]) << 16) |
              (static_cast<uint32_t>(dataPtr[row * 4 + 2]) << 8) | (static_cast<uint32_t>(dataPtr[row * 4 + 3]));

          // Print hexadecimal and binary representations
          std::cout << std::hex << std::setw(8) << std::setfill('0') << word << " " << std::bitset<32>(word) << " ";
        } else {
          // Print empty space for missing data in this column
          std::cout << "                 EMPTY                   ";
        }
      }
      std::cout << std::endl;  // Move to the next line after printing a row
    }
  }
}

void RawAnalyzer::endJob() {}

// Define this as a plug-in
DEFINE_FWK_MODULE(RawAnalyzer);
