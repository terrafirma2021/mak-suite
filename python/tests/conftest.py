import pytest
from makxd import MakxdController

@pytest.fixture(scope="session")
def makxd():
    ctrl = MakxdController(debug=False)
    return ctrl
