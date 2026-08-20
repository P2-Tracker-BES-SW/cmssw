#ifndef SENSOR_HYBRID_H
#define SENSOR_HYBRID_H

#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2TrackerSpecifications.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "FWCore/Utilities/interface/Exception.h"

#include <bitset>

class SensorHybrid {
private:
  void set_sensor_type(const DetId& det_id, const TrackerGeometry& trackerGeometry, const int internal_id);
  std::vector<Phase2TrackerCluster1D*> get_clusters_on_cic(edmNew::DetSetVector<Phase2TrackerCluster1D>::const_iterator clusterIterator);
  void get_channel_cluster_payload(std::vector<Phase2DAQFormatSpecification::Word32Bits>& payload);
  
  unsigned int get_number_of_strip_clusters();
  unsigned int get_number_of_pixel_clusters();

  bool cic_id_;
  unsigned int cic_boundary_in_z_;

  std::vector<Phase2TrackerCluster1D*> sensor_1_clusters_;
  TrackerGeometry::ModuleType sensor_type_1;

  std::vector<Phase2TrackerCluster1D*> sensor_2_clusters_;
  TrackerGeometry::ModuleType sensor_type_2;

  unsigned int offset_index_;
  unsigned int eventId_;

public:
  SensorHybrid(const DetId& det_id,
               edmNew::DetSetVector<Phase2TrackerCluster1D>::const_iterator sensor_1,
               edmNew::DetSetVector<Phase2TrackerCluster1D>::const_iterator sensor_2,
               const edmNew::DetSetVector<Phase2TrackerCluster1D>::const_iterator nullIter,
               const bool cic_id,
               const TrackerGeometry& trackerGeometry,
               const unsigned int eventId);

  unsigned int get_payload_size();
  unsigned int get_offset_within_payload() const;
  const bool get_cic_id() const { return cic_id_; }
  unsigned int get_module_slink_id() const;
  unsigned int get_module_slink_mod_id() const;
  std::vector<Phase2TrackerCluster1D*> get_sensor_1_clusters() const { return sensor_1_clusters_; }
  std::vector<Phase2TrackerCluster1D*> get_sensor_2_clusters() const { return sensor_2_clusters_; }
  TrackerGeometry::ModuleType get_sensor_type_1() const { return sensor_type_1; }
  TrackerGeometry::ModuleType get_sensor_type_2() const { return sensor_type_2; }

  void get_payload(std::vector<Phase2DAQFormatSpecification::Word32Bits>& payload);
};

#endif