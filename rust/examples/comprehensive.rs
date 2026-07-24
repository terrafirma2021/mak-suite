//! Comprehensive real-device example — exercises EVERY library function, variant,
//! and feature flag (async, batch, extras, profile) on actual hardware.
//!
//! Run with:
//!   cargo run --example comprehensive --features "async,batch,extras,profile"
//!
//! Requires a MAKXD device physically connected with a mouse.

use std::time::Duration;

use makxd::{
    AsyncDevice, Button, DeviceConfig, EventHandle, LockTarget, MakxdError, Result, VERSION,
};

const PAUSE: Duration = Duration::from_millis(1000);
const LONG_PAUSE: Duration = Duration::from_secs(2);

async fn pause() {
    tokio::time::sleep(PAUSE).await;
}

async fn long_pause() {
    tokio::time::sleep(LONG_PAUSE).await;
}

#[tokio::main]
async fn main() -> Result<()> {
    println!("=== makxd v{} — Comprehensive Example ===\n", VERSION);

    // =========================================================================
    // 1. Connection (async)
    // =========================================================================
    println!("--- 1. Async Connection ---");

    let device = AsyncDevice::connect().await?;
    println!("  Connected: {}", device.is_connected());
    println!("  Port: {}", device.port_name());

    // You can also connect with explicit config:
    let custom_cfg = DeviceConfig {
        port: Some(device.port_name()),
        try_4m_first: true,
        command_timeout: Duration::from_millis(500),
        reconnect: true,
        reconnect_backoff: Duration::from_millis(100),
        fire_and_forget: false,
    };
    println!(
        "  DeviceConfig: timeout={:?}, reconnect={}, ff={}",
        custom_cfg.command_timeout, custom_cfg.reconnect, custom_cfg.fire_and_forget
    );
    pause().await;

    // =========================================================================
    // 2. Device Info
    // =========================================================================
    println!("\n--- 2. Device Info ---");

    let info = device.device_info().await?;
    println!("  Port: {}", info.port);
    println!("  Firmware: {}", info.firmware);

    let version = device.version().await?;
    println!("  Version: {}", version);

    let serial = device.serial().await?;
    println!("  Serial: {}", serial);

    let spoofed = device.set_serial("TEST123").await?;
    println!("  set_serial(\"TEST123\"): {}", spoofed);

    let reset = device.reset_serial().await?;
    println!("  reset_serial(): {}", reset);
    pause().await;

    // =========================================================================
    // 3. Buttons — all 5 variants
    // =========================================================================
    println!("\n--- 3. Buttons (all 5 variants) ---");

    let buttons = [
        Button::Left,
        Button::Right,
        Button::Middle,
        Button::Side1,
        Button::Side2,
    ];

    for &button in &buttons {
        let state = device.button_state(button).await?;
        println!("  {:?} state: {}", button, state);
    }
    pause().await;

    for &button in &buttons {
        println!("  {:?} down+up...", button);
        device.button_down(button).await?;
        tokio::time::sleep(Duration::from_millis(50)).await;
        device.button_up(button).await?;
        tokio::time::sleep(Duration::from_millis(150)).await;
    }

    for &button in &buttons {
        device.button_up_force(button).await?;
    }
    println!("  All 5 buttons force-released");
    pause().await;

    // =========================================================================
    // 4. Movement — all 3 types
    // =========================================================================
    println!("\n--- 4. Movement ---");

    println!("  move_xy: right 80...");
    device.move_xy(80, 0).await?;
    pause().await;

    println!("  move_xy: back left 80...");
    device.move_xy(-80, 0).await?;
    pause().await;

    println!("  move_xy: down 50...");
    device.move_xy(0, 50).await?;
    pause().await;

    println!("  move_xy: back up 50...");
    device.move_xy(0, -50).await?;
    pause().await;

    println!("  silent_move: (15,15)...");
    device.silent_move(15, 15).await?;
    pause().await;

    device.silent_move(-15, -15).await?;
    println!("  silent_move: back");
    pause().await;

    println!("  wheel: up 3...");
    device.wheel(3).await?;
    pause().await;

    println!("  wheel: down 3...");
    device.wheel(-3).await?;
    pause().await;

    // =========================================================================
    // 5. Locks — all 7 targets
    // =========================================================================
    println!("\n--- 5. Locks (all 7 targets) ---");

    let lock_targets = [
        LockTarget::X,
        LockTarget::Y,
        LockTarget::Left,
        LockTarget::Right,
        LockTarget::Middle,
        LockTarget::Side1,
        LockTarget::Side2,
    ];

    for &target in &lock_targets {
        device.set_lock(target, true).await?;
        let state = device.lock_state(target).await?;
        println!("  {:?} locked: {}", target, state);
    }
    println!("  All 7 targets locked — try moving/clicking!");
    long_pause().await;
    long_pause().await;

    for &target in &lock_targets {
        device.set_lock(target, false).await?;
    }
    println!("  All unlocked");

    let all = device.lock_states_all().await?;
    println!(
        "  lock_states_all: x={}, y={}, left={}, right={}, mid={}, s1={}, s2={}",
        all.x, all.y, all.left, all.right, all.middle, all.side1, all.side2
    );
    pause().await;

    // =========================================================================
    // 6. Button Stream
    // =========================================================================
    println!("\n--- 6. Button Stream ---");

    device.enable_button_stream().await?;
    println!("  Stream enabled");

    let rx = device.button_events();
    println!("  Subscribed — press a mouse button now!");
    tokio::time::sleep(Duration::from_secs(3)).await;

    let mut count = 0;
    while let Ok(mask) = rx.try_recv() {
        if count < 5 {
            println!(
                "  Event: left={}, right={}, mid={}, s1={}, s2={}",
                mask.left(),
                mask.right(),
                mask.middle(),
                mask.side1(),
                mask.side2()
            );
        }
        count += 1;
    }
    if count == 0 {
        println!("  No events (normal if no buttons pressed)");
    } else if count > 5 {
        println!("  ... and {} more events", count - 5);
    }

    device.disable_button_stream().await?;
    println!("  Stream disabled");
    pause().await;

    // =========================================================================
    // 7. Fire-and-Forget — all commands
    // =========================================================================
    println!("\n--- 7. Fire-and-Forget (all commands) ---");

    let ff = device.ff();

    println!("  ff.button_down(Left)...");
    ff.button_down(Button::Left).await?;
    tokio::time::sleep(Duration::from_millis(100)).await;

    ff.button_up(Button::Left).await?;
    println!("  ff.button_up(Left)");

    ff.button_up_force(Button::Right).await?;
    println!("  ff.button_up_force(Right)");

    println!("  ff.move_xy(30, 0)...");
    ff.move_xy(30, 0).await?;
    tokio::time::sleep(Duration::from_millis(200)).await;
    ff.move_xy(-30, 0).await?;
    println!("  ff.move_xy(-30, 0)");

    ff.silent_move(5, 5).await?;
    ff.silent_move(-5, -5).await?;
    println!("  ff.silent_move: there and back");

    ff.wheel(1).await?;
    ff.wheel(-1).await?;
    println!("  ff.wheel: up 1, down 1");

    ff.set_lock(LockTarget::X, true).await?;
    ff.set_lock(LockTarget::X, false).await?;
    println!("  ff.set_lock: X on then off");

    ff.enable_button_stream().await?;
    ff.disable_button_stream().await?;
    println!("  ff.enable/disable_button_stream");
    pause().await;

    // =========================================================================
    // 8. Raw Command
    // =========================================================================
    println!("\n--- 8. Raw Command ---");

    let raw = device.send_raw(b"km.version()\r\n").await?;
    println!("  send_raw(km.version) -> {} bytes", raw.len());
    pause().await;

    // =========================================================================
    // 9. Connection Events
    // =========================================================================
    println!("\n--- 9. Connection Events ---");

    let _conn_rx = device.connection_events();
    println!("  Subscribed to connection state changes");
    pause().await;

    // =========================================================================
    // 10. Extras — Click
    // =========================================================================
    println!("\n--- 10. Extras: Click ---");

    println!("  click(Middle, 50ms)...");
    device
        .click(Button::Middle, Duration::from_millis(50))
        .await?;
    pause().await;

    println!("  click_sequence(Middle, 50ms hold, x3, 200ms interval)...");
    device
        .click_sequence(
            Button::Middle,
            Duration::from_millis(50),
            3,
            Duration::from_millis(200),
        )
        .await?;
    pause().await;

    // =========================================================================
    // 11. Extras — Smooth Movement
    // =========================================================================
    println!("\n--- 11. Extras: Smooth Movement ---");

    println!("  move_smooth: right 150 (15 steps)...");
    device
        .move_smooth(150, 0, 15, Duration::from_millis(20))
        .await?;
    pause().await;

    println!("  move_smooth: back left 150...");
    device
        .move_smooth(-150, 0, 15, Duration::from_millis(20))
        .await?;
    pause().await;

    println!("  drag(Left): right 100 (10 steps)...");
    device
        .drag(Button::Left, 100, 0, 10, Duration::from_millis(20))
        .await?;
    pause().await;

    println!("  drag(Left): back left 100...");
    device
        .drag(Button::Left, -100, 0, 10, Duration::from_millis(20))
        .await?;
    pause().await;

    println!("  move_pattern: square (40px sides)...");
    let waypoints = vec![(40, 0), (0, 40), (-40, 0), (0, -40)];
    device
        .move_pattern(&waypoints, 5, Duration::from_millis(20))
        .await?;
    pause().await;

    // =========================================================================
    // 12. Extras — Event Callbacks
    // =========================================================================
    println!("\n--- 12. Extras: Event Callbacks ---");

    // on_button_press / on_button_event auto-enable the button stream.
    let h1: EventHandle = device
        .on_button_press(Button::Left, |pressed| {
            println!(
                "    [callback] Left: {}",
                if pressed { "down" } else { "up" }
            );
        })
        .await?;
    println!("  on_button_press(Left) registered");

    let h2: EventHandle = device
        .on_button_event(|mask| {
            println!("    [callback] mask: {:#04x}", mask.raw());
        })
        .await?;
    println!("  on_button_event() registered");
    println!("  Press mouse buttons to see callbacks fire...");
    tokio::time::sleep(Duration::from_secs(3)).await;

    drop(h1);
    drop(h2);
    println!("  Handles dropped — callbacks unregistered");

    device.disable_button_stream().await?;
    pause().await;

    // =========================================================================
    // 13. Batch Builder — native commands (coalesced into single write)
    // =========================================================================
    println!("\n--- 13. Batch: Native (coalesced) ---");

    println!("  Executing 13 coalesced commands...");
    device
        .batch()
        .move_xy(30, 0)
        .move_xy(0, 30)
        .move_xy(-30, 0)
        .move_xy(0, -30)
        .silent_move(5, 5)
        .silent_move(-5, -5)
        .wheel(1)
        .wheel(-1)
        .button_down(Button::Left)
        .button_up(Button::Left)
        .button_up_force(Button::Right)
        .set_lock(LockTarget::X, true)
        .set_lock(LockTarget::X, false)
        .execute()
        .await?;
    println!("  Done — all 13 sent in one write_all()");
    pause().await;

    // =========================================================================
    // 14. Batch Builder — mixed (native + extras, timing breaks coalescing)
    // =========================================================================
    println!("\n--- 14. Batch: Mixed (native + extras) ---");

    println!("  Executing mixed batch...");
    device
        .batch()
        .move_xy(10, 0)
        .click(Button::Middle, Duration::from_millis(30))
        .move_smooth(50, 0, 5, Duration::from_millis(15))
        .click_sequence(
            Button::Middle,
            Duration::from_millis(30),
            2,
            Duration::from_millis(100),
        )
        .move_pattern(
            vec![(20, 0), (0, 20), (-20, 0), (0, -20)],
            3,
            Duration::from_millis(15),
        )
        .drag(Button::Left, 40, 0, 4, Duration::from_millis(15))
        .move_xy(-120, -20)
        .button_up(Button::Left)
        .execute()
        .await?;
    println!("  Done — native coalesced where possible, extras inline");
    pause().await;

    // =========================================================================
    // 15. Profiler
    // =========================================================================
    println!("\n--- 15. Profiler ---");

    // timed! macro wraps a command and records its duration
    println!("  Profiling a move_xy...");
    let _r = makxd::timed!("profiled_move", device.move_xy(5, 0).await);
    device.move_xy(-5, 0).await?;

    let stats = makxd::profiler::stats();
    println!("  {} command types profiled:", stats.len());
    for (name, stat) in &stats {
        println!(
            "    {}: count={}, avg={}us, min={}us, max={}us",
            name, stat.count, stat.avg_us as u64, stat.min_us, stat.max_us
        );
    }

    makxd::profiler::reset();
    println!("  Profiler reset");
    pause().await;

    // =========================================================================
    // 16. Error Types
    // =========================================================================
    println!("\n--- 16. Error Types ---");

    let errors: Vec<MakxdError> = vec![
        MakxdError::Timeout,
        MakxdError::NotFound,
        MakxdError::Disconnected,
        MakxdError::Protocol("example".into()),
    ];
    for e in &errors {
        println!("  {:?}: {}", e, e);
    }
    pause().await;

    // =========================================================================
    // 17. Disconnect
    // =========================================================================
    println!("\n--- 17. Disconnect ---");

    device.disconnect();
    println!("  is_connected: {}", device.is_connected());

    println!("\n=== All features demonstrated! ===");
    Ok(())
}
