// Constants for bit field widths, markers, and size in BYTES
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

