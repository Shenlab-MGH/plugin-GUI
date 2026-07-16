import os
import secrets
import threading
from copy import deepcopy

from .models import Mode


_TRANSITIONS = {
    ("IDLE", "ACQUIRE"): ["ACQUIRE"],
    ("IDLE", "RECORD"): ["ACQUIRE", "RECORD"],
    ("ACQUIRE", "IDLE"): ["IDLE"],
    ("ACQUIRE", "RECORD"): ["RECORD"],
    ("RECORD", "ACQUIRE"): ["ACQUIRE"],
    ("RECORD", "IDLE"): ["ACQUIRE", "IDLE"],
}


class AgentGatewayService:
    def __init__(
        self,
        client,
        audit,
        *,
        mutation_allowed=False,
        mutation_disabled_reason="NOT_ARMED",
        safety_check=None,
        safety_evidence=None,
    ):
        self.client = client
        self.audit = audit
        self.mutation_allowed = mutation_allowed
        self.mutation_disabled_reason = mutation_disabled_reason
        self.safety_check = safety_check
        self.safety_evidence = safety_evidence or {}
        self._lock = threading.RLock()
        self._session_id = secrets.token_hex(16)
        self._mode = "UNKNOWN"
        self._revision = 0
        self._requests = {}
        self._payloads = {}
        self._audit_healthy = True
        self._audit(
            "session_started",
            {
                "session_id": self._session_id,
                "mutation_allowed": self.mutation_allowed,
                "mutation_disabled_reason":
                    self.mutation_disabled_reason,
                "safety": deepcopy(self.safety_evidence),
            },
        )

    def _audit(self, event, payload):
        try:
            self.audit.append(event, payload)
            return True
        except Exception:
            self._audit_healthy = False
            return False

    def audit_event(self, event, payload):
        with self._lock:
            return self._audit(event, payload)

    def _observe(self):
        try:
            observed = str(self.client.get_mode()).upper()
        except Exception as error:
            observed = "UNKNOWN"
            self._audit(
                "backend_observation_failed",
                {"error": type(error).__name__},
            )
        if observed not in {mode.value for mode in Mode}:
            observed = "UNKNOWN"
        if observed != self._mode:
            self._mode = observed
            self._revision += 1
        return self._mode

    def status(self):
        with self._lock:
            self._observe()
            status = {
                "schema_version": "oe-agent-gateway/v1",
                "session_id": self._session_id,
                "backend": "open-ephys-rest-v1.0.2",
                "online": self._mode != "UNKNOWN",
                "mode": self._mode,
                "revision": self._revision,
                "mutation_allowed": self.mutation_allowed,
                "mutation_disabled_reason": (
                    None
                    if self.mutation_allowed
                    else self.mutation_disabled_reason
                ),
                "safety": deepcopy(self.safety_evidence),
            }
            return status

    def query(self, request_id):
        with self._lock:
            result = self._requests.get(request_id)
            if result is None:
                return {
                    "request_id": request_id,
                    "state": "UNKNOWN",
                    "reason": "NOT_FOUND",
                }
            return deepcopy(result)

    def _store(self, request_id, payload, result):
        result = {
            "session_id": self._session_id,
            **result,
        }
        self._payloads[request_id] = payload
        self._requests[request_id] = deepcopy(result)
        if not self._audit("transport_request", result):
            result = {
                **result,
                "state": "REJECTED",
                "reason": "AUDIT_WRITE_FAILED",
            }
            self._requests[request_id] = deepcopy(result)
        return deepcopy(result)

    @staticmethod
    def _normalized_payload(request):
        return {
            "request_id": request.get("request_id"),
            "target_mode": request.get("target_mode"),
            "expected_revision": request.get("expected_revision"),
        }

    def _reject(self, payload, reason):
        request_id = payload.get("request_id", "")
        if not isinstance(request_id, str) or not request_id:
            result = {
                "session_id": self._session_id,
                "request_id": "",
                "state": "REJECTED",
                "reason": reason,
            }
            self._audit("transport_request", result)
            return result
        return self._store(
            request_id,
            payload,
            {
                "request_id": payload.get("request_id", ""),
                "state": "REJECTED",
                "reason": reason,
            },
        )

    def _check_safety(self):
        if not self._audit_healthy:
            return False, "AUDIT_UNAVAILABLE", {}
        if self.safety_check is None:
            return (
                self.mutation_allowed,
                (
                    None
                    if self.mutation_allowed
                    else self.mutation_disabled_reason
                ),
                {},
            )
        try:
            allowed, reason, evidence = self.safety_check()
            self.mutation_allowed = bool(allowed)
            self.mutation_disabled_reason = reason
            self.safety_evidence = deepcopy(evidence)
            return bool(allowed), reason, evidence
        except Exception as error:
            return (
                False,
                "SAFETY_PROBE_FAILED",
                {"error": type(error).__name__},
            )

    def _recording_preflight(self):
        try:
            recording = self.client.get_recording()
            nodes = recording.get("record_nodes") or []
            if not nodes:
                return False
            if not all(
                bool(node.get("is_synchronized"))
                for node in nodes
            ):
                return False
            directories = [
                node.get("parent_directory")
                or recording.get("parent_directory")
                for node in nodes
            ]
            return all(
                directory
                and os.path.isdir(directory)
                and os.access(directory, os.W_OK)
                for directory in directories
            )
        except Exception:
            return False

    def submit(self, request):
        payload = self._normalized_payload(request)
        request_id = payload["request_id"]
        target = payload["target_mode"]
        expected_revision = payload["expected_revision"]

        with self._lock:
            if (
                not isinstance(request_id, str)
                or not request_id
                or len(request_id) > 128
            ):
                return self._reject(payload, "INVALID_REQUEST")
            if (
                not isinstance(target, str)
                or target not in {"IDLE", "ACQUIRE", "RECORD"}
            ):
                return self._reject(payload, "INVALID_REQUEST")
            if (
                not isinstance(expected_revision, int)
                or isinstance(expected_revision, bool)
                or expected_revision < 0
            ):
                return self._reject(payload, "INVALID_REQUEST")

            if request_id in self._payloads:
                if self._payloads[request_id] == payload:
                    return deepcopy(self._requests[request_id])
                self._audit(
                    "transport_id_conflict",
                    {
                        "session_id": self._session_id,
                        "request_id": request_id,
                    },
                )
                return {
                    "session_id": self._session_id,
                    "request_id": request_id,
                    "state": "REJECTED",
                    "reason": "ID_CONFLICT",
                }

            allowed, disabled_reason, safety_evidence = (
                self._check_safety()
            )
            if not allowed:
                return self._reject(payload, disabled_reason)
            current = self._observe()
            if current == "UNKNOWN":
                return self._reject(payload, "BACKEND_UNAVAILABLE")
            if expected_revision != self._revision:
                return self._reject(payload, "STALE_REVISION")
            if current == target:
                return self._store(
                    request_id,
                    payload,
                    {
                        "request_id": request_id,
                        "state": "COMPLETED",
                        "reason": "ALREADY_SATISFIED",
                        "final_mode": current,
                        "final_revision": self._revision,
                    },
                )
            if target == "RECORD" and not self._recording_preflight():
                return self._reject(
                    payload,
                    "RECORD_PREFLIGHT_FAILED",
                )

            result = {
                "request_id": request_id,
                "state": "ACTIVE",
                "reason": None,
                "initial_mode": current,
                "target_mode": target,
                "steps": [],
                "safety_evidence": safety_evidence,
            }
            self._payloads[request_id] = payload
            self._requests[request_id] = deepcopy(result)
            if not self._audit(
                "transport_request_accepted",
                deepcopy(result),
            ):
                result["state"] = "REJECTED"
                result["reason"] = "AUDIT_UNAVAILABLE"
                return self._store(request_id, payload, result)

            expected_before = current
            for expected in _TRANSITIONS[(current, target)]:
                allowed, disabled_reason, step_safety = (
                    self._check_safety()
                )
                if not allowed:
                    result["state"] = "REJECTED"
                    result["reason"] = disabled_reason
                    result["safety_evidence"] = step_safety
                    return self._store(request_id, payload, result)

                try:
                    before = str(self.client.get_mode()).upper()
                except Exception:
                    before = "UNKNOWN"
                self._observe_from_value(before)
                if before != expected_before:
                    result["state"] = "REJECTED"
                    result["reason"] = "PRECONDITION_CHANGED"
                    result["final_mode"] = before
                    result["final_revision"] = self._revision
                    return self._store(request_id, payload, result)

                if (
                    expected == "RECORD"
                    and not self._recording_preflight()
                ):
                    result["state"] = "REJECTED"
                    result["reason"] = "RECORD_PREFLIGHT_FAILED"
                    return self._store(request_id, payload, result)

                if not self._audit(
                    "transport_step_intent",
                    {
                        "session_id": self._session_id,
                        "request_id": request_id,
                        "expected_before": expected_before,
                        "target": expected,
                        "safety_evidence": step_safety,
                    },
                ):
                    result["state"] = "REJECTED"
                    result["reason"] = "AUDIT_UNAVAILABLE"
                    return self._store(request_id, payload, result)
                transport_error = None
                try:
                    self.client.set_mode(expected)
                except Exception as error:
                    transport_error = type(error).__name__

                try:
                    if hasattr(self.client, "wait_for_mode"):
                        observed = self.client.wait_for_mode(expected)
                    else:
                        observed = self.client.get_mode()
                except Exception as error:
                    observed = "UNKNOWN"
                    result["steps"].append(
                        {
                            "target": expected,
                            "observed": observed,
                            "transport_error": transport_error,
                            "readback_error": type(error).__name__,
                        }
                    )
                    result["state"] = "REJECTED"
                    result["reason"] = "BACKEND_ERROR"
                    return self._store(request_id, payload, result)

                self._observe_from_value(observed)
                step_result = {
                    "expected_before": expected_before,
                    "target": expected,
                    "observed": observed,
                    "transport_error": transport_error,
                }
                result["steps"].append(step_result)
                if not self._audit(
                    "transport_step_readback",
                    {
                        "session_id": self._session_id,
                        "request_id": request_id,
                        **step_result,
                    },
                ):
                    result["state"] = "REJECTED"
                    result["reason"] = "AUDIT_WRITE_FAILED"
                    result["final_mode"] = observed
                    result["final_revision"] = self._revision
                    return self._store(request_id, payload, result)
                if observed != expected:
                    result["state"] = "REJECTED"
                    result["reason"] = "READBACK_MISMATCH"
                    result["final_mode"] = observed
                    result["final_revision"] = self._revision
                    return self._store(request_id, payload, result)
                expected_before = expected

            result["state"] = "COMPLETED"
            result["reason"] = None
            result["final_mode"] = self._mode
            result["final_revision"] = self._revision
            return self._store(request_id, payload, result)

    def _observe_from_value(self, observed):
        observed = str(observed).upper()
        if observed not in {mode.value for mode in Mode}:
            observed = "UNKNOWN"
        if observed != self._mode:
            self._mode = observed
            self._revision += 1
