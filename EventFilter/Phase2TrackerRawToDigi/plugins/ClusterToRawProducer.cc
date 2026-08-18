#include <memory>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2TrackerDigi.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"

#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDHeader.h"
#include "DataFormats/FEDRawData/interface/FEDTrailer.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/DTCELinkId.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"

#include "EventFilter/Phase2TrackerRawToDigi/interface/SensorHybrid.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2TrackerSpecifications.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include <fstream>

#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"  // Keep for FEDHeader/FEDTrailer if needed

class ClusterToRawProducer : public edm::one::EDProducer<> {
public:
  explicit ClusterToRawProducer(const edm::ParameterSet&);
  ~ClusterToRawProducer() override;

private:
  void produce(edm::Event&, const edm::EventSetup&) override;

  edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> clusterCollectionToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
  const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> trackerGeometryToken_;

  void insertHexWordAt(unsigned char* data_ptr, size_t word_index, uint32_t hex_word) {
    // Reverse order within each 128-bit (4-word) block
    size_t block_start = (word_index / 4) * 4;
    size_t offset_within_block = word_index % 4;
    size_t remapped_index = block_start + (3 - offset_within_block);
    data_ptr[remapped_index * 4 + 0] = (hex_word >> 0) & 0xFF; // Most significant byte (bits 31-24)
    data_ptr[remapped_index * 4 + 1] = (hex_word >> 8) & 0xFF; // Next byte (bits 23-16)
    data_ptr[remapped_index * 4 + 2] = (hex_word >> 16) & 0xFF;  // Next byte (bits 15-8)
    data_ptr[remapped_index * 4 + 3] = (hex_word >> 24) & 0xFF;  // Least significant byte (bits 7-0)
  }

  uint32_t get32bWordAtLine(const unsigned char*& data, size_t LineID, bool debug);
  void dumpPacket(const unsigned char* data, size_t dataSize);
  void InspectDAQPayload(const std::vector<Phase2DAQFormatSpecification::Word32Bits>& DAQPayload);

};

ClusterToRawProducer::ClusterToRawProducer(const edm::ParameterSet& iConfig)
    : clusterCollectionToken_(
          consumes<Phase2TrackerCluster1DCollectionNew>(iConfig.getParameter<edm::InputTag>("Phase2Clusters"))),
      cablingMapToken_(esConsumes()),
      trackerGeometryToken_(esConsumes<TrackerGeometry, TrackerDigiGeometryRecord>()) {
  produces<RawDataBuffer>();;
}

ClusterToRawProducer::~ClusterToRawProducer() {}

void ClusterToRawProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  // Retrieve TrackerGeometry from EventSetup
  const TrackerGeometry& trackerGeometry = iSetup.getData(trackerGeometryToken_);

  // Retrieve the CablingMap
  const auto& cablingMap = iSetup.getData(cablingMapToken_);

  // get EventID and RunID
  unsigned int eventId_ = iEvent.id().event();

  // Get input clusters
  edm::Handle<Phase2TrackerCluster1DCollectionNew> clusters_handle;
  iEvent.getByToken(clusterCollectionToken_, clusters_handle);

  using namespace Phase2TrackerSpecifications;
  using namespace Phase2DAQFormatSpecification;

  // Create RawDataBuffer to store the output
  auto rawDataBuffer = std::make_unique<RawDataBuffer>(MAX_DTC_ID * SLINKS_PER_DTC);

  // for (int dtc_id = MIN_DTC_ID; dtc_id < MAX_DTC_ID + 1; dtc_id++) {
  //   for (int slink_id = 0; slink_id < MAX_SLINK_ID + 1; slink_id++) {
  for (int dtc_id = MIN_DTC_ID; dtc_id < 2; dtc_id++) {
    for (int slink_id = 0; slink_id < 1; slink_id++) {
      int index_first = slink_id * MODULES_PER_SLINK;
      int index_last = (slink_id + 1) * MODULES_PER_SLINK;

      //FEDRawData slink_daq_stream;

      std::vector<Word32Bits> daq_packet;
      std::vector<Word32Bits> offset_map(CICs_PER_SLINK / 2, Word32Bits(0));

      daq_packet.reserve(4);
      daq_packet.push_back(Word32Bits(0xC4200AA0));
      daq_packet.push_back(Word32Bits(0x0));
      daq_packet.push_back(Word32Bits(0x0));
      daq_packet.push_back(Word32Bits(0x0));
      std::vector<Word32Bits> payload;

      unsigned int offset_in_32b_words = 0;

      for (int module_id = index_first; module_id < index_last; module_id++) {
        const unsigned int module_id_within_slink = module_id - index_first;
        DTCELinkId cms_link_id = DTCELinkId(dtc_id, module_id, 0);
        try {
          auto link_to_det_association = cablingMap.dtcELinkIdToDetId(cms_link_id);
          const DetId& det_id = link_to_det_association->second;

          edmNew::DetSetVector<Phase2TrackerCluster1D>::const_iterator sensor_1_cluster_collection =
              clusters_handle->find(det_id + 1);
          edmNew::DetSetVector<Phase2TrackerCluster1D>::const_iterator sensor_2_cluster_collection =
              clusters_handle->find(det_id + 2);
          const edmNew::DetSetVector<Phase2TrackerCluster1D>::const_iterator nullIter = clusters_handle->end();

          // sensor_1_cic_0 and sensor_2_cic_0 form a single output daq channel.
          SensorHybrid hybrid_1(
              det_id, sensor_1_cluster_collection, sensor_2_cluster_collection, nullIter, false, trackerGeometry, eventId_);

          // // sensor_1_cic_1 and sensor_2_cic_1 form a single output daq channel.
          SensorHybrid hybrid_2(
              det_id, sensor_1_cluster_collection, sensor_2_cluster_collection, nullIter, true, trackerGeometry, eventId_);

          // sensor_2 is always isUpper == 1 for 2S.
          // sensor_2 is always isLower == 0 for 2S.

          // Figure Out Offsets
          offset_in_32b_words += hybrid_1.get_payload_size();
          uint16_t hybrid_1_offset = offset_in_32b_words;

          offset_in_32b_words += hybrid_2.get_payload_size();
          uint16_t hybrid_2_offset = offset_in_32b_words;

          // 24 is PSS, 23 is PSP, 26 is SS-SS
          uint32_t combined_offsets = (static_cast<uint32_t>(hybrid_1_offset) << 16) | hybrid_2_offset;
          offset_map[module_id_within_slink] = Word32Bits(combined_offsets);

          // Figure out Payload
          hybrid_1.get_payload(payload);
          hybrid_2.get_payload(payload);
          std::cout << "Here @ " << dtc_id << " & " << slink_id << std::endl;
        } 
        catch (const cms::Exception& e) {
          // exception here means that the link is not connected to a detector
          uint32_t eventID = eventId_ & L1ID_MAX_VALUE;  // eventId_ (9 bits)
          uint32_t channelErrors = 0;                    // 9 bits for errors, all set to 0
          uint32_t numClusters = 0;                      // no clusters here.

          // Build the channel header
          uint32_t header_ = (eventID << (N_BITS_PER_WORD - L1ID_BITS)) |
                             (channelErrors << (N_BITS_PER_WORD - L1ID_BITS - CIC_ERROR_BITS)) |
                             (numClusters << (N_BITS_PER_WORD - L1ID_BITS - CIC_ERROR_BITS - N_STRIP_CLUSTER_BITS)) |
                             (numClusters);

          uint16_t hybrid_1_offset = offset_in_32b_words;
          offset_in_32b_words += 1;

          uint16_t hybrid_2_offset = offset_in_32b_words;
          offset_in_32b_words += 1;

          uint32_t combined_offsets = (static_cast<uint32_t>(hybrid_2_offset) << 16) | hybrid_1_offset;
          offset_map[module_id_within_slink] = Word32Bits(combined_offsets);

          // Push the header into the payload
          payload.push_back(Word32Bits(header_));
          payload.push_back(Word32Bits(header_));

          // continue;
        }
      }

      // Add the offset map to the slink_daq_stream
      for (std::size_t i = 0; i < offset_map.size(); i++) {
        daq_packet.push_back(offset_map[i]);
      }

      daq_packet.push_back(0x0);
      daq_packet.push_back(0x0);

      // Add the payload to the slink_daq_stream
      for (std::size_t i = 0; i < payload.size(); i++) {
        daq_packet.push_back(payload[i]);
      }

      daq_packet.push_back(0x4C000000);
      daq_packet.push_back(0x0);
      daq_packet.push_back(0x0);
      daq_packet.push_back(0x0);

      InspectDAQPayload(daq_packet);

      size_t size_in_bytes = daq_packet.size() * N_BYTES_PER_WORD;
      size_t padding = (N_BYTES_PER_DTH_BINARY_WORD - (size_in_bytes % N_BYTES_PER_DTH_BINARY_WORD)) % N_BYTES_PER_DTH_BINARY_WORD;
      // Create a raw buffer for each stream
      std::vector<unsigned char> slink_daq_stream;
      slink_daq_stream.resize(size_in_bytes + padding, N_BYTES_PER_DTH_BINARY_WORD);
      unsigned char* data_ptr = slink_daq_stream.data();
      std::cout << slink_daq_stream.size() << std::endl;

      for (size_t word_index = 0; word_index < daq_packet.size(); ++word_index) {
        insertHexWordAt(data_ptr, word_index, (daq_packet[word_index].to_ulong()));
      }

      // Calculate source ID (similar to FED ID calculation)
      uint32_t sourceId = slink_id + SLINKS_PER_DTC * (dtc_id - 1) + TRACKER_HEADER;
      rawDataBuffer->addSource(sourceId, slink_daq_stream.data(), slink_daq_stream.size());
      // Note: For dumping, you'll need to adapt dumpPacket to work with vector<unsigned char>
      // or create a temporary FEDRawData object
      dumpPacket(slink_daq_stream.data(), slink_daq_stream.size());
    }
  }
  iEvent.put(std::move(rawDataBuffer));
}

