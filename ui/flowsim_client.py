"""Thin, safe wrapper around the flowsim_cli executable.

The C++ engine does all simulation and optimization work. This module only writes the
input JSON, launches the executable with an argument list (never through a shell), and
parses the result JSON.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TIMEOUT_S = 120

_REQUIRED_METRICS = (
    "customers_served", "finish_time_min", "mean_wait_min", "p95_wait_min", "utilization_percent",
    "total_objective_cost", "staffing_cost", "waiting_cost", "sla_penalty",
)


class FlowSimError(Exception):
    """Engine or interface failure with a message safe to show in the UI."""

    def __init__(self, code: str, message: str, field: str | None = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.field = field

    def __str__(self) -> str:
        return f"{self.message} (field: {self.field})" if self.field else self.message


def default_cli_path() -> Path:
    env = os.environ.get("FLOWSIM_CLI")
    if env:
        return Path(env)
    name = "flowsim_cli.exe" if sys.platform.startswith("win") else "flowsim_cli"
    for candidate in (REPO_ROOT / "build" / name, REPO_ROOT / "build" / "Release" / name):
        if candidate.exists():
            return candidate
    return REPO_ROOT / "build" / name


@dataclass
class FlowSimClient:
    cli_path: Path | None = None
    timeout_s: float = DEFAULT_TIMEOUT_S

    def __post_init__(self) -> None:
        self.cli_path = Path(self.cli_path) if self.cli_path else default_cli_path()

    def available(self) -> bool:
        return self.cli_path.is_file() and os.access(self.cli_path, os.X_OK)

    def version(self) -> str:
        proc = self._launch([str(self.cli_path), "version"])
        if proc.returncode != 0:
            raise FlowSimError("ENGINE_FAILED", "flowsim_cli version failed")
        return proc.stdout.strip()

    def simulate(self, document: dict[str, Any], counters: int) -> dict[str, Any]:
        result = self._run("simulate", document, ["--counters", str(int(counters))])
        _check_simulation(result, "result")
        return result

    def optimize(self, document: dict[str, Any]) -> dict[str, Any]:
        result = self._run("optimize", document, [])
        if result.get("run_type") != "optimization" or not result.get("results_by_counter_count"):
            raise FlowSimError("BAD_OUTPUT", "engine returned an unexpected optimization result")
        for key in ("best_counter_count", "best_objective_cost", "recommended_simulation"):
            if key not in result:
                raise FlowSimError("BAD_OUTPUT", f"optimization result is missing '{key}'")
        _check_simulation(result["recommended_simulation"], "recommended_simulation")
        return result

    def _launch(self, args: list[str]) -> subprocess.CompletedProcess:
        if not self.available():
            raise FlowSimError(
                "ENGINE_NOT_FOUND",
                "The C++ engine (flowsim_cli) was not found. Build it with ./scripts/build.sh "
                "or set FLOWSIM_CLI to its path.",
            )
        try:
            return subprocess.run(args, check=False, capture_output=True, text=True, timeout=self.timeout_s)
        except subprocess.TimeoutExpired as exc:
            raise FlowSimError("TIMEOUT", f"engine did not finish within {self.timeout_s:g} seconds") from exc
        except OSError as exc:
            raise FlowSimError("ENGINE_FAILED", f"could not start engine: {exc.strerror}") from exc

    def _run(self, command: str, document: dict[str, Any], extra: list[str]) -> dict[str, Any]:
        with tempfile.TemporaryDirectory(prefix="flowsim_") as tmp:
            in_path = Path(tmp) / "input.json"
            out_path = Path(tmp) / "output.json"
            in_path.write_text(json.dumps(document), encoding="utf-8")
            proc = self._launch(
                [str(self.cli_path), command, "--input", str(in_path), "--output", str(out_path), *extra]
            )
            result: dict[str, Any] | None = None
            if out_path.exists():
                try:
                    result = json.loads(out_path.read_text(encoding="utf-8"))
                except json.JSONDecodeError as exc:
                    raise FlowSimError("BAD_OUTPUT", "engine wrote invalid JSON") from exc

        if result is not None and result.get("ok") is False:
            err = result.get("error") or {}
            raise FlowSimError(err.get("code", "ENGINE_ERROR"), err.get("message", "engine error"), err.get("field"))
        if proc.returncode != 0 or result is None:
            lines = [ln for ln in proc.stderr.strip().splitlines() if ln.strip()]
            last = lines[-1].replace(str(Path(tmp)), "<tmp>") if lines else f"exit code {proc.returncode}"
            raise FlowSimError("ENGINE_FAILED", f"engine failed: {last}")
        if result.get("schema_version") != 1:
            raise FlowSimError("BAD_OUTPUT", "unsupported result schema_version")
        return result


def _check_simulation(result: dict[str, Any], where: str) -> None:
    metrics = result.get("metrics")
    if not isinstance(metrics, dict):
        raise FlowSimError("BAD_OUTPUT", f"{where} has no metrics")
    missing = [k for k in _REQUIRED_METRICS if k not in metrics]
    if missing:
        raise FlowSimError("BAD_OUTPUT", f"{where} metrics missing: {', '.join(missing)}")
    for key in ("service_records", "queue_trace"):
        if not isinstance(result.get(key), list):
            raise FlowSimError("BAD_OUTPUT", f"{where} is missing '{key}'")
