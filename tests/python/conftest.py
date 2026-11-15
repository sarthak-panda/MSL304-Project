import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ui"))

from flowsim_client import FlowSimClient  # noqa: E402


@pytest.fixture(scope="session")
def client():
    c = FlowSimClient()
    if not c.available():
        pytest.fail("flowsim_cli is not built; run ./scripts/build.sh first")
    return c


@pytest.fixture
def sample_doc():
    import copy

    from scenario import SAMPLE_SCENARIO, build_document
    return build_document(copy.deepcopy(SAMPLE_SCENARIO))
