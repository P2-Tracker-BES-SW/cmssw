#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <iomanip>
#include <cstdint>


// Constants for bit field widths, markers, and sizes
constexpr unsigned int orbitCount = 4;
constexpr unsigned int orbitHeaderSize = 32;
constexpr unsigned int fragmentTrailerSize = 16;
constexpr unsigned int fragmentPayloadWordSize = 16;  // Each fragment payload word is 16 bytes
constexpr unsigned int orbitVersionSize = 2;  // The version field size is 2 bytes
constexpr unsigned int sourceIdSize = 4;  // The source ID  field is 4 bytes
constexpr unsigned int runNumberSize = 4;  // The run number field is 4 bytes
constexpr unsigned int orbitNumberSize = 4;  // The orbit number field is 4 bytes
constexpr unsigned int eventCountResSize = 4;  // The event count reserved field is 4 bytes
constexpr unsigned int packetWordCountSize = 4;  // The packet word count  field is 4 bytes
constexpr unsigned int flagsSize = 4;  // The flag field is 4 bytes
constexpr unsigned int checksumSize = 4;  // The checksum field is 4 bytes
constexpr unsigned int fragFlagSize = 2;  // The fragment flag field is 2 bytes
constexpr unsigned int fragSizeSize = 4;  // The fragment flag field is 4 bytes
constexpr uint8_t orbitHeaderMarkerH = 0x48;
constexpr uint8_t orbitHeaderMarkerO = 0x4F;
constexpr uint8_t fragmentTrailerMarkerH = 0x48;
constexpr uint8_t fragmentTrailerMarkerF = 0x46;

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
  return buffer;
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
    
    // Ensure enough space for the Orbit Header
    if (buffer.size() - startIdx < orbitHeaderSize) {
      edm::LogError("DTHDAQToFEDRawDataConverter")
                   << "Insufficient data for Orbit Header in Orbit " << orbitIdx + 1;
      return;
    }

    // Parse Orbit Header (similar to original code, but starting from startIdx)
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

    // Read version, source ID, run number, orbit number, etc.
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
    uint32_t runNumber = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], runNumberSize));
    startIdx += runNumberSize;
    uint32_t orbitNumber = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], orbitNumberSize));
    startIdx += orbitNumberSize;

    uint32_t eventCountReserved = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], eventCountResSize));
    uint16_t eventCount = eventCountReserved & 0xFFF;
    startIdx += eventCountResSize;
    uint32_t packetWordCount = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], packetWordCountSize));
    startIdx += packetWordCountSize;
    uint32_t flags = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], flagsSize));
    startIdx += flagsSize;
    uint32_t checksum = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], checksumSize));
    startIdx += checksumSize;

    edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Version: " << version << "\nSource ID: " << sourceId << "\nRun Number: " << runNumber
              << "\nOrbit Number: " << orbitNumber << "\nEvent Count: " << eventCount
              << "\nPacket Word Count: " << packetWordCount << "\nFlags: " << flags
              << "\nChecksum: 0x" << std::hex << checksum << std::dec << "\n";

    // Reverse parse fragments within this orbit
    reverseParseFragments(buffer, startIdx, packetWordCount, eventCount);
  }
}

void DTHDAQToFEDRawDataConverter::reverseParseFragments(const std::vector<char>& buffer, size_t startIdx, uint32_t packetWordCount, uint16_t eventCount) {
  size_t bufferSize = buffer.size();

  size_t index = startIdx + (packetWordCount * fragmentPayloadWordSize);  // Calculate where the orbit payload ends

  edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Starting reverse parsing from byte offset: " << index << std::endl;

  auto readLittleEndian = [&](const char* data, size_t size) -> uint64_t {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
      value |= (static_cast<uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i));
    }
    return value;
  };

  for (int frag = eventCount - 1; frag >= 0; --frag) {
    if (index < fragmentTrailerSize) {
      std::cerr << "Not enough data for fragment trailer of fragment " << frag + 1 << std::endl;
      return;
    }
    index -= fragmentTrailerSize;

    uint8_t markerH = static_cast<uint8_t>(buffer[index]);
    uint8_t markerF = static_cast<uint8_t>(buffer[index + 1]);
    if (markerF != fragmentTrailerMarkerF || markerH != fragmentTrailerMarkerH) {
      std::cerr << "Invalid Fragment Trailer marker in fragment " << frag + 1 << ": 0x"
                << std::hex << static_cast<int>(markerF) << " 0x" << static_cast<int>(markerH) << std::dec << std::endl;
      return;
    }

   // uint16_t fragFlags = static_cast<uint16_t>(readLittleEndian(&buffer[index + fragFlagSize], fragFlagSize));
    uint32_t fragSize = static_cast<uint32_t>(readLittleEndian(&buffer[index + fragSizeSize], fragSizeSize));
   // uint64_t eventId = readLittleEndian(&buffer[index + 8], 8) & 0xFFFFFFFFFFF;
  //  uint16_t crc = static_cast<uint16_t>(readLittleEndian(&buffer[index + 14], 2));

    // edm::LogInfo("DTHDAQToFEDRawDataConverter")
        //       << "Version: " << version << " Source ID: " << sourceId;

    size_t payloadSizeBytes = fragSize * 16/128;
    if (index < payloadSizeBytes) {
      std::cerr << "Not enough data for the payload of fragment " << frag + 1 << std::endl;
      return;
    }
    index -= payloadSizeBytes;
    edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Fragment " << frag + 1 << " Payload starts at byte offset: " << index << std::endl;

    // Optionally, you could print the payload here
    edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Fragment " << frag + 1 << " Payload (first 16 bytes): ";
    for (size_t i = index; i < index + 16 && i < bufferSize; ++i) {
      edm::LogInfo("DTHDAQToFEDRawDataConverter") << std::hex << std::setw(2) << std::setfill('0')
                << (static_cast<unsigned int>(static_cast<unsigned char>(buffer[i]))) << " ";
    }
    edm::LogInfo("DTHDAQToFEDRawDataConverter") << std::dec << std::endl;
  }
  

  edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Finished reverse parsing of all fragments in the orbit." << std::endl;
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
