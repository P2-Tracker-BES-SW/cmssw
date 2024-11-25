#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "FWCore/Utilities/interface/Exception.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <iomanip>
#include <cstdint>

// Constants for bit field widths, markers, and sizes
constexpr unsigned int orbitHeaderSize = 32;
constexpr unsigned int fragmentTrailerSize = 16;
constexpr unsigned int fragmentPayloadWordSize = 16;  // Each fragment payload word is 16 bytes
constexpr uint8_t orbitHeaderMarkerH = 0x48;
constexpr uint8_t orbitHeaderMarkerO = 0x4F;
constexpr uint8_t fragmentTrailerMarkerF = 0x48;
constexpr uint8_t fragmentTrailerMarkerH = 0x46;

class DTHDAQToFEDRawDataConverter : public edm::stream::EDProducer<> {
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
  return buffer;
}

void DTHDAQToFEDRawDataConverter::printHex(const std::vector<char>& buffer, size_t length) {
  std::cout << "Raw bitstream (first " << length << " bytes): ";
  for (size_t i = 0; i < std::min(buffer.size(), length); ++i) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << (static_cast<unsigned int>(static_cast<unsigned char>(buffer[i]))) << " ";
  }
  std::cout << std::dec << std::endl;
}

void DTHDAQToFEDRawDataConverter::parseAndDumpEventData(const std::vector<char>& buffer) {
  size_t orbitSize = buffer.size() / 4;  // Divide buffer for 4 orbits
  for (int orbitIdx = 0; orbitIdx < 4; ++orbitIdx) {
    size_t startIdx = orbitIdx * orbitSize;
    std::cout << "\nParsing Orbit " << orbitIdx + 1 << ":\n";

    if (buffer.size() - startIdx < orbitHeaderSize) {
      std::cerr << "Insufficient data for Orbit Header in Orbit " << orbitIdx + 1 << std::endl;
      return;
    }

    uint8_t markerH = static_cast<uint8_t>(buffer[startIdx++]);
    uint8_t markerO = static_cast<uint8_t>(buffer[startIdx++]);
    if (markerH != orbitHeaderMarkerH || markerO != orbitHeaderMarkerO) {
      std::cerr << "Invalid Orbit Header marker in Orbit " << orbitIdx + 1 << ": 0x"
                << std::hex << static_cast<int>(markerH) << " 0x" << static_cast<int>(markerO) << std::dec << std::endl;
      return;
    }
    std::cout << "Orbit Header Marker: 0x" << std::hex << static_cast<int>(markerH) << " 0x" << static_cast<int>(markerO) << std::dec << std::endl;

    // Read additional fields as before...
    // Reverse parse fragments within this orbit
    reverseParseFragments(buffer, startIdx, packetWordCount, eventCount);
  }
}

void DTHDAQToFEDRawDataConverter::reverseParseFragments(const std::vector<char>& buffer, size_t startIdx, uint32_t packetWordCount, uint16_t eventCount) {
  size_t index = startIdx + (packetWordCount * fragmentPayloadWordSize);  

  std::cout << "Starting reverse parsing from byte offset: " << index << std::endl;

  for (int frag = eventCount - 1; frag >= 0; --frag) {
    if (index < fragmentTrailerSize) {
      std::cerr << "Not enough data for fragment trailer of fragment " << frag + 1 << std::endl;
      return;
    }
    index -= fragmentTrailerSize;

    uint8_t markerF = static_cast<uint8_t>(buffer[index]);
    uint8_t markerH = static_cast<uint8_t>(buffer[index + 1]);
    if (markerF != fragmentTrailerMarkerF || markerH != fragmentTrailerMarkerH) {
      std::cerr << "Invalid Fragment Trailer marker in fragment " << frag + 1 << ": 0x"
                << std::hex << static_cast<int>(markerF) << " 0x" << static_cast<int>(markerH) << std::dec << std::endl;
      return;
    }

    // Process fragment as before...
    std::cout << "Finished reverse parsing of all fragments in the orbit." << std::endl;
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

  std::unique_ptr<FEDRawDataCollection> fedRawDataCollection(new FEDRawDataCollection());
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
