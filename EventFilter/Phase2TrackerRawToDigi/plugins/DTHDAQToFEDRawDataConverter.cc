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
    
    // Ensure enough space for the Orbit Header
    if (buffer.size() - startIdx < 32) {
      std::cerr << "Insufficient data for Orbit Header in Orbit " << orbitIdx + 1 << std::endl;
      return;
    }

    // Parse Orbit Header (similar to original code, but starting from startIdx)
    uint8_t markerH = static_cast<uint8_t>(buffer[startIdx++]);
    uint8_t markerO = static_cast<uint8_t>(buffer[startIdx++]);
    if (markerH != 0x48 || markerO != 0x4F) {
      std::cerr << "Invalid Orbit Header marker in Orbit " << orbitIdx + 1 << ": 0x"
                << std::hex << static_cast<int>(markerH) << " 0x" << static_cast<int>(markerO) << std::dec << std::endl;
      return;
    }
    std::cout << "Orbit Header Marker: 0x" << std::hex << static_cast<int>(markerH) << " 0x" << static_cast<int>(markerO) << std::dec << std::endl;

    // Read version, source ID, run number, orbit number, etc.
    auto readLittleEndian = [&](const char* data, size_t size) -> uint64_t {
      uint64_t value = 0;
      for (size_t i = 0; i < size; ++i) {
        value |= (static_cast<uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i));
      }
      return value;
    };

    uint16_t version = static_cast<uint16_t>(readLittleEndian(&buffer[startIdx], 2));
    startIdx += 2;
    uint32_t sourceId = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], 4));
    startIdx += 4;
    uint32_t runNumber = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], 4));
    startIdx += 4;
    uint32_t orbitNumber = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], 4));
    startIdx += 4;

    uint32_t eventCountReserved = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], 4));
    uint16_t eventCount = eventCountReserved & 0xFFF;
    startIdx += 4;
    uint32_t packetWordCount = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], 4));
    startIdx += 4;
    uint32_t flags = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], 4));
    startIdx += 4;
    uint32_t checksum = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], 4));
    startIdx += 4;

    std::cout << "Version: " << version << "\nSource ID: " << sourceId << "\nRun Number: " << runNumber
              << "\nOrbit Number: " << orbitNumber << "\nEvent Count: " << eventCount
              << "\nPacket Word Count: " << packetWordCount << "\nFlags: " << flags
              << "\nChecksum: 0x" << std::hex << checksum << std::dec << "\n";

    // Reverse parse fragments within this orbit
    reverseParseFragments(buffer, startIdx, packetWordCount, eventCount);
  }
}

void DTHDAQToFEDRawDataConverter::reverseParseFragments(const std::vector<char>& buffer, size_t startIdx, uint32_t packetWordCount, uint16_t eventCount) {
  size_t bufferSize = buffer.size();

  size_t index = startIdx + (packetWordCount * 16);  // Calculate where the orbit payload ends

  std::cout << "Starting reverse parsing from byte offset: " << index << std::endl;

  auto readLittleEndian = [&](const char* data, size_t size) -> uint64_t {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
      value |= (static_cast<uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i));
    }
    return value;
  };

  for (int frag = eventCount - 1; frag >= 0; --frag) {
    if (index < 16) {
      std::cerr << "Not enough data for fragment trailer of fragment " << frag + 1 << std::endl;
      return;
    }
    index -= 16;

    uint8_t markerF = static_cast<uint8_t>(buffer[index]);
    uint8_t markerH = static_cast<uint8_t>(buffer[index + 1]);
    if (markerF != 0x48 || markerH != 0x46) {
      std::cerr << "Invalid Fragment Trailer marker in fragment " << frag + 1 << ": 0x"
                << std::hex << static_cast<int>(markerF) << " 0x" << static_cast<int>(markerH) << std::dec << std::endl;
      return;
    }

    uint16_t fragFlags = static_cast<uint16_t>(readLittleEndian(&buffer[index + 2], 2));
    uint32_t fragSize = static_cast<uint32_t>(readLittleEndian(&buffer[index + 4], 4));
    uint64_t eventId = readLittleEndian(&buffer[index + 8], 8) & 0xFFFFFFFFFFF;
    uint16_t crc = static_cast<uint16_t>(readLittleEndian(&buffer[index + 14], 2));

    std::cout << "Fragment " << frag + 1 << " Flags: 0x" << std::hex << fragFlags << std::dec
              << "\nSize: " << fragSize << " 128-bit words\nEvent ID: " << eventId
              << "\nCRC: 0x" << std::hex << crc << std::dec << std::endl;

    size_t payloadSizeBytes = fragSize * 16/128;
    if (index < payloadSizeBytes) {
      std::cerr << "Not enough data for the payload of fragment " << frag + 1 << std::endl;
      return;
    }
    index -= payloadSizeBytes;
    std::cout << "Fragment " << frag + 1 << " Payload starts at byte offset: " << index << std::endl;

    // Optionally, you could print or process the payload here
    std::cout << "Fragment " << frag + 1 << " Payload (first 16 bytes): ";
    for (size_t i = index; i < index + 16 && i < bufferSize; ++i) {
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << (static_cast<unsigned int>(static_cast<unsigned char>(buffer[i]))) << " ";
    }
    std::cout << std::dec << std::endl;
  }
  

  std::cout << "Finished reverse parsing of all fragments in the orbit." << std::endl;
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
