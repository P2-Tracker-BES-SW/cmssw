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
    void reverseParseFragments(const std::vector<char>& buffer, size_t startIdx, uint32_t packetWordCount, uint16_t eventCount, edm::Event& event, FEDRawDataCollection& fedRawDataCollection);
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
    size_t orbitSize = buffer.size() / orbitCount;
    for (unsigned int orbitIdx = 0; orbitIdx < orbitCount; ++orbitIdx) {
        size_t startIdx = orbitIdx * orbitSize;
        edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Parsing Orbit " << orbitIdx + 1;

        if (buffer.size() - startIdx < orbitHeaderSize) {
            edm::LogError("DTHDAQToFEDRawDataConverter")
                << "Insufficient data for Orbit Header in Orbit " << orbitIdx + 1;
            return;
        }

        uint32_t eventCount = 10;  // Replace with actual parsed value
        uint32_t packetWordCount = 128; // Replace with actual parsed value

        reverseParseFragments(buffer, startIdx, packetWordCount, eventCount, event, fedRawDataCollection);
    }
}

void DTHDAQToFEDRawDataConverter::reverseParseFragments(const std::vector<char>& buffer, size_t startIdx, uint32_t packetWordCount, uint16_t eventCount, edm::Event& event, FEDRawDataCollection& fedRawDataCollection) {
    size_t index = startIdx + (packetWordCount * fragmentPayloadWordSize);

    for (int frag = eventCount - 1; frag >= 0; --frag) {
        if (index < fragmentTrailerSize) {
            edm::LogError("DTHDAQToFEDRawDataConverter")
                << "Not enough data for fragment trailer of fragment " << frag + 1;
            return;
        }
        index -= fragmentTrailerSize;

        uint32_t fragSize = 128;  // Replace with actual parsed value
        size_t payloadSizeBytes = fragSize * 16 / 128;
        if (index < payloadSizeBytes) {
            edm::LogError("DTHDAQToFEDRawDataConverter")
                << "Not enough data for the payload of fragment " << frag + 1;
            return;
        }
        index -= payloadSizeBytes;

        edm::LogInfo("DTHDAQToFEDRawDataConverter")
            << "Fragment " << frag + 1 << " Payload starts at byte offset: " << index;

        FEDRawData& fedData = fedRawDataCollection.FEDData(fedId_);
        fedData.resize(payloadSizeBytes);
        std::copy(buffer.begin() + index, buffer.begin() + index + payloadSizeBytes, fedData.data());

        edm::LogInfo("DTHDAQToFEDRawDataConverter")
            << "FEDRawData created for fragment " << frag + 1 << " with size: " 
            << payloadSizeBytes << " bytes for FED ID: " << fedId_;
    }
}

DEFINE_FWK_MODULE(DTHDAQToFEDRawDataConverter);
