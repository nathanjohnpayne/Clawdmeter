#!/usr/bin/env python3
"""Tests for the Codex usage collector.

The two bugs these guard against were both found on real data:

  * Reading the newest rollout record blindly returned 0% off a per-model
    bucket while the account was actually at 78%.
  * primary/secondary are positional, not semantic -- a Pro plan puts its
    7-day window in `primary`, a per-model block puts 5h there.

Run: python -m pytest daemon/tests/test_codex_collector.py -x -q
"""
import json
import urllib.error
from unittest.mock import patch

from daemon.collectors import WINDOW_5H, WINDOW_7D
from daemon.collectors.codex import (
    CodexCollector,
    _windows_from_endpoint,
    collect_via_logs,
    collect_via_oauth,
)


# --- endpoint shape ---------------------------------------------------------

def test_endpoint_windows_keyed_by_duration_not_position():
    """A Pro plan's 7-day window arrives in `primary`, with secondary null."""
    windows = _windows_from_endpoint({
        "primary_window": {"used_percent": 78, "limit_window_seconds": 604800,
                           "reset_after_seconds": 428901},
        "secondary_window": None,
    })
    assert set(windows) == {WINDOW_7D}
    assert windows[WINDOW_7D].used_percent == 78
    assert windows[WINDOW_7D].resets_in == 428901


def test_endpoint_windows_handles_both_slots():
    """A per-model block uses primary for 5h and secondary for 7d."""
    windows = _windows_from_endpoint({
        "primary_window": {"used_percent": 12, "limit_window_seconds": 18000},
        "secondary_window": {"used_percent": 3, "limit_window_seconds": 604800},
    })
    assert windows[WINDOW_5H].used_percent == 12
    assert windows[WINDOW_7D].used_percent == 3


def test_unknown_window_length_is_dropped_not_guessed():
    assert _windows_from_endpoint(
        {"primary_window": {"used_percent": 5, "limit_window_seconds": 999}}
    ) == {}


# --- free-ride credential rule ----------------------------------------------

def _auth_dir(tmp_path):
    (tmp_path / "auth.json").write_text(json.dumps(
        {"tokens": {"access_token": "tok", "account_id": "acct"}}))
    return tmp_path


def test_dead_token_reports_no_data_and_never_refreshes(tmp_path):
    """401 means "no data", not a refresh -- the Codex CLI owns the token."""
    err = urllib.error.HTTPError(url="", code=401, msg="", hdrs=None, fp=None)
    with patch("urllib.request.urlopen", side_effect=err):
        assert collect_via_oauth(_auth_dir(tmp_path)) is None


def test_signed_out_reports_no_data(tmp_path):
    """No auth.json at all."""
    assert collect_via_oauth(tmp_path) is None


# --- rollout-log fallback ---------------------------------------------------

def _rollout(tmp_path, *records):
    d = tmp_path / "sessions" / "2026" / "08" / "20"
    d.mkdir(parents=True)
    path = d / "rollout-2026-08-20T10-00-12-test.jsonl"
    path.write_text("\n".join(
        json.dumps({"payload": {"rate_limits": r}}) for r in records))
    return path


PER_MODEL = {
    "limit_id": "codex_bengalfox", "limit_name": "GPT-5.3-Codex-Spark",
    "primary": {"used_percent": 0.0, "window_minutes": 300, "resets_at": 0},
    "secondary": {"used_percent": 0.0, "window_minutes": 10080, "resets_at": 0},
}
ACCOUNT = {
    "limit_id": "codex", "plan_type": "pro",
    "primary": {"used_percent": 78.0, "window_minutes": 10080, "resets_at": 0},
    "secondary": None,
}


def test_per_model_bucket_never_masquerades_as_the_account_limit(tmp_path):
    """The account record is older, so the naive "newest wins" read got 0%."""
    _rollout(tmp_path, ACCOUNT, PER_MODEL)
    snap = collect_via_logs(tmp_path)
    assert snap is not None
    assert snap.windows[WINDOW_7D].used_percent == 78.0
    assert WINDOW_5H not in snap.windows  # 5h belonged to the per-model bucket


def test_no_account_record_reports_no_data(tmp_path):
    """Per-model records alone are not a usable answer."""
    _rollout(tmp_path, PER_MODEL)
    assert collect_via_logs(tmp_path) is None


def test_log_snapshot_is_never_reported_as_live(tmp_path):
    """A log file written a second ago is still not an on-demand read."""
    _rollout(tmp_path, ACCOUNT)
    snap = collect_via_logs(tmp_path)
    assert snap.source.startswith("logs:")
    assert not snap.live


# --- fallback ordering ------------------------------------------------------

def test_logs_used_only_when_the_endpoint_fails(tmp_path):
    _auth_dir(tmp_path)
    _rollout(tmp_path, ACCOUNT)
    with patch("urllib.request.urlopen", side_effect=urllib.error.URLError("offline")):
        snap = CodexCollector(tmp_path).collect()
    assert snap.source.startswith("logs:")
