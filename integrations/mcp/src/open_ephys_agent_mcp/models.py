"""Strict models for the native fork protocol."""

from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class NativeStatus(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)

    schema_version: Literal["oe-agent-control-preview/v0.0.1"]
    backend: Literal["in-process-agent-endpoint"]
    session_id: str = Field(min_length=1, max_length=128, pattern=r"^[A-Za-z0-9._:-]+$")
    online: bool
    mutation_allowed: bool
    mutation_disabled_reason: str = Field(min_length=1, max_length=128)
    phase: Literal["DETACHED", "READY", "STOPPED"]
    gui_version: str = Field(min_length=1, max_length=64)
    mode: Literal["IDLE", "ACQUIRE", "RECORD", "UNKNOWN"]
    revision: int = Field(ge=0)