/**
 * @brief Retrives a specific 32b word from the DAQ Payload.
 * @param data Pointer to the raw data buffer (passed by reference)
 * @param LineID Carefully defined in this google sheet
 * https://docs.google.com/spreadsheets/d/1RHZFqeHCoJhRaAfaKEO1Gx6U6c1Y3tRGhL_aSbZQROY/edit?gid=256168213#gid=256168213
 * @return 32bit word. MSB on the far left. LSB on the far right.
 */
uint32_t ClusterToRawProducer::get32bWordAtLine(const unsigned char*& data, size_t LineID, bool debug = false) {
    int group = LineID / 4;
    int offset = LineID % 4;
    int reversedOffset = 3 - offset;
    size_t reversedWordIndex = group * 4 + reversedOffset;
    
    size_t byteOffset = reversedWordIndex * 4;
    uint32_t word = (static_cast<uint32_t>(data[byteOffset + 3]) << 24) |
                    (static_cast<uint32_t>(data[byteOffset + 2]) << 16) |
                    (static_cast<uint32_t>(data[byteOffset + 1]) << 8)  |
                    (static_cast<uint32_t>(data[byteOffset + 0]));
    if (debug) {
        printf("%08X \n", (unsigned int)word);
    }            
    return word;
}

/**
 * @brief Dumps the entire DAQ Packet, in hexdump -C view.
 * @param data Pointer to the raw data buffer
 * @param dataSize Size of the data buffer in bytes
 * @return void
 */
void ClusterToRawProducer::dumpPacket(const unsigned char* data, size_t dataSize) {
    for (size_t l16byteslineID = 0; l16byteslineID < (dataSize + 15) / 16; l16byteslineID++) {
        for (size_t byte_within_line = 0; byte_within_line < 16; byte_within_line++) {
            size_t index = l16byteslineID * 16 + byte_within_line;
            if (index >= dataSize) break;  // Stop if we've printed all bytes
            printf("%02X ", (unsigned int)data[index]);
        }
        printf("\n");
    }
}

/**
 * @brief Dumps the entire DAQ Packet, in hexdump -C view.
 * @return void
 */
void ClusterToRawProducer::InspectDAQPayload(const std::vector<Phase2DAQFormatSpecification::Word32Bits>& DAQPayload) {
  for (std::size_t i = 0; i < DAQPayload.size(); i++) {
    printf("%08lX \n", (unsigned long int)DAQPayload.at(i).to_ulong());
  }
}

DEFINE_FWK_MODULE(ClusterToRawProducer);
