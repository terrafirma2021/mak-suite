import pytest
import time
from makxd import MouseButton


TEST_BUTTONS = (MouseButton.LEFT, MouseButton.RIGHT, MouseButton.MIDDLE)
BUTTON_STATE_KEYS = ('left', 'right', 'middle', 'mouse4', 'mouse5')
MOVE_COORDS = ((10, 0), (0, 10), (-10, 0), (0, -10))

def test_connect_to_port(makxd):
    print("Connecting to port...")
    makxd.connect()
    assert makxd.is_connected(), "Failed to connect to the makxd"

def test_press_and_release(makxd):
    makxd.press(MouseButton.LEFT)
    makxd.release(MouseButton.LEFT)

def test_firmware_version(makxd):
    version = makxd.get_firmware_version()
    assert version and len(version.strip()) > 0

def test_middle_click(makxd):
    makxd.press(MouseButton.MIDDLE)
    makxd.release(MouseButton.MIDDLE)

def test_device_info(makxd):
    print("Fetching device info...")
    info = makxd.mouse.get_device_info()
    print(f"Device Info: {info}")
    assert info.get("port")
    assert info.get("isConnected") is True

def test_port_connection(makxd):
    assert makxd.is_connected()

def test_button_mask(makxd):
    print("Getting button mask...")
    mask = makxd.get_button_mask()
    print(f"Mask value: {mask}")
    assert isinstance(mask, int)

def test_get_button_states(makxd):
    states = makxd.get_button_states()
    assert isinstance(states, dict)
    for key in BUTTON_STATE_KEYS:
        assert key in states

def test_lock_state(makxd):
    print("Locking LEFT button...")
    makxd.lock_left(True)
    print("Querying lock state while LEFT is locked...")
    state = makxd.is_locked(MouseButton.LEFT)
    print(state)
    assert state

def test_makxd_behavior(makxd):
    makxd.move(25, 25)
    makxd.click(MouseButton.LEFT)
    makxd.scroll(-2)

def test_batch_commands(makxd):
    print("Testing batch command execution (10 commands)...")
    
    start_time = time.perf_counter()

    async def combo_actions():
        await makxd.batch_execute([
            lambda: makxd.move(5, 5),
            lambda: makxd.click(MouseButton.LEFT),
            lambda: makxd.scroll(-1)
        ])

    combo_actions()
    
    end_time = time.perf_counter()
    elapsed_ms = (end_time - start_time) * 1000
    
    print(f"Batch execution time: {elapsed_ms:.2f}ms")
    print(f"Average per command: {elapsed_ms/10:.2f}ms")
    

    assert elapsed_ms < 50, f"Batch commands took {elapsed_ms:.2f}ms, expected < 50ms"
    

    start_time = time.perf_counter()
    for _ in range(10):
        makxd.move(5, 5)
    end_time = time.perf_counter()
    
    move_only_ms = (end_time - start_time) * 1000
    print(f"10 move commands: {move_only_ms:.2f}ms ({move_only_ms/10:.2f}ms per move)")

def test_rapid_moves(makxd):
    start = time.perf_counter_ns()
    

    makxd.move(5, 5)
    makxd.move(5, 5)
    makxd.move(5, 5)
    makxd.move(5, 5)
    makxd.move(5, 5)
    makxd.move(5, 5)
    makxd.move(5, 5)
    makxd.move(5, 5)
    makxd.move(5, 5)
    makxd.move(5, 5)
    
    elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
    print(f"10 rapid moves: {elapsed_ms:.2f}ms")
    assert elapsed_ms < 30

def test_button_performance(makxd):
    start = time.perf_counter_ns()
    

    for button in TEST_BUTTONS:
        makxd.press(button)
        makxd.release(button)
    
    elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
    print(f"Button operations: {elapsed_ms:.2f}ms")
    assert elapsed_ms < 20

def test_mixed_operations(makxd):
    start = time.perf_counter_ns()
    

    makxd.move(20, 20)
    makxd.press(MouseButton.LEFT)
    makxd.move(-20, -20)
    makxd.release(MouseButton.LEFT)
    makxd.scroll(1)
    
    elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
    print(f"Mixed operations: {elapsed_ms:.2f}ms")
    assert elapsed_ms < 15


def test_cleanup(makxd):
    time.sleep(0.1)

    makxd.lock_left(False)
    makxd.lock_right(False)
    makxd.lock_middle(False)
    makxd.lock_side1(False)
    makxd.lock_side2(False)
    makxd.lock_x(False)
    makxd.lock_y(False)

    makxd.release(MouseButton.LEFT)
    makxd.release(MouseButton.RIGHT)
    makxd.release(MouseButton.MIDDLE)
    makxd.release(MouseButton.MOUSE4)
    makxd.release(MouseButton.MOUSE5)

    makxd.enable_button_monitoring(False)
    makxd.disconnect()
    assert not makxd.is_connected(), "Failed to disconnect from the makxd"