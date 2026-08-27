#include <memory>
#include <vector>

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

#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DataFormats/FEDRawData/interface/SLinkRocketHeaders.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/DTCELinkId.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"

#include "EventFilter/Phase2TrackerRawToDigi/interface/SensorHybrid.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2TrackerSpecifications.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include <fstream>

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
    data_ptr[word_index * 4 + 0] = (hex_word >> 24) & 0xFF;  // Most significant byte (bits 31-24)
    data_ptr[word_index * 4 + 1] = (hex_word >> 16) & 0xFF;  // Next byte (bits 23-16)
    data_ptr[word_index * 4 + 2] = (hex_word >> 8) & 0xFF;   // Next byte (bits 15-8)
    data_ptr[word_index * 4 + 3] = (hex_word >> 0) & 0xFF;   // Least significant byte (bits 7-0)
  }
};
   
ClusterToRawProducer::ClusterToRawProducer(const edm::ParameterSet& iConfig)
    : clusterCollectionToken_(
          consumes<Phase2TrackerCluster1DCollectionNew>(iConfig.getParameter<edm::InputTag>("Phase2Clusters"))),
      cablingMapToken_(esConsumes()),
      trackerGeometryToken_(esConsumes<TrackerGeometry, TrackerDigiGeometryRecord>()) {
  produces<RawDataBuffer>();
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

  constexpr size_t slink_header_size = sizeof(SLinkRocketHeader_v3);
  constexpr size_t slink_trailer_size = sizeof(SLinkRocketTrailer_v3);

  // prepare a vector to contain all the slink fragments
  struct SlinkFragment {
    uint64_t source_id;
    std::vector<unsigned char> data;
  };
  std::vector<SlinkFragment> allSlinkFragments;
  allSlinkFragments.reserve((MAX_DTC_ID - MIN_DTC_ID + 1) * (MAX_SLINK_ID + 1));

  size_t totalSize = 0;

  for (int dtc_id = MIN_DTC_ID; dtc_id < MAX_DTC_ID + 1; dtc_id++) {
    for (int slink_id = 0; slink_id < MAX_SLINK_ID + 1; slink_id++) {
      int index_first = slink_id * MODULES_PER_SLINK;
      int index_last = (slink_id + 1) * MODULES_PER_SLINK;

      std::vector<Word32Bits> daq_packet;
      std::vector<Word32Bits> offset_map(CICs_PER_SLINK / 2, Word32Bits(0));

      // Compute the source ID (equivalent to the old FED ID)
      uint64_t source_id = static_cast<uint64_t>(slink_id + SLINKS_PER_DTC * (dtc_id - 1) + TRACKER_HEADER);

      bool is_2S_module = false;
      bool is_unknown_type = false;

      // add the tracker header
      Word32Bits header_first_word;
      try {
        auto link_to_module_association = cablingMap.dtcELinkIdToDetId(DTCELinkId(dtc_id, index_first, 0));
        const DetId& det_id = link_to_module_association->second;
        is_2S_module = (trackerGeometry.getDetectorType(det_id) == TrackerGeometry::ModuleType::Ph2SS);
        header_first_word = Word32Bits(
            (is_2S_module ? MODULE_TYPE_2S : MODULE_TYPE_PS) << (N_BITS_PER_WORD - MODULE_TYPE_BITS)
        );
      } catch (const cms::Exception& e) {
        is_unknown_type = true;
        header_first_word = Word32Bits(DTC_DAQ_HEADER);
      }      
      daq_packet.reserve(HEADER_N_LINES);
      daq_packet.push_back(header_first_word);
      for (int i = 0; i < HEADER_N_LINES - 1; ++i) {
        daq_packet.push_back(Word32Bits(DTC_DAQ_HEADER));
      }
      
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
          uint16_t hybrid_1_offset = offset_in_32b_words;
          offset_in_32b_words += hybrid_1.get_payload_size();

          uint16_t hybrid_2_offset = offset_in_32b_words;
          offset_in_32b_words += hybrid_2.get_payload_size();

          // 24 is PSS, 23 is PSP, 26 is SS-SS
          uint32_t combined_offsets = (static_cast<uint32_t>(hybrid_2_offset) << 16) | hybrid_1_offset;
          offset_map[module_id_within_slink] = Word32Bits(combined_offsets);

          // Figure out Payload
          hybrid_1.get_payload(payload);
          hybrid_2.get_payload(payload);
        } catch (const cms::Exception& e) {
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

      // Add the offset map to the daq_packet
      for (std::size_t i = 0; i < offset_map.size(); i++) {
        daq_packet.push_back(offset_map[i]);
      }
      // add two reserved lines after the offset section as in the OT data format doc
      for (int i = 0; i < RESERVED_N_LINES; ++i) {
        daq_packet.push_back(Word32Bits(0));
      }

      // Pad payload to 128b 
      size_t payload_data_bytes = payload.size() * N_BYTES_PER_WORD;
      size_t padded_payload_bytes = (payload_data_bytes + 15) & ~static_cast<size_t>(15);  // pad to 16-byte boundary, required by RawDataBuffer
      size_t padding_bytes = padded_payload_bytes - payload_data_bytes;
      size_t padding_words = padding_bytes / N_BYTES_PER_WORD;
      for (size_t i = 0; i < padding_words; ++i) {
        payload.push_back(Word32Bits(0));
      }
        
      // Add the payload to the daq_packet
      for (std::size_t i = 0; i < payload.size(); i++) {
        daq_packet.push_back(payload[i]);
      }

      // Add the tracker trailer
      daq_packet.reserve(TRAILER_N_LINES);
      Word32Bits trailer_first_word;
      if (!is_unknown_type)
        trailer_first_word = Word32Bits(
            (is_2S_module ? MODULE_TYPE_2S : MODULE_TYPE_PS) << (N_BITS_PER_WORD - MODULE_TYPE_BITS)
        );
      else
        trailer_first_word = Word32Bits(0);
      daq_packet.push_back(trailer_first_word);
      for (int i = 0; i < TRAILER_N_LINES - 1; ++i) {
        daq_packet.push_back(Word32Bits(DTC_DAQ_HEADER));
      }

      // compute the overall fragment size = S-Link header + DAQ data + S-Link trailer
      size_t daq_data_bytes = daq_packet.size() * N_BYTES_PER_WORD;
      size_t fragment_bytes = slink_header_size + daq_data_bytes + slink_trailer_size;

      std::vector<unsigned char> slink_bytes(fragment_bytes, 0);
      unsigned char* buffer = slink_bytes.data();

      // S-Link header first
      // following vars are tmp set as in 
      // https://github.com/cms-sw/cmssw/blob/2f70a0116630c1586a2a26ffb4b7d256bb8f4b36/DataFormats/FEDRawData/test/TestWriteRawDataBuffer.cc#L36
      uint16_t l1a_types = 1;  //set provisionally to 1, to be revised later
      uint8_t l1a_phys = 0;
      uint8_t emu_status = 2;  //set 2 indicating fragment generated by DTH (emulator)
      new ((void*)buffer) SLinkRocketHeader_v3(
          source_id, l1a_types, l1a_phys, emu_status, static_cast<uint64_t>(eventId_));

      // insert tracker data
      unsigned char* daq_data_ptr = buffer + slink_header_size;
      for (size_t word_index = 0; word_index < daq_packet.size(); ++word_index) {
        insertHexWordAt(daq_data_ptr, word_index, (daq_packet[word_index].to_ulong()));
      }

      // S-Link trailer at the end
      // following vars are tmp set as in 
      // https://github.com/cms-sw/cmssw/blob/2f70a0116630c1586a2a26ffb4b7d256bb8f4b36/DataFormats/FEDRawData/test/TestWriteRawDataBuffer.cc#L36
      uint16_t slt_status = 0;  
      uint16_t crc = 0;
      uint32_t orbit_id = 0x3989;
      uint16_t bx_id = 2200;
        //set 2 indicating fragment generated by DTH (emulator)
      new ((void*)(buffer + slink_header_size + daq_data_bytes)) SLinkRocketTrailer_v3(
           slt_status, crc, orbit_id, bx_id, fragment_bytes >> SLR_WORD_NUM_BYTES_SHIFT, 0);

      totalSize += fragment_bytes;  
      allSlinkFragments.push_back({source_id, std::move(slink_bytes)});
    }
  }

  // pad again total size to 16byte boundary (not sure if needed)
  size_t paddedTotalSize = (totalSize + 15) & ~static_cast<size_t>(15);

  // Create the RawDataBuffer and add each slink fragment as a source
  auto rawDataBuffer = std::make_unique<RawDataBuffer>(paddedTotalSize);

  for (auto& fragment : allSlinkFragments) {
    rawDataBuffer->addSource(fragment.source_id, fragment.data.data(), fragment.data.size());
  }

  iEvent.put(std::move(rawDataBuffer));
}

DEFINE_FWK_MODULE(ClusterToRawProducer);
