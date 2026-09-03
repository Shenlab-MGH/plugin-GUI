# Open Ephys GUI directory-specific file

# add files in this folder
add_sources(
  open-ephys
  OpenEphysHttpServer.h
  OpenEphysHttpApiRoutes.h
  ControlCapability.h
  ControlCapability.cpp
  ControlCapabilityJson.h
  ControlCapabilityJson.cpp
  ConfigSnapshotApiHandler.h
  ControlStatus.h
  ControlStatusJson.h
  ControlStatusJson.cpp
  ControlRead.h
  MessageThreadCall.h
  NeuropixelsPresetApiHandler.h
  NeuropixelsPresetApiHandler.cpp
  ProcessorInventoryApiHandler.h
  RecordingOptionsControl.h
  StatusControl.h
  StatusApiHandler.h
  StatusApiHandler.cpp
  ListSliceParser.h
  ListSliceParser.cpp
  BroadcastParser.h
  BroadcastParser.cpp
  BroadcastPayload.h
  BroadcastPayload.cpp
  Utils.h
  Utils.cpp
)
# add nested directories
