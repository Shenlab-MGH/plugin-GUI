#Open Ephys GUI directory-specific file

#add files in this folder
add_sources(open-ephys 
	DataQueue.cpp
	DataQueue.h
	EventQueue.h
	RecordEngine.cpp
	RecordEngine.h
	RecordNode.cpp
	RecordNode.h
	RecordNodeRecordingStart.h
	RecordNodeRecordingStop.h
	RecordNodeEditor.cpp
	RecordNodeEditor.h
	RecordThread.cpp
	RecordThreadCleanStop.h
	RecordThread.h
)

#add nested directories
add_subdirectory(BinaryFormat)
add_subdirectory(DiskMonitor)
