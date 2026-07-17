"""Fail-closed Neuropixels preset evidence and human confirmation binding."""

from __future__ import annotations

from datetime import datetime
from enum import Enum

from pydantic import BaseModel, ConfigDict, Field


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid", frozen=True)


class PresetCapability(str, Enum):
    SEMANTIC_SET_AND_READBACK = "SEMANTIC_SET_AND_READBACK"
    ACCESSIBLE_SET_AND_READBACK = "ACCESSIBLE_SET_AND_READBACK"
    HUMAN_CONFIRMED = "HUMAN_CONFIRMED"
    UNVERIFIABLE = "UNVERIFIABLE"


class PresetEvidence(StrictModel):
    expected_preset: str | None = Field(default=None, max_length=128)
    semantic_set: bool = False
    semantic_readback: str | None = Field(default=None, max_length=128)
    accessibility_set: bool = False
    accessibility_readback: str | None = Field(default=None, max_length=128)
    human_confirmation_id: str | None = Field(default=None, max_length=128)
    human_operator: str | None = Field(default=None, max_length=128)
    exact_displayed_text: str | None = Field(default=None, max_length=128)
    pixel_match: str | None = Field(default=None, max_length=128)


def classify_preset_capability(evidence: PresetEvidence) -> PresetCapability:
    expected = evidence.expected_preset
    if (
        expected
        and evidence.semantic_set
        and evidence.semantic_readback == expected
    ):
        return PresetCapability.SEMANTIC_SET_AND_READBACK
    if (
        expected
        and evidence.accessibility_set
        and evidence.accessibility_readback == expected
    ):
        return PresetCapability.ACCESSIBLE_SET_AND_READBACK
    if (
        expected
        and evidence.human_confirmation_id
        and evidence.human_operator
        and evidence.exact_displayed_text == expected
    ):
        return PresetCapability.HUMAN_CONFIRMED
    return PresetCapability.UNVERIFIABLE


class PresetConfirmationError(ValueError):
    """A previously recorded confirmation no longer matches live context."""


class PresetConfirmation(StrictModel):
    confirmation_id: str = Field(min_length=1, max_length=128)
    run_id: str = Field(min_length=1, max_length=128)
    part_index: int = Field(ge=1, le=8)
    preset: str = Field(min_length=1, max_length=128)
    probe_serial: str = Field(min_length=1, max_length=128)
    configuration_revision: int = Field(ge=0)
    evidence_sha256: str = Field(pattern=r"^[0-9a-fA-F]{64}$")
    operator: str = Field(min_length=1, max_length=128)
    confirmed_at: datetime

    def verify(
        self,
        *,
        run_id: str,
        part_index: int,
        preset: str,
        probe_serial: str,
        configuration_revision: int,
    ) -> PresetConfirmation:
        comparisons = (
            (self.run_id == run_id, "RUN_CHANGED"),
            (self.part_index == part_index, "PART_CHANGED"),
            (self.preset == preset, "PRESET_CHANGED"),
            (self.probe_serial == probe_serial, "PROBE_IDENTITY_CHANGED"),
            (
                self.configuration_revision == configuration_revision,
                "CONFIGURATION_CHANGED",
            ),
        )
        for matches, code in comparisons:
            if not matches:
                raise PresetConfirmationError(code)
        return self
