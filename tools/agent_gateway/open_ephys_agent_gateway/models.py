from enum import Enum


class Mode(str, Enum):
    IDLE = "IDLE"
    ACQUIRE = "ACQUIRE"
    RECORD = "RECORD"
    UNKNOWN = "UNKNOWN"


class RequestState(str, Enum):
    PENDING = "PENDING"
    ACTIVE = "ACTIVE"
    COMPLETED = "COMPLETED"
    REJECTED = "REJECTED"
