# Open Ephys GUI directory-specific file

# add files in this folder
add_sources(
  open-ephys
  OpenEphysHttpServer.h
  HttpServerLifecycle.h
  AcquisitionRecordingRequest.h
  AcquisitionRecordingControl.h
  AcquisitionRecordingControl.cpp
  AcquisitionRecordingRuntime.h
  AcquisitionRecordingRuntime.cpp
  AcquisitionRecordingStatus.h
  AcquisitionRecordingStatus.cpp
  ControlCapability.h
  ControlCapability.cpp
  ControlCapabilityJson.h
  ControlCapabilityJson.cpp
  ControlDispatch.h
  ControlRead.h
  ControlStatus.h
  ControlStatusJson.h
  ControlStatusJson.cpp
  MessageThreadCall.h
  RecordingOptionsControl.h
  StatusControl.h
  StatusControl.cpp
  StatusHttpAdapter.h
  StatusHttpAdapter.cpp
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
