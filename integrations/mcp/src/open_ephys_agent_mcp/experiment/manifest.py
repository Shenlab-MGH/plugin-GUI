"""Strict immutable manifest for the laboratory's eight-part experiment."""

from __future__ import annotations

import re
from datetime import datetime
from pathlib import PureWindowsPath
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator


SCHEMA_VERSION = "oe-agent-run-manifest/v0.0.2"
DEFAULT_PARTS = (
    (1, "All Shanks 1-96", 1, 96),
    (2, "All Shanks 97-192", 97, 192),
    (3, "All Shanks 193-288", 193, 288),
    (4, "All Shanks 289-384", 289, 384),
    (5, "All Shanks 385-480", 385, 480),
    (6, "All Shanks 481-576", 481, 576),
    (7, "All Shanks 577-672", 577, 672),
    (8, "All Shanks 673-768", 673, 768),
)
_SAFE_ID = re.compile(r"^[A-Za-z0-9._:-]{1,128}$")
_AUTO_SUFFIX = re.compile(r" \([1-9][0-9]*\)$")
_RESERVED_NAMES = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
    *(f"COM{index}" for index in range(1, 10)),
    *(f"LPT{index}" for index in range(1, 10)),
}


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid", frozen=True)


def _require_safe_id(value: str, code: str) -> str:
    if _SAFE_ID.fullmatch(value) is None:
        raise ValueError(code)
    return value


def _validate_native_directory_name(value: str) -> str:
    if _AUTO_SUFFIX.search(value):
        raise ValueError("AUTO_SUFFIX_FORBIDDEN")
    if (
        not value
        or len(value) > 120
        or "/" in value
        or "\\" in value
        or "." in value
        or value[-1].isspace()
        or any(ord(character) < 32 for character in value)
        or value.upper() in _RESERVED_NAMES
    ):
        raise ValueError("INVALID_NATIVE_DIRECTORY_NAME")
    return value


def _validate_absolute_windows_path(value: str, code: str) -> str:
    path = PureWindowsPath(value)
    if not path.is_absolute() or not path.drive:
        raise ValueError(code)
    return str(path)


class HardwareIdentity(StrictModel):
    gui_version: str = Field(min_length=1, max_length=128)
    pxi_plugin_version: str = Field(min_length=1, max_length=128)
    imec_api_version: str = Field(min_length=1, max_length=128)
    onebox_serial: str = Field(min_length=1, max_length=128)
    probe_serial: str = Field(min_length=1, max_length=128)
    slot: int = Field(ge=0, le=255)
    port: int = Field(ge=0, le=255)
    dock: int = Field(ge=0, le=255)
    reference: str = Field(min_length=1, max_length=128)


class StoragePolicy(StrictModel):
    minimum_free_bytes: int = Field(gt=0)
    reserve_bytes: int = Field(gt=0)
    poll_interval_seconds: int = Field(ge=1, le=60)


class StreamExpectation(StrictModel):
    name: str = Field(min_length=1, max_length=128)
    channel_count: int = Field(gt=0, le=4096)
    sample_rate_hz: float = Field(gt=0, le=1_000_000)
    recording_enabled: Literal[True]
    events_required: bool
    timestamps_required: bool


class QcPolicy(StrictModel):
    criteria_id: str = Field(min_length=1, max_length=128)
    scientist_review_required: Literal[True]


class Authorization(StrictModel):
    approval_id: str = Field(min_length=1, max_length=128)
    scope: Literal["SUPERVISED_COMPLETE_RUN"]
    approver: str = Field(min_length=1, max_length=128)
    expires_at_utc: datetime


class PartPlan(StrictModel):
    part_index: int = Field(ge=1, le=8)
    preset: str = Field(min_length=1, max_length=128)
    electrode_start: int = Field(ge=1, le=1248)
    electrode_end: int = Field(ge=1, le=1248)
    target_duration_seconds: int = Field(ge=1, le=3600)
    duration_tolerance_seconds: int = Field(ge=0, le=60)
    maximum_overrun_seconds: int = Field(ge=1, le=300)
    settling_seconds: int = Field(ge=0, le=300)
    directory_name: str = Field(min_length=1, max_length=120)

    @model_validator(mode="after")
    def validate_part(self) -> "PartPlan":
        _validate_native_directory_name(self.directory_name)
        if self.electrode_end < self.electrode_start:
            raise ValueError("INVALID_ELECTRODE_RANGE")
        return self


class RunManifest(StrictModel):
    schema_version: Literal[SCHEMA_VERSION]
    run_id: str = Field(min_length=1, max_length=128)
    subject_id: str = Field(min_length=1, max_length=128)
    operator: str = Field(min_length=1, max_length=128)
    protocol_id: str = Field(min_length=1, max_length=128)
    created_at_utc: datetime
    experiment_root: str = Field(min_length=3, max_length=1024)
    archive_root: str = Field(min_length=3, max_length=1024)
    hardware: HardwareIdentity
    storage: StoragePolicy
    stream_expectations: tuple[StreamExpectation, ...]
    qc_policy: QcPolicy
    authorizations: tuple[Authorization, ...]
    parts: tuple[PartPlan, ...]

    @model_validator(mode="after")
    def validate_complete_run(self) -> "RunManifest":
        _require_safe_id(self.run_id, "INVALID_RUN_ID")
        _require_safe_id(self.subject_id, "INVALID_SUBJECT_ID")
        _require_safe_id(self.operator, "INVALID_OPERATOR")
        _require_safe_id(self.protocol_id, "INVALID_PROTOCOL_ID")
        object.__setattr__(
            self,
            "experiment_root",
            _validate_absolute_windows_path(
                self.experiment_root, "EXPERIMENT_ROOT_NOT_ABSOLUTE"
            ),
        )
        object.__setattr__(
            self,
            "archive_root",
            _validate_absolute_windows_path(
                self.archive_root, "ARCHIVE_ROOT_NOT_ABSOLUTE"
            ),
        )
        observed = tuple(
            (
                part.part_index,
                part.preset,
                part.electrode_start,
                part.electrode_end,
            )
            for part in self.parts
        )
        if observed != DEFAULT_PARTS:
            raise ValueError("PART_PLAN_NOT_ACTUAL_EIGHT_BLOCK_SEQUENCE")
        casefolded = [part.directory_name.casefold() for part in self.parts]
        if len(set(casefolded)) != len(casefolded):
            raise ValueError("DUPLICATE_PART_DIRECTORY")
        if not self.stream_expectations:
            raise ValueError("STREAM_EXPECTATIONS_REQUIRED")
        stream_names = [item.name.casefold() for item in self.stream_expectations]
        if len(set(stream_names)) != len(stream_names):
            raise ValueError("DUPLICATE_STREAM_EXPECTATION")
        if not self.authorizations:
            raise ValueError("AUTHORIZATION_REQUIRED")
        if any(
            authorization.expires_at_utc <= self.created_at_utc
            for authorization in self.authorizations
        ):
            raise ValueError("AUTHORIZATION_EXPIRES_BEFORE_RUN")
        return self

    def planned_directory(self, part_index: int) -> PureWindowsPath:
        for part in self.parts:
            if part.part_index == part_index:
                return PureWindowsPath(self.experiment_root) / part.directory_name
        raise ValueError("PART_NOT_FOUND")
