//! Basic real-device example — connects to a MAKXD and exercises core functionality.
//!
//! Run with:
//!   cargo run --example basic

use std::thread::sleep;
use std::time::Duration;

use makxd::{Button, Device, LockTarget, Result, VERSION};

const PAUSE: Duration = Duration::from_millis(500);

fn main() -> Result<()> {
    println!("=== makxd v{} — Basic Example ===\n", VERSION);

    // Auto-detect and connect to a MAKXD device by USB VID/PID.
    let device = Device::connect()?;
    println!("Connected to: {}", device.port_name());
    sleep(PAUSE);

    // -- Device info --
    let version = device.version()?;
    println!("Firmware: {}", version);

    let serial = device.serial()?;
    println!("Serial: {}", serial);
    sleep(PAUSE);

    // -- Move the cursor --
    println!("\nMoving cursor right 80...");
    device.move_xy(80, 0)?;
    sleep(PAUSE);

    println!("Moving cursor back...");
    device.move_xy(-80, 0)?;
    sleep(PAUSE);

    println!("Silent move (10,10)...");
    device.silent_move(10, 10)?;
    sleep(PAUSE);

    device.silent_move(-10, -10)?;
    println!("Silent moved back");
    sleep(PAUSE);

    // -- Scroll --
    println!("\nScrolling up 3...");
    device.wheel(3)?;
    sleep(PAUSE);

    println!("Scrolling down 3...");
    device.wheel(-3)?;
    sleep(PAUSE);

    // -- Button press/release --
    println!("\nLeft click (down + up)...");
    device.button_down(Button::Left)?;
    sleep(Duration::from_millis(50));
    device.button_up(Button::Left)?;
    sleep(PAUSE);

    // Query button state
    let left = device.button_state(Button::Left)?;
    println!("Left button pressed: {}", left);

    // Force release (ensures button is up even if state is stuck)
    device.button_up_force(Button::Left)?;
    println!("Left force-released");
    sleep(PAUSE);

    // -- Locks --
    println!("\nLocking X axis...");
    device.set_lock(LockTarget::X, true)?;
    let locked = device.lock_state(LockTarget::X)?;
    println!("X locked: {} (try moving your mouse horizontally!)", locked);
    sleep(Duration::from_secs(2));

    device.set_lock(LockTarget::X, false)?;
    println!("X unlocked");
    sleep(PAUSE);

    // -- Fire-and-forget (faster, no response wait) --
    println!("\nFire-and-forget moves...");
    let ff = device.ff();
    ff.move_xy(40, 0)?;
    sleep(Duration::from_millis(200));
    ff.move_xy(-40, 0)?;
    println!("F&F moves sent (no response wait)");
    sleep(PAUSE);

    // -- Button stream --
    println!("\nButton stream (press a mouse button now!)...");
    device.enable_button_stream()?;
    let rx = device.button_events();
    sleep(Duration::from_secs(2));
    match rx.try_recv() {
        Ok(mask) => println!(
            "Button event: left={}, right={}, mid={}",
            mask.left(),
            mask.right(),
            mask.middle()
        ),
        Err(_) => println!("No button events received"),
    }
    device.disable_button_stream()?;
    sleep(PAUSE);

    // -- Raw command (escape hatch) --
    let raw = device.send_raw(b"km.version()\r\n")?;
    println!("\nRaw command response: {} bytes", raw.len());
    sleep(PAUSE);

    // -- Disconnect --
    device.disconnect();
    println!("Disconnected");

    println!("\n=== Done! ===");
    Ok(())
}
