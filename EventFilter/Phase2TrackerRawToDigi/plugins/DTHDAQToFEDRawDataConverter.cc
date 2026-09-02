// A utility to read and parse a .raw orbit aggregation file from the DTH,
// and convert all event fragments belonging to the same event ID into one
// FEDRawDataCollection per CMSSW event.
// By Alaa Adel Abdelhamid, May 2025
// Modified June 2026
//
// Important:
//   The outer DTH Orbit Header has a source ID.
//   The inner SLinkRocket fragment header also has a source ID.
//   For FEDRawDataCollection::FEDData(...), use the inner SLinkRocket source ID,
//   not the outer DTH Orbit Header source ID.

#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/Run.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/SLinkRocketHeaders.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include <fstream>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <sstream>
#include <algorithm>

// Include the constants for bit field widths, markers, and size in BYTES:
#include "EventFilter/Phase2TrackerRawToDigi/interface/DTHOrbitFieldSizes.h"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Helper for endianness.
// The DTH data in these files is little-endian at the byte-field level.
uint64_t readLittleEndian(const char* data, size_t size) {
  uint64_t value = 0;
  for (size_t i = 0; i < size; ++i) {
    value |= (static_cast<uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i));
  }
  return value;
}

// -----------------------------------------------------------------------------
// Represents a single fragment belonging to one event.
// -----------------------------------------------------------------------------

struct FragmentData {
  unsigned int orbitIdx = 0;
  uint32_t runNumber = 0;
  uint32_t orbitNumber = 0;

  // Source ID from the OUTER DTH Orbit Header.
  // Example from SourceID0005.raw: this is 5.
  // Do NOT use this as the FEDRawDataCollection index.
  uint32_t dthOrbitSourceId = 0;

  // Source ID from the INNER SLinkRocket fragment header.
  // This is the correct FEDRawDataCollection index.
  // Example from the same SourceID0005.raw fragment: this is 0.
  uint32_t fedSourceId = 0;

  uint16_t fragFlags = 0;
  uint32_t fragSize = 0;
  uint64_t eventId = 0;
  uint16_t crc = 0;

  // The actual binary payload for this fragment.
  // This should include the SLinkRocket header and SLinkRocket trailer.
  std::vector<char> payloadBytes;
};

// -----------------------------------------------------------------------------
// Producer
// -----------------------------------------------------------------------------

class DTHDAQToFEDRawDataConverter : public edm::one::EDProducer<> {
public:
  explicit DTHDAQToFEDRawDataConverter(const edm::ParameterSet& config);
  ~DTHDAQToFEDRawDataConverter() override = default;

  void beginJob() override;
  void produce(edm::Event& event, const edm::EventSetup&) override;

private:
  std::string inputFile_;

  // Store all fragments grouped by eventId.
  std::unordered_map<uint64_t, std::vector<FragmentData>> eventIdToFragments_;
  std::vector<uint64_t> eventInsertionOrder_;
  size_t currentEventIndex_ = 0;

  std::vector<char> readRawFile(const std::string& inputFile);
  void parseAllOrbitsAndFragments(const std::vector<char>& buffer);
  void printHex(const std::vector<char>& buffer, size_t maxLength);
};

DTHDAQToFEDRawDataConverter::DTHDAQToFEDRawDataConverter(const edm::ParameterSet& config)
    : inputFile_(config.getParameter<std::string>("inputFile")) {
  produces<FEDRawDataCollection>();
}

void DTHDAQToFEDRawDataConverter::beginJob() {
  edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Reading raw file: " << inputFile_;

  std::vector<char> buffer = readRawFile(inputFile_);
  parseAllOrbitsAndFragments(buffer);

  edm::LogInfo("DTHDAQToFEDRawDataConverter")
      << "Total unique eventIds found: " << eventIdToFragments_.size();
}

