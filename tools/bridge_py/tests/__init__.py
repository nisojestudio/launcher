from __future__ import annotations

import sys
from pathlib import Path


TESTS_DIR = Path(__file__).resolve().parent
BRIDGE_PY_DIR = TESTS_DIR.parent

if str(BRIDGE_PY_DIR) not in sys.path:
    sys.path.insert(0, str(BRIDGE_PY_DIR))
