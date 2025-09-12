// EDProducer that takes bitstream to FEDRawData (Packer)

#include <utility>
#include <unordered_map>
#include <string>
#include <iostream>
#include <iomanip>
#include <array>
#include <cstring>
#include <algorithm>
#include <bitset>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "Geometry/CommonDetUnit/interface/PixelGeomDetUnit.h"

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/DTCELinkId.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDHeader.h"
#include "DataFormats/FEDRawData/interface/FEDTrailer.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"

using namespace Phase2DAQFormatSpecification;

class BitStreamToRawProducer : public edm::one::EDProducer<edm::one::WatchRuns> {
public:
  explicit BitStreamToRawProducer(const edm::ParameterSet&);
  ~BitStreamToRawProducer() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  void beginRun(const edm::Run&, const edm::EventSetup&) override;
  void endRun(const edm::Run&, const edm::EventSetup&) override {}
  void produce(edm::Event&, const edm::EventSetup&) override;

private:
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
  const edm::EDGetTokenT<edm::DetSetVector<Phase2ITChipBitStream>> ITChipBitStreamToken_;

  void addWordToBuffer(unsigned char* buffer, size_t position, uint32_t word);
  void addWordToBitVector(std::vector<bool>& vec, uint32_t word);
  void padToChunkBoundary(std::vector<bool>& vec);
  uint32_t calculateChipOffset(const std::vector<bool>& dataBlock);
  std::string getBitString(const std::vector<bool>& bits, size_t start, size_t len);
  const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;

  std::vector<std::pair<unsigned int, unsigned int>> knownDTCIdsWithIndex_;
  std::unordered_map<unsigned int, std::vector<uint32_t>> dtcIdToDetIds_;
};

BitStreamToRawProducer::BitStreamToRawProducer(const edm::ParameterSet& iConfig)
    : cablingMapToken_(
          esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
      ITChipBitStreamToken_(consumes<edm::DetSetVector<Phase2ITChipBitStream>>(
          iConfig.getParameter<edm::InputTag>("Phase2ITChipBitStream"))) {
  produces<FEDRawDataCollection>();
}

void BitStreamToRawProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("Phase2ITChipBitStream", edm::InputTag("PixelQCoreProducer"));
  descriptions.add("pixelToRawProducer", desc);
}

void BitStreamToRawProducer::beginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) {
  cablingMap_ = &iSetup.getData(cablingMapToken_);
  knownDTCIdsWithIndex_ = cablingMap_->getKnownDTCIdsWithIndex();
  dtcIdToDetIds_.clear();

  for (const auto& pair : knownDTCIdsWithIndex_) {
    unsigned int dtcId = pair.second;
    dtcIdToDetIds_[dtcId] = cablingMap_->getAllDetIdsForDTCId(dtcId);
  }
}
std::string BitStreamToRawProducer::getBitString(const std::vector<bool>& bits, size_t start, size_t len) {
  std::string result;
  for (size_t i = 0; i < len && start + i < bits.size(); i++) {
    result += (bits[start + i] ? "1" : "0");
    if ((i + 1) % 8 == 0)
      result += " ";
  }
  return result;
}

void BitStreamToRawProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  using namespace edm;
  using namespace std;

  const auto& cablingMap = iSetup.getData(cablingMapToken_);
  auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();
  edm::Handle<edm::DetSetVector<Phase2ITChipBitStream>> handle;
  iEvent.getByToken(ITChipBitStreamToken_, handle);

  if (!handle.isValid()) {
    throw cms::Exception("BitStreamToRawProducer") << "Invalid BitStream handle";
  }

  // Loop over all DTCs
  for (const auto& pair : knownDTCIdsWithIndex_) {
    unsigned int dtcIndex = pair.first;
    unsigned int dtcId = pair.second;

    // Get all detector IDs for this DTC
    auto& det_ids = dtcIdToDetIds_[dtcId];
    int total_det_ids = det_ids.size();

    // Calculate modules per slink - distribute evenly
    int base_modules_per_slink = total_det_ids / SLINKS_PER_DTC;
    int remaining_modules = total_det_ids % SLINKS_PER_DTC;

    int moduleIndex = 0;

    // Loop over all SLinks
    for (int slink_id = 0; slink_id < SLINKS_PER_DTC; slink_id++) {
      int global_slink_id = dtcIndex * SLINKS_PER_DTC + slink_id;

      // Calculate how many modules should be assigned to this SLink
      int modules_for_this_slink = base_modules_per_slink + ((slink_id < remaining_modules) ? 1 : 0);

      if (modules_for_this_slink == 0) {
        continue;
      }

      // Data structure for each SLink (offset block and data block)
      std::vector<bool> offsetBlock;
      std::vector<bool> dataBlock;

      // Process each module assigned to this SLink
      for (int mod_idx = 0; mod_idx < modules_for_this_slink; mod_idx++) {
        uint32_t det_id = det_ids[moduleIndex++];

        auto found_det_id = handle->find(det_id);
        if (found_det_id == handle->end()) {
          throw cms::Exception("BitstreamToRawProducer") << "Could not find detId from the inputs";
        }

        const edm::DetSet<Phase2ITChipBitStream>& detSet = *found_det_id;

        // Process each chip in this module
        int chipId = 0;
        for (auto const& chip : detSet) {
          // Make sure dataBlock is aligned to 128-bit boundary before adding a new chip
          size_t oldSize = dataBlock.size();
          padToChunkBoundary(dataBlock);
          size_t newSize = dataBlock.size();

          // Calculate the offset for this chip (word position in the data block)
          uint32_t chipOffset = calculateChipOffset(dataBlock);

          addWordToBitVector(offsetBlock, chipOffset & 0xFFFFFFFF);

          std::vector<bool> chipBitstream = chip.get_bitstream();
          unsigned int bitstreamSize = chipBitstream.size();

          // Calculate padding needed to align to 128-bit boundary
          unsigned int total_chip_size = 2 * BITS_PER_WORD + bitstreamSize;  // 2 headers + bitstream
          unsigned int padding_needed = (BITS_PER_CHUNK - (total_chip_size % BITS_PER_CHUNK)) % BITS_PER_CHUNK;

          // Add chip header 1 (marker + padding info)
          uint32_t header1 = CHIP_HEADER_MARKER | (padding_needed & 0xF);
          addWordToBitVector(dataBlock, header1);

          // Add chip header 2 (bitstream size)
          addWordToBitVector(dataBlock, bitstreamSize);

          dataBlock.insert(dataBlock.end(), chipBitstream.begin(), chipBitstream.end());

          // Add padding to align to 128-bit boundary
          if (padding_needed > 0) {
            dataBlock.insert(dataBlock.end(), padding_needed, false);
          }

          chipId++;
        }
      }

      // Ensure offset block is padded to 128-bit boundary
      size_t oldSize = offsetBlock.size();
      padToChunkBoundary(offsetBlock);
      size_t newSize = offsetBlock.size();

      // Calculate sizes in bytes
      unsigned int header_size = HEADER_TRAILER_LINES * BYTES_PER_WORD;
      unsigned int offset_size = offsetBlock.size() / BITS_PER_WORD * BYTES_PER_WORD;
      unsigned int data_size = dataBlock.size() / BITS_PER_WORD * BYTES_PER_WORD;
      unsigned int trailer_size = HEADER_TRAILER_LINES * BYTES_PER_WORD;
      unsigned int total_size = header_size + offset_size + data_size + trailer_size;

      // Create FEDRawData for this SLink
      FEDRawData& slink_data = fedRawDataCollection->FEDData(global_slink_id);
      slink_data.resize(total_size);
      unsigned char* buffer = slink_data.data();

      // Add header (4 lines of 0xFFFFFFFF)
      for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
        addWordToBuffer(buffer, i, HEADER_TRAILER_PATTERN);
      }

      // Add offset block
      unsigned int offset_words = offsetBlock.size() / BITS_PER_WORD;
      for (unsigned int i = 0; i < offset_words; i++) {
        // Extract 32-bit word from offset block
        uint32_t word = 0;
        for (int bit = 0; bit < BITS_PER_WORD; bit++) {
          if (offsetBlock[i * BITS_PER_WORD + bit]) {
            word |= (1 << (31 - bit));
          }
        }
        addWordToBuffer(buffer, HEADER_TRAILER_LINES + i, word);
      }

      // Add data block
      unsigned int data_words = dataBlock.size() / BITS_PER_WORD;
      for (unsigned int i = 0; i < data_words; i++) {
        // Extract 32-bit word from data block
        uint32_t word = 0;
        for (int bit = 0; bit < BITS_PER_WORD; bit++) {
          if (dataBlock[i * BITS_PER_WORD + bit]) {
            word |= (1 << (31 - bit));
          }
        }
        addWordToBuffer(buffer, HEADER_TRAILER_LINES + offset_words + i, word);
      }

      // Add trailer (4 lines of 0xFFFFFFFF)
      for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
        addWordToBuffer(buffer, HEADER_TRAILER_LINES + offset_words + data_words + i, HEADER_TRAILER_PATTERN);
      }
    }
  }

  iEvent.put(std::move(fedRawDataCollection));
}

void BitStreamToRawProducer::addWordToBuffer(unsigned char* buffer, size_t position, uint32_t word) {
  // Add 32-bit word to the buffer at the specified position
  buffer[position * BYTES_PER_WORD] = (word >> 16) & 0xFFFF;  // MSB
  buffer[position * BYTES_PER_WORD + 1] = word         & 0xFFFF;  // LSB
}

void BitStreamToRawProducer::addWordToBitVector(std::vector<bool>& vec, uint32_t word) {
  // Track the starting position for debugging
  size_t startPos = vec.size();

  // Convert two 32-bit words to 32 bits and add to the vector
  for (int bit = 31; bit >= 0; bit--) {
    bool bitValue = (word >> bit) & 1;
    vec.push_back(bitValue);
  }

}

void BitStreamToRawProducer::padToChunkBoundary(std::vector<bool>& vec) {
  // Add padding to align to 128-bit boundary if needed
  if (!vec.empty() && vec.size() % BITS_PER_CHUNK != 0) {
    size_t padding_needed = BITS_PER_CHUNK - (vec.size() % BITS_PER_CHUNK);
    vec.insert(vec.end(), padding_needed, false);
  }
}

uint32_t BitStreamToRawProducer::calculateChipOffset(const std::vector<bool>& dataBlock) {
  // Calculate the word offset where this chip's data will start
  return dataBlock.size() / BITS_PER_WORD;
}

DEFINE_FWK_MODULE(BitStreamToRawProducer);
