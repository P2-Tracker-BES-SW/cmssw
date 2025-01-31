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
#include "EventFilter/Phase2TrackerRawToDigi/plugins/constants.h"
#include "DataFormats/FEDRawData/interface/FEDNumbering.h"

uint64_t readLittleEndian(const char* data, size_t size) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        value |= (static_cast<uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i));
    }
    return value;
}

class DTHDAQToFEDRawDataConverter : public edm::one::EDProducer<> {
public:
    explicit DTHDAQToFEDRawDataConverter(const edm::ParameterSet&);
    ~DTHDAQToFEDRawDataConverter() override = default;

    void produce(edm::Event&, const edm::EventSetup&) override;

private:
    std::string inputFile_;
    unsigned int fedId_;

    std::vector<char> readRawFile(const std::string& inputFile);
    void parseAndDumpEventData(const std::vector<char>& buffer, edm::Event& event, FEDRawDataCollection& fedRawDataCollection);
    void reverseParseFragments(const std::vector<char>& buffer, size_t startIdx, uint32_t packetWordCount, uint16_t eventCount, unsigned int orbitIdx, FEDRawDataCollection& fedRawDataCollection);
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

void DTHDAQToFEDRawDataConverter::produce(edm::Event& event, const edm::EventSetup&) {
    std::vector<char> buffer;
    try {
        buffer = readRawFile(inputFile_);
    } catch (const cms::Exception& e) {
        edm::LogError("DTHDAQToFEDRawDataConverter") << e.what();
        return;
    }

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Raw data read with size: " << buffer.size() << " bytes from input file: " << inputFile_;

    // Create a single FEDRawDataCollection instance for this event
    auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();

    // Parse and process the buffer
    parseAndDumpEventData(buffer, event, *fedRawDataCollection);

    // Store the final collection in the event
    event.put(std::move(fedRawDataCollection));

    // Print the first 64 bytes for debugging
    printHex(buffer, 64);
}

void DTHDAQToFEDRawDataConverter::parseAndDumpEventData(const std::vector<char>& buffer, edm::Event& event, FEDRawDataCollection& fedRawDataCollection) {
    size_t startIdx = 0;

    for (unsigned int orbitIdx = 0; orbitIdx < orbitCount; ++orbitIdx) {
        edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Parsing Orbit " << orbitIdx + 1;

        // Validate buffer size for Orbit Header
        if (buffer.size() - startIdx < orbitHeaderSize) {
            edm::LogError("DTHDAQToFEDRawDataConverter") << "Insufficient data for Orbit Header in Orbit " << orbitIdx + 1;
            break; // Or handle accordingly
        }

        // Parse Orbit Header
        uint8_t markerH = static_cast<uint8_t>(buffer[startIdx]);
        uint8_t markerO = static_cast<uint8_t>(buffer[startIdx + 1]);

        // Validate Orbit Header markers ('H' 'O')
        if (markerH != orbitHeaderMarkerH || markerO != orbitHeaderMarkerO) {
            edm::LogError("DTHDAQToFEDRawDataConverter") << "Invalid Orbit Header marker in Orbit " << orbitIdx + 1
                << ": 0x" << std::hex << static_cast<int>(markerH)
                << " 0x" << static_cast<int>(markerO) << std::dec;
            break; // Or handle accordingly
        }

        startIdx += 2;

        // Extract Orbit Header Fields
        uint16_t version = static_cast<uint16_t>(readLittleEndian(&buffer[startIdx], orbitVersionSize));
        startIdx += orbitVersionSize;

        uint32_t sourceId = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], sourceIdSize));
        startIdx += sourceIdSize;

        uint32_t runNumber = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], runNumberSize));
        startIdx += runNumberSize;

        uint32_t orbitNumber = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], orbitNumberSize));
        startIdx += orbitNumberSize;

        uint32_t eventCountReserved = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], eventCountResSize));
        uint16_t eventCount = eventCountReserved & 0xFFF; // Extract 12-bit event count
        startIdx += eventCountResSize;

        uint32_t packetWordCount = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], packetWordCountSize));
        startIdx += packetWordCountSize;

        uint32_t flags = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], flagsSize));
        startIdx += flagsSize;

        uint32_t checksum = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], checksumSize));
        startIdx += checksumSize;

        edm::LogInfo("DTHDAQToFEDRawDataConverter")
            << "Orbit " << orbitIdx + 1 << ": Version=" << version
            << ", SourceID=" << sourceId
            << ", RunNumber=" << runNumber
            << ", OrbitNumber=" << orbitNumber
            << ", EventCount=" << eventCount
            << ", PacketWordCount=" << packetWordCount
            << ", Flags=" << flags
            << ", Checksum=" << checksum;

        // Iterate over each fragment within the orbit
        for (unsigned int fragIdx = 0; fragIdx < eventCount; ++fragIdx) {
            edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Parsing Fragment " << fragIdx + 1;

            // Parse Payload
            if (buffer.size() - startIdx < fragmentPayloadWordSize) {
                edm::LogError("DTHDAQToFEDRawDataConverter") << "Insufficient data for Fragment Payload " << fragIdx + 1
                    << " in Orbit " << orbitIdx + 1;
                break; // Or handle accordingly
            }

            size_t payloadStart = startIdx;
            size_t payloadSizeBytes = fragmentPayloadWordSize; // 16 bytes
            startIdx += payloadSizeBytes;

            // Parse Fragment Trailer
            if (buffer.size() - startIdx < fragmentTrailerSize) {
                edm::LogError("DTHDAQToFEDRawDataConverter") << "Insufficient data for Fragment Trailer " << fragIdx + 1
                    << " in Orbit " << orbitIdx + 1;
                break; // Or handle accordingly
            }

            uint8_t markerH_trailer = static_cast<uint8_t>(buffer[startIdx]);
            uint8_t markerF_trailer = static_cast<uint8_t>(buffer[startIdx + 1]);

            // Validate trailer markers ('H' 'F')
            if (markerH_trailer != fragmentTrailerMarkerH || markerF_trailer != fragmentTrailerMarkerF) {
                edm::LogError("DTHDAQToFEDRawDataConverter") << "Invalid Fragment Trailer marker in Orbit " << orbitIdx + 1
                    << ", Fragment " << fragIdx + 1 << ": 0x" << std::hex << static_cast<int>(markerH_trailer)
                    << " 0x" << static_cast<int>(markerF_trailer) << std::dec;
                break; // Or handle accordingly
            }

            // Extract Fragment Trailer Fields
            uint16_t fragFlags = static_cast<uint16_t>(readLittleEndian(&buffer[startIdx + 2], fragFlagSize));
            uint32_t fragSize = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx + 4], fragSizeSize));
            uint64_t eventId = readLittleEndian(&buffer[startIdx + 8], 6) & 0xFFFFFFFFFFF; // 44 bits
            uint16_t crc = static_cast<uint16_t>(readLittleEndian(&buffer[startIdx + 14], 2));
            startIdx += fragmentTrailerSize;

            edm::LogInfo("DTHDAQToFEDRawDataConverter")
                << "Orbit " << orbitIdx + 1 << ", Fragment " << fragIdx + 1
                << ": Flags=" << fragFlags
                << ", FragSize=" << fragSize
                << ", EventID=" << eventId
                << ", CRC=" << crc;

            // Validate fragSize (assuming fragSize corresponds to the number of 128-bit words)
            if (fragSize != 1) { // Adjust this condition based on actual fragSize expectations
                edm::LogWarning("DTHDAQToFEDRawDataConverter")
                    << "Unexpected fragSize (" << fragSize << ") in Orbit " << orbitIdx + 1
                    << ", Fragment " << fragIdx + 1;
                // Handle accordingly
            }

            // Store Payload into FEDRawDataCollection
            FEDRawData& fedData = fedRawDataCollection.FEDData(fedId_);
            fedData.resize(payloadSizeBytes);
            std::copy(buffer.begin() + payloadStart, buffer.begin() + payloadStart + payloadSizeBytes, fedData.data());

            edm::LogInfo("DTHDAQToFEDRawDataConverter")
                << "Orbit " << orbitIdx + 1 << ", Fragment " << fragIdx + 1
                << " Payload stored for FED ID: " << fedId_
                << " with size: " << payloadSizeBytes << " bytes";
        }
    }




}
DEFINE_FWK_MODULE(DTHDAQToFEDRawDataConverter);