void DTHDAQToFEDRawDataConverter::produce(edm::Event& event, const edm::EventSetup&) {
  if (currentEventIndex_ >= eventInsertionOrder_.size()) {
    edm::LogWarning("DTHDAQToFEDRawDataConverter") << "No more event groups to produce.";
    return;
  }

  const uint64_t eventId = eventInsertionOrder_[currentEventIndex_];
  const auto& fragments = eventIdToFragments_.at(eventId);

  edm::LogInfo("DTHDAQToFEDRawDataConverter")
      << "Producing CMSSW event for eventId=" << eventId
      << " with " << fragments.size() << " fragments.";

  auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();

  for (const auto& frag : fragments) {
    // Correct: use the INNER SLinkRocket source ID as the FED collection index.
    //
    // Wrong old behavior:
    //   FEDData(frag.dthOrbitSourceId)
    //
    // Correct behavior:
    //   FEDData(frag.fedSourceId)
    FEDRawData& fedData = fedRawDataCollection->FEDData(frag.fedSourceId);

    if (fedData.size() != 0) {
      edm::LogWarning("DTHDAQToFEDRawDataConverter")
          << "FEDRawDataCollection already has data for fedSourceId="
          << frag.fedSourceId
          << " in eventId=" << eventId
          << ". Existing payload will be overwritten. "
          << "Existing size=" << fedData.size()
          << ", new size=" << frag.payloadBytes.size();
    }

    fedData.resize(frag.payloadBytes.size());
    std::copy(frag.payloadBytes.begin(), frag.payloadBytes.end(), fedData.data());

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Filled FEDRawDataCollection slot fedSourceId=" << frag.fedSourceId
        << " using fragment from DTH orbit sourceId=" << frag.dthOrbitSourceId
        << ", eventId=" << frag.eventId
        << ", payloadBytes=" << frag.payloadBytes.size();
  }

  event.put(std::move(fedRawDataCollection));
  ++currentEventIndex_;
}

std::vector<char> DTHDAQToFEDRawDataConverter::readRawFile(const std::string& inputFile) {
  std::ifstream rawFile(inputFile, std::ios::binary | std::ios::ate);
  if (!rawFile.is_open()) {
    throw cms::Exception("FileOpenError") << "Could not open input file: " << inputFile;
  }

  const std::streamsize fileSize = rawFile.tellg();
  rawFile.seekg(0, std::ios::beg);

  std::vector<char> buffer(fileSize);
  if (!rawFile.read(buffer.data(), fileSize)) {
    throw cms::Exception("FileReadError") << "Could not read input file: " << inputFile;
  }

  rawFile.close();
  return buffer;
}

void DTHDAQToFEDRawDataConverter::printHex(const std::vector<char>& buffer, size_t maxLength) {
  std::ostringstream hexOutput;
  hexOutput << "Raw bitstream up to " << maxLength << " bytes: ";

  const size_t length = std::min(buffer.size(), maxLength);
  for (size_t i = 0; i < length; ++i) {
    hexOutput << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned int>(static_cast<unsigned char>(buffer[i])) << " ";
  }

  edm::LogInfo("DTHDAQToFEDRawDataConverter") << hexOutput.str();
}

// -----------------------------------------------------------------------------
// Parse entire .raw file buffer into fragments grouped by eventId.
// -----------------------------------------------------------------------------

