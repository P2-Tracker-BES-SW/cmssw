#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include <fstream>
#include <vector>
#include <iomanip>
#include <cstdint>


// Constants for bit field widths, markers, and sizes
constexpr unsigned int orbitCount = 4;
constexpr unsigned int orbitHeaderSize = 32;
constexpr unsigned int fragmentTrailerSize = 16;
constexpr unsigned int fragmentPayloadWordSize = 16;
constexpr unsigned int orbitVersionSize = 2;
constexpr unsigned int sourceIdSize = 4;
constexpr unsigned int runNumberSize = 4;
constexpr unsigned int orbitNumberSize = 4;
constexpr unsigned int eventCountResSize = 4;
constexpr unsigned int packetWordCountSize = 4;
constexpr unsigned int flagsSize = 4;
constexpr unsigned int checksumSize = 4;
constexpr unsigned int fragFlagSize = 2;
constexpr unsigned int fragSizeSize = 4;
constexpr uint8_t orbitHeaderMarkerH = 0x48;
constexpr uint8_t orbitHeaderMarkerO = 0x4F;
constexpr uint8_t fragmentTrailerMarkerF = 0x48;
constexpr uint8_t fragmentTrailerMarkerH = 0x46;

class DTHDAQToFEDRawDataConverter : public edm::one::EDProducer<> {
public:
  explicit DTHDAQToFEDRawDataConverter(const edm::ParameterSet&);
  ~DTHDAQToFEDRawDataConverter() override = default;

  void produce(edm::Event&, const edm::EventSetup&) override;

private:
  std::string inputFile_;
  unsigned int fedId_;

  std::vector<char> readRawFile(const std::string& inputFile);
  void parseAndDumpEventData(const std::vector<char>& buffer);
  void reverseParseFragments(const std::vector<char>& buffer, size_t startIdx, uint32_t packetWordCount, uint16_t eventCount);
  void printHex(const std::vector<char>& buffer, size_t length);
};

DTHDAQToFEDRawDataConverter::DTHDAQToFEDRawDataConverter(const edm::ParameterSet& config)
  : inputFile_(config.getParameter<std::string>("inputFile")),
    fedId_(config.getParameter<unsigned int>("fedId")) {
  produces<FEDRawDataCollection>();
}

std::vector<char> DTHDAQToFEDRawDataConverter::readRawFile(const std::string& inputFile) {
  std::ifstream rawFile(inputFile, std::ios::binary | std::ios::ate);
  if (!rawFile.is_open()) {
    throw cms::Exception("FileOpenError") << "Could not open input file: " << inputFile;
  }

  std::streamsize fileSize = rawFile.tellg();
  rawFile.seekg(0, std::ios::beg);

  std::vector<char> buffer(fileSize);
  if (!rawFile.read(buffer.data(), fileSize)) {
    throw cms::Exception("FileReadError") << "Could not read input file: " << inputFile;
  }

  rawFile.close();
  return buffer;  // Ensure move semantics
}

void DTHDAQToFEDRawDataConverter::printHex(const std::vector<char>& buffer, size_t length) {
  std::ostringstream hexOutput;
  hexOutput << "Raw bitstream (first " << length << " bytes): ";
  for (size_t i = 0; i < std::min(buffer.size(), length); ++i) {
    hexOutput << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned int>(static_cast<unsigned char>(buffer[i])) << " ";
  }
  edm::LogInfo("DTHDAQToFEDRawDataConverter") << hexOutput.str();
}

void DTHDAQToFEDRawDataConverter::parseAndDumpEventData(const std::vector<char>& buffer) {
  size_t orbitSize = buffer.size() / orbitCount;
  for (unsigned int orbitIdx = 0; orbitIdx < orbitCount; ++orbitIdx) {
    size_t startIdx = orbitIdx * orbitSize;
    edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Parsing Orbit " << orbitIdx + 1;

    if (buffer.size() - startIdx < orbitHeaderSize) {
      edm::LogError("DTHDAQToFEDRawDataConverter")
          << "Insufficient data for Orbit Header in Orbit " << orbitIdx + 1;
      return;
    }

    uint8_t markerH = static_cast<uint8_t>(buffer[startIdx++]);
    uint8_t markerO = static_cast<uint8_t>(buffer[startIdx++]);
    if (markerH != orbitHeaderMarkerH || markerO != orbitHeaderMarkerO) {
      edm::LogError("DTHDAQToFEDRawDataConverter")
          << "Invalid Orbit Header marker in Orbit " << orbitIdx + 1 << ": 0x"
          << std::hex << static_cast<int>(markerH) << " 0x" << static_cast<int>(markerO) << std::dec;
      return;
    }
    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Orbit Header Marker: 0x" << std::hex << static_cast<int>(markerH)
        << " 0x" << static_cast<int>(markerO) << std::dec;

    // Read key data fields
    auto readLittleEndian = [&](const char* data, size_t size) -> uint64_t {
      uint64_t value = 0;
      for (size_t i = 0; i < size; ++i) {
        value |= (static_cast<uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i));
      }
      return value;
    };

    uint16_t version = static_cast<uint16_t>(readLittleEndian(&buffer[startIdx], orbitVersionSize));
    startIdx += orbitVersionSize;
    uint32_t sourceId = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], sourceIdSize));
    startIdx += sourceIdSize;

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Version: " << version << " Source ID: " << sourceId;
  }
}

void DTHDAQToFEDRawDataConverter::produce(edm::Event& event, const edm::EventSetup&) {
  std::vector<char> buffer;
  try {
    buffer = readRawFile(inputFile_);
  } catch (const cms::Exception& e) {
    edm::LogError("DTHDAQToFEDRawDataConverter") << e.what();
    return;
  }

  auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();
  FEDRawData& fedData = fedRawDataCollection->FEDData(fedId_);
  fedData.resize(buffer.size());
  std::copy(buffer.begin(), buffer.end(), fedData.data());

  edm::LogInfo("DTHDAQToFEDRawDataConverter")
      << "FEDRawData created with size: " << fedData.size() << " bytes for FED ID: " << fedId_;

  printHex(buffer, 64);
  parseAndDumpEventData(buffer);

  event.put(std::move(fedRawDataCollection));
}

DEFINE_FWK_MODULE(DTHDAQToFEDRawDataConverter);
