import pytest
from makxd import ApiProtocol, MakxdController, MouseButton

@pytest.fixture(scope="session")
def makxd():
    ctrl = MakxdController(debug=False, api_protocol=ApiProtocol.KM)
    return ctrl