void DTHDAQToFEDRawDataConverter::parseAllOrbitsAndFragments(const std::vector<char>& buffer) {
  size_t startIdx = 0;
  unsigned int orbitIdx = 0;

  while (startIdx < buffer.size()) {
    if (buffer.size() - startIdx < orbitHeaderSize) {
      edm::LogWarning("DTHDAQToFEDRawDataConverter")
          << "Remaining buffer is smaller than orbitHeaderSize. Stopping parse. "
          << "remaining=" << (buffer.size() - startIdx)
          << ", orbitHeaderSize=" << orbitHeaderSize;
      break;
    }

    if (static_cast<unsigned char>(buffer[startIdx]) != static_cast<unsigned char>(orbitHeaderMarkerH) ||
        static_cast<unsigned char>(buffer[startIdx + 1]) != static_cast<unsigned char>(orbitHeaderMarkerO)) {
      edm::LogWarning("DTHDAQToFEDRawDataConverter")
          << "Orbit header marker not found at byte offset " << startIdx
          << ". Got bytes 0x"
          << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<unsigned int>(static_cast<unsigned char>(buffer[startIdx]))
          << " 0x"
          << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<unsigned int>(static_cast<unsigned char>(buffer[startIdx + 1]))
          << ". Stopping parse.";
      break;
    }

    // Orbit Header:
    //   marker bytes
    //   version
    //   source ID
    //   run number
    //   orbit number
    //   event count / reserved or lumi/event field depending format constants
    //   packet word count
    //   flags
    //   checksum
    startIdx += 2;

    const uint16_t version = static_cast<uint16_t>(
        readLittleEndian(&buffer[startIdx], orbitVersionSize));
    startIdx += orbitVersionSize;

    const uint32_t dthOrbitSourceId = static_cast<uint32_t>(
        readLittleEndian(&buffer[startIdx], sourceIdSize));
    startIdx += sourceIdSize;

    const uint32_t runNumber = static_cast<uint32_t>(
        readLittleEndian(&buffer[startIdx], runNumberSize));
    startIdx += runNumberSize;

    const uint32_t orbitNumber = static_cast<uint32_t>(
        readLittleEndian(&buffer[startIdx], orbitNumberSize));
    startIdx += orbitNumberSize;

    const uint32_t eventCountReserved = static_cast<uint32_t>(
        readLittleEndian(&buffer[startIdx], eventCountResSize));
    const uint16_t eventCount = static_cast<uint16_t>(eventCountReserved & 0xFFF);
    startIdx += eventCountResSize;

    const uint32_t packetWordCount = static_cast<uint32_t>(
        readLittleEndian(&buffer[startIdx], packetWordCountSize));
    startIdx += packetWordCountSize;

    const uint32_t flags = static_cast<uint32_t>(
        readLittleEndian(&buffer[startIdx], flagsSize));
    startIdx += flagsSize;

    const uint32_t checksum = static_cast<uint32_t>(
        readLittleEndian(&buffer[startIdx], checksumSize));
    startIdx += checksumSize;

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Orbit " << (orbitIdx + 1)
        << ": Version=" << version
        << ", DTHOrbitSourceID=" << dthOrbitSourceId
        << ", RunNumber=" << runNumber
        << ", OrbitNumber=" << orbitNumber
        << ", EventCount=" << eventCount
        << ", PacketWordCount=" << packetWordCount
        << ", Flags=" << flags
        << ", Checksum=" << checksum;

    const size_t orbitDataSizeBytes =
        static_cast<size_t>(packetWordCount) * fragmentPayloadWordSize - orbitHeaderSize;

    const size_t orbitDataEnd = startIdx + orbitDataSizeBytes;
    if (orbitDataEnd > buffer.size()) {
      edm::LogWarning("DTHDAQToFEDRawDataConverter")
          << "Orbit data extends beyond file buffer. Stopping parse. "
          << "orbitDataEnd=" << orbitDataEnd
          << ", buffer.size()=" << buffer.size();
      break;
    }

    // Fragment trailers are at the end of the orbit payload, so walk backwards.
    size_t currentPos = orbitDataEnd;

    // Move startIdx to next orbit before decoding fragments.
    startIdx += orbitDataSizeBytes;

    for (unsigned int fragIdx = 0; fragIdx < eventCount; ++fragIdx) {
      if (currentPos < fragmentTrailerSize) {
        edm::LogWarning("DTHDAQToFEDRawDataConverter")
            << "currentPos < fragmentTrailerSize while parsing orbit "
            << (orbitIdx + 1)
            << ", fragIdx=" << fragIdx
            << ". Stopping fragment parse for this orbit.";
        break;
      }

      const size_t trailerPos = currentPos - fragmentTrailerSize;

      if (static_cast<unsigned char>(buffer[trailerPos]) !=
              static_cast<unsigned char>(fragmentTrailerMarkerT) ||
          static_cast<unsigned char>(buffer[trailerPos + 1]) !=
              static_cast<unsigned char>(fragmentTrailerMarkerF)) {
        edm::LogWarning("DTHDAQToFEDRawDataConverter")
            << "Fragment trailer marker not found at byte offset " << trailerPos
            << " while parsing orbit " << (orbitIdx + 1)
            << ", fragIdx=" << fragIdx
            << ". Got bytes 0x"
            << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned int>(static_cast<unsigned char>(buffer[trailerPos]))
            << " 0x"
            << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned int>(static_cast<unsigned char>(buffer[trailerPos + 1]))
            << ". Stopping fragment parse for this orbit.";
        break;
      }

      const uint16_t fragFlags = static_cast<uint16_t>(
          readLittleEndian(&buffer[trailerPos + fragFlagSize], fragFlagSize));

      const uint32_t fragSize = static_cast<uint32_t>(
          readLittleEndian(&buffer[trailerPos + fragSizeSize], fragSizeSize));

      const uint64_t eventId =
          readLittleEndian(&buffer[trailerPos + trailerOffsetEventId], eventIdSize) & eventIdMask;

      const uint16_t crc = static_cast<uint16_t>(
          readLittleEndian(&buffer[trailerPos + trailerOffsetCRC], crcSize));

      const size_t payloadSizeBytes =
          static_cast<size_t>(fragSize) * fragmentPayloadWordSize;

      if (trailerPos < payloadSizeBytes) {
        edm::LogWarning("DTHDAQToFEDRawDataConverter")
            << "Fragment payload would start before beginning of buffer. "
            << "orbit=" << (orbitIdx + 1)
            << ", fragIdx=" << fragIdx
            << ", trailerPos=" << trailerPos
            << ", payloadSizeBytes=" << payloadSizeBytes
            << ". Stopping fragment parse for this orbit.";
        break;
      }

      const size_t payloadStart = trailerPos - payloadSizeBytes;

      FragmentData frag;
      frag.orbitIdx = orbitIdx + 1;
      frag.runNumber = runNumber;
      frag.orbitNumber = orbitNumber;
      frag.dthOrbitSourceId = dthOrbitSourceId;
      frag.fragFlags = fragFlags;
      frag.fragSize = fragSize;
      frag.eventId = eventId;
      frag.crc = crc;

      frag.payloadBytes.assign(
          buffer.begin() + payloadStart,
          buffer.begin() + payloadStart + payloadSizeBytes);

      // Verify this is a SLinkRocket fragment and extract the correct FED source ID.
      const size_t minimumSLinkRocketFragmentSize = sizeof(SLinkRocketHeader_v3) + sizeof(SLinkRocketTrailer_v3);
      if (frag.payloadBytes.size() < minimumSLinkRocketFragmentSize) {
        throw cms::Exception("InvalidSLinkRocketFragment")
            << "Fragment payload is too small to contain both a SLinkRocket header and trailer. "
            << "payloadBytes.size() = " << frag.payloadBytes.size()
            << ", required at least " << minimumSLinkRocketFragmentSize;
      }

      auto slinkHeader = makeSLinkRocketHeaderView(frag.payloadBytes.data());
      if (!slinkHeader->verifyMarker()) {
        throw cms::Exception("InvalidSLinkRocketHeader") << "Invalid SLinkRocket BOE marker.";
      }

      const char* slinkTrailerData =
          frag.payloadBytes.data() + frag.payloadBytes.size() - sizeof(SLinkRocketTrailer_v3);
      auto slinkTrailer = makeSLinkRocketTrailerView(slinkTrailerData, slinkHeader->version());
      if (!slinkTrailer->verifyMarker()) {
        throw cms::Exception("InvalidSLinkRocketTrailer") << "Invalid SLinkRocket EOE marker.";
      }

      frag.fedSourceId = slinkHeader->sourceID();

      edm::LogInfo("DTHDAQToFEDRawDataConverter")
          << "Parsed fragment: "
          << "orbitIdx=" << frag.orbitIdx
          << ", eventId=" << frag.eventId
          << ", DTHOrbitSourceID=" << frag.dthOrbitSourceId
          << ", FEDSourceIDFromSLinkRocketHeader=" << frag.fedSourceId
          << ", fragFlags=" << frag.fragFlags
          << ", fragSizeWords=" << frag.fragSize
          << ", payloadBytes=" << frag.payloadBytes.size()
          << ", crc=" << frag.crc;

      if (eventIdToFragments_.find(eventId) == eventIdToFragments_.end()) {
        eventInsertionOrder_.push_back(eventId);
      }

      eventIdToFragments_[eventId].emplace_back(std::move(frag));

      // Move backwards to the previous fragment payload/trailer.
      currentPos = payloadStart;
    }

    ++orbitIdx;
  }
}

DEFINE_FWK_MODULE(DTHDAQToFEDRawDataConverter);
