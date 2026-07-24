//! Mock transport example — demonstrates how to use makxd without real hardware.
//!
//! Run with:
//!   cargo run --example mock --features "mock"
//!
//! ## How the Mock Transport Works
//!
//! When you call `Device::mock()`, the library creates a `Device` backed by a
//! `MockTransport` instead of a real serial port. No USB device is needed.
//!
//! The mock replaces the entire serial I/O layer:
//!
//!   Real hardware path:
//!     Device -> channel -> writer thread -> serial port -> MAKXD chip
//!                          reader thread <- serial port <- MAKXD chip
//!
//!   Mock path:
//!     Device -> channel -> mock_worker thread -> MockTransport (HashMap lookup)
//!
//! Instead of sending bytes over USB and waiting for the firmware to respond,
//! the mock worker thread looks up the command bytes in a HashMap and returns
//! the pre-configured response. This means:
//!
//! - Commands that return values (version, serial, button_state, lock_state)
//!   need a response registered via `mock.on_command(command_bytes, response_bytes)`
//!   before you call them, or they'll get an empty response.
//!
//! - Commands that just execute (move_xy, button_down, set_lock) work without
//!   any setup — they send bytes and expect no meaningful response body.
//!
//! - Fire-and-forget commands always work — they don't wait for a response.
//!
//! - Button events can be injected via `mock.inject_button_event(mask)` and
//!   will be dispatched to subscribers on the next command.
//!
//! - `mock.sent_commands()` returns every command that was sent through the
//!   transport, useful for asserting your code sends the right bytes.
//!
//! ## Key Differences from Real Hardware
//!
//! | Aspect            | Real Hardware                    | Mock                              |
//! |-------------------|----------------------------------|-----------------------------------|
//! | Connection        | USB serial at 4 Mbaud            | In-process, instant               |
//! | Latency           | ~700-2000μs per command          | ~10μs per command                 |
//! | Response format   | GET echoes query + value; SET/EXEC echo command only | You provide exact response bytes  |
//! | Button stream     | Hardware sends `km.` + mask byte | You inject events manually        |
//! | Timing (extras)   | Real sleeps between steps        | Real sleeps (same as hardware)    |
//! | Reconnection      | Monitor thread retries on USB    | No disconnect possible            |
//! | Side effects      | Cursor actually moves            | Nothing happens on screen         |

use makxd::{Button, ButtonMask, Device, LockTarget, Result, VERSION};

fn main() -> Result<()> {
    println!("=== makxd v{} — Mock Example ===\n", VERSION);

    // Create a Device + MockTransport pair. No hardware needed.
    let (device, mock) = Device::mock();
    println!("Mock device created (port: {})", device.port_name());

    // -------------------------------------------------------------------------
    // Query commands need responses registered first
    // -------------------------------------------------------------------------

    // The response bodies mirror what the real firmware sends before the prompt:
    //   b"km.version()\r\n"  ->  b"km.MAKCU v3.7"  (single-line = value)
    //   b"km.left()\r\n"     ->  b"km.left()\r\n0"  (echo + newline + value)
    //   b"km.left(1)\r\n"   ->  b"km.left(1)"  (SET echo only)
    mock.on_command(b"km.version()\r\n", b"km.MAKCU v3.7");
    mock.on_command(b"km.left()\r\n", b"km.left()\r\n0");
    mock.on_command(b"km.lock_mx()\r\n", b"km.lock_mx()\r\n0");
    mock.on_command(b"km.left(1)\r\n", b"km.left(1)");

    let version = device.version()?;
    println!("version() -> {}", version);

    let left_state = device.button_state(Button::Left)?;
    println!("button_state(Left) -> {}", left_state);

    let x_locked = device.lock_state(LockTarget::X)?;
    println!("lock_state(X) -> {}", x_locked);

    // -------------------------------------------------------------------------
    // Execute commands work without setup
    // -------------------------------------------------------------------------

    // These send bytes but don't need a specific response — the mock returns
    // empty bytes which the parser treats as "executed successfully".
    device.move_xy(100, -50)?;
    device.button_down(Button::Left)?;
    device.button_up(Button::Left)?;
    device.button_up_force(Button::Right)?;
    device.silent_move(10, 20)?;
    device.wheel(3)?;
    device.set_lock(LockTarget::Y, true)?;
    device.set_lock(LockTarget::Y, false)?;
    println!("Execute commands work with no setup");

    // -------------------------------------------------------------------------
    // Fire-and-forget is even simpler
    // -------------------------------------------------------------------------

    let ff = device.ff();
    ff.move_xy(50, 50)?;
    ff.button_down(Button::Right)?;
    ff.button_up(Button::Right)?;
    println!("Fire-and-forget commands sent");

    // -------------------------------------------------------------------------
    // Button stream and event injection
    // -------------------------------------------------------------------------

    device.enable_button_stream()?;
    let rx = device.button_events();

    // Inject a button event (left + right pressed = 0x03)
    mock.inject_button_event(ButtonMask::default());

    // Events are dispatched when the next command is processed by the mock worker
    device.move_xy(0, 0)?;

    match rx.try_recv() {
        Ok(mask) => println!(
            "Received injected event: left={}, right={}",
            mask.left(),
            mask.right()
        ),
        Err(_) => println!("No event received (timing dependent)"),
    }

    device.disable_button_stream()?;

    // -------------------------------------------------------------------------
    // Inspect what was sent
    // -------------------------------------------------------------------------

    let commands = mock.sent_commands();
    println!("\nTotal commands sent: {}", commands.len());
    println!("First 5 commands:");
    for (i, cmd) in commands.iter().take(5).enumerate() {
        let text = String::from_utf8_lossy(cmd);
        println!("  [{}] {}", i, text.trim());
    }

    // You can clear and start fresh
    mock.clear_sent();
    device.move_xy(1, 1)?;
    assert_eq!(mock.sent_commands().len(), 1);
    println!(
        "\nAfter clear + 1 command: {} sent",
        mock.sent_commands().len()
    );

    // -------------------------------------------------------------------------
    // Disconnect
    // -------------------------------------------------------------------------

    device.disconnect();
    println!("\nDisconnected");

    println!("\n=== Done! ===");
    Ok(())
}
