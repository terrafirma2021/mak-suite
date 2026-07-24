import pytest
import time
from makxd import MakxdController, MouseButton

@pytest.fixture(scope="session")
def makxd():
    ctrl = MakxdController(fallback_com_port="COM1", debug=False)
    return ctrl