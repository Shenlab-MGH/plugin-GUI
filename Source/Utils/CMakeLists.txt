# Open Ephys GUI directory-specific file

# add files in this folder
add_sources(
  open-ephys
  OpenEphysHttpServer.h
  ControlCapability.h
  ControlCapability.cpp
  ControlCapabilityJson.h
  ControlCapabilityJson.cpp
  ConfigSnapshotApiHandler.h
  ControlRead.h
  ControlStatus.h
  ControlStatusJson.h
  ControlStatusJson.cpp
  MessageThreadCall.h
  ProcessorInventoryApiHandler.h
  RecordingOptionsControl.h
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
