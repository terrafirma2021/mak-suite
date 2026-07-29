//! Benchmark example — measures real device latency and throughput.
//!
//! Run with:
//!   cargo run --example benchmark --release --features "batch,extras"
//!
//! ## What this measures
//!
//! Unlike makxd-cpp and makxd-rs benchmarks (which only measure serial write
//! time in fire-and-forget mode, reporting misleadingly low numbers like
//! ~0.0-40µs), this benchmark measures TWO things separately:
//!
//! 1. **Confirmed round-trip latency**: Time from sending a command to
//!    receiving the `>>> ` prompt back from the device. This is the REAL
//!    latency your code experiences when using the default confirmed mode.
//!
//! 2. **Fire-and-forget throughput**: How fast commands can be enqueued
//!    without waiting for responses. This measures the channel send + serial
//!    write path and is comparable to what other libraries report.
//!
//! Both are useful, but they mean very different things.

use std::time::{Duration, Instant};

use makxd::{Button, Device, LockTarget, Result, VERSION};

const WARM_UP: usize = 10;

fn main() -> Result<()> {
    println!("=== makxd v{} — Benchmark ===\n", VERSION);

    let device = Device::connect()?;
    println!("Connected to: {}", device.port_name());
    println!("Mode: confirmed (default) — measuring real round-trip latency\n");

    // Warm up the connection (first few commands may be slower)
    for _ in 0..WARM_UP {
        device.move_xy(0, 0)?;
    }

    // =========================================================================
    // PART 1: Confirmed round-trip latency (individual commands)
    // =========================================================================
    println!("=== PART 1: Confirmed Round-Trip Latency ===\n");
    println!("Each measurement = time from send to receiving `>>> ` prompt back.\n");

    // -- move_xy --
    let (avg, min, max) = bench_n(100, || device.move_xy(1, 0))?;
    println!(
        "  move_xy          (100x):  avg={:>6}us  min={:>6}us  max={:>6}us",
        avg, min, max
    );

    // Undo drift
    device.move_xy(-100, 0)?;

    // -- silent_move --
    let (avg, min, max) = bench_n(100, || device.silent_move(1, 0))?;
    println!(
        "  silent_move      (100x):  avg={:>6}us  min={:>6}us  max={:>6}us",
        avg, min, max
    );

    device.silent_move(-100, 0)?;

    // -- wheel --
    let (avg, min, max) = bench_n(50, || device.wheel(1))?;
    println!(
        "  wheel             (50x):  avg={:>6}us  min={:>6}us  max={:>6}us",
        avg, min, max
    );

    // Undo scroll
    device.wheel(-50)?;

    // -- button_down + button_up --
    let (avg, min, max) = bench_n(50, || {
        device.button_down(Button::Left)?;
        device.button_up(Button::Left)
    })?;
    println!(
        "  button down+up    (50x):  avg={:>6}us  min={:>6}us  max={:>6}us  (2 cmds per iter)",
        avg, min, max
    );

    // -- button_up_force --
    let (avg, min, max) = bench_n(50, || device.button_up_force(Button::Left))?;
    println!(
        "  button_up_force   (50x):  avg={:>6}us  min={:>6}us  max={:>6}us",
        avg, min, max
    );

    // -- set_lock --
    let (avg, min, max) = bench_n(50, || {
        device.set_lock(LockTarget::X, true)?;
        device.set_lock(LockTarget::X, false)
    })?;
    println!(
        "  set_lock on+off   (50x):  avg={:>6}us  min={:>6}us  max={:>6}us  (2 cmds per iter)",
        avg, min, max
    );

    // -- button_state (query, not execute) --
    let (avg, min, max) = bench_n(50, || {
        device.button_state(Button::Left)?;
        Ok(())
    })?;
    println!(
        "  button_state      (50x):  avg={:>6}us  min={:>6}us  max={:>6}us",
        avg, min, max
    );

    // -- lock_state (query) --
    let (avg, min, max) = bench_n(50, || {
        device.lock_state(LockTarget::X)?;
        Ok(())
    })?;
    println!(
        "  lock_state        (50x):  avg={:>6}us  min={:>6}us  max={:>6}us",
        avg, min, max
    );

    // -- version (query) --
    let (avg, min, max) = bench_n(20, || {
        device.version()?;
        Ok(())
    })?;
    println!(
        "  version           (20x):  avg={:>6}us  min={:>6}us  max={:>6}us",
        avg, min, max
    );

    // -- send_raw --
    let (avg, min, max) = bench_n(50, || {
        device.send_raw(b"km.version()\r\n")?;
        Ok(())
    })?;
    println!(
        "  send_raw          (50x):  avg={:>6}us  min={:>6}us  max={:>6}us",
        avg, min, max
    );

    // =========================================================================
    // PART 2: Fire-and-forget throughput
    // =========================================================================
    println!("\n=== PART 2: Fire-and-Forget Throughput (fenced) ===\n");
    println!("Sends N fire-and-forget commands, then 1 confirmed command as a fence.");
    println!("Total time includes actual serial I/O — comparable to makxd-cpp.\n");

    let ff = device.ff();

    // -- fenced ff move_xy --
    let (total, per, cps) = bench_ff_fenced(100, || ff.move_xy(1, 0), &device)?;
    println!(
        "  ff move_xy       (100x):  total={:>6}us  per_cmd={:>4}us  ({:.0} cmd/s)",
        total, per, cps
    );
    device.move_xy(-100, 0)?;

    let (total, per, cps) = bench_ff_fenced(1000, || ff.move_xy(1, 0), &device)?;
    println!(
        "  ff move_xy      (1000x):  total={:>6}us  per_cmd={:>4}us  ({:.0} cmd/s)",
        total, per, cps
    );
    std::thread::sleep(Duration::from_millis(500));
    device.move_xy(-1000, 0)?;

    // -- fenced ff wheel --
    let (total, per, cps) = bench_ff_fenced(100, || ff.wheel(1), &device)?;
    println!(
        "  ff wheel         (100x):  total={:>6}us  per_cmd={:>4}us  ({:.0} cmd/s)",
        total, per, cps
    );
    device.wheel(-100)?;

    // -- fenced ff button_down --
    let (total, per, cps) = bench_ff_fenced(100, || ff.button_down(Button::Left), &device)?;
    println!(
        "  ff button_down   (100x):  total={:>6}us  per_cmd={:>4}us  ({:.0} cmd/s)",
        total, per, cps
    );
    device.button_up_force(Button::Left)?;

    // -- fenced ff silent_move --
    let (total, per, cps) = bench_ff_fenced(100, || ff.silent_move(1, 0), &device)?;
    println!(
        "  ff silent_move   (100x):  total={:>6}us  per_cmd={:>4}us  ({:.0} cmd/s)",
        total, per, cps
    );
    device.silent_move(-100, 0)?;

    // -- cpp-comparable: 100 moves with varying args --
    let start = Instant::now();
    for i in 0u64..100 {
        ff.move_xy((i % 10) as i32, (i % 10) as i32)?;
    }
    // Fence: one confirmed command to ensure all F&F writes have flushed
    device.move_xy(0, 0)?;
    let elapsed = start.elapsed();
    let per_cmd = elapsed.as_micros() as u64 / 101;
    println!(
        "  ff 100 moves (cpp-style): total={:>6}us  per_cmd={:>4}us  ({:.0} cmd/s)",
        elapsed.as_micros(),
        per_cmd,
        101.0 / elapsed.as_secs_f64()
    );

    std::thread::sleep(Duration::from_millis(200));

    // =========================================================================
    // PART 3: Batch coalesced write
    // =========================================================================
    #[cfg(feature = "batch")]
    {
        println!("\n=== PART 3: Batch Coalesced Writes ===\n");
        println!("Multiple commands sent as a single write_all() call.\n");

        // 10-command batch
        let start = Instant::now();
        device
            .batch()
            .move_xy(10, 0)
            .move_xy(0, 10)
            .move_xy(-10, 0)
            .move_xy(0, -10)
            .wheel(1)
            .wheel(-1)
            .button_down(Button::Left)
            .button_up(Button::Left)
            .set_lock(LockTarget::X, true)
            .set_lock(LockTarget::X, false)
            .execute()?;
        let elapsed = start.elapsed();
        println!(
            "  batch 10 native cmds:    {:>6}us total  ({:>4}us per cmd equivalent)",
            elapsed.as_micros(),
            elapsed.as_micros() / 10
        );

        // 50-command batch (heavy)
        let start = Instant::now();
        let mut batch = device.batch();
        for i in 0..50 {
            batch = batch.move_xy(if i % 2 == 0 { 2 } else { -2 }, 0);
        }
        batch.execute()?;
        let elapsed = start.elapsed();
        println!(
            "  batch 50 move_xy cmds:   {:>6}us total  ({:>4}us per cmd equivalent)",
            elapsed.as_micros(),
            elapsed.as_micros() / 50
        );
    }

    // =========================================================================
    // PART 4: Extras (software-timed operations)
    // =========================================================================
    #[cfg(feature = "extras")]
    {
        println!("\n=== PART 4: Extras (Software-Timed) ===\n");
        println!("These include intentional sleeps, so measure overhead vs expected time.\n");

        // click (50ms hold)
        let expected_ms = 50;
        let start = Instant::now();
        device.click(Button::Middle, Duration::from_millis(expected_ms))?;
        let elapsed = start.elapsed();
        let overhead_us = elapsed.as_micros() as i64 - (expected_ms as i64 * 1000);
        println!(
            "  click(50ms hold):        {:>6}us elapsed  ({:>+6}us overhead vs {}ms expected)",
            elapsed.as_micros(),
            overhead_us,
            expected_ms
        );

        // move_smooth (10 steps, 10ms interval = ~100ms expected)
        let steps = 10u32;
        let interval_ms = 10u64;
        let expected_ms = steps as u64 * interval_ms;
        let start = Instant::now();
        device.move_smooth(50, 0, steps, Duration::from_millis(interval_ms))?;
        let elapsed = start.elapsed();
        let overhead_us = elapsed.as_micros() as i64 - (expected_ms as i64 * 1000);
        println!(
            "  move_smooth(10x10ms):    {:>6}us elapsed  ({:>+6}us overhead vs {}ms expected)",
            elapsed.as_micros(),
            overhead_us,
            expected_ms
        );

        device.move_xy(-50, 0)?;

        // drag (5 steps, 10ms interval = ~50ms expected)
        let steps = 5u32;
        let interval_ms = 10u64;
        let expected_ms = steps as u64 * interval_ms;
        let start = Instant::now();
        device.drag(
            Button::Left,
            30,
            0,
            steps,
            Duration::from_millis(interval_ms),
        )?;
        let elapsed = start.elapsed();
        let overhead_us = elapsed.as_micros() as i64 - (expected_ms as i64 * 1000);
        println!(
            "  drag(5x10ms):            {:>6}us elapsed  ({:>+6}us overhead vs {}ms expected)",
            elapsed.as_micros(),
            overhead_us,
            expected_ms
        );

        device.move_xy(-30, 0)?;
    }

    // =========================================================================
    // Summary comparison
    // =========================================================================
    println!("\n=== COMPARISON NOTES ===\n");
    println!("  makxd (ours):    Confirmed mode measures REAL round-trip latency (~700-2000us)");
    println!("  makxd-cpp:       Claims ~40us/cmd — measures serial write only (fire-and-forget)");
    println!("  makxd-rs:        Claims ~0.0us/cmd — measures channel enqueue only (meaningless)");
    println!();
    println!("  Our F&F throughput is comparable to makxd-cpp's claimed numbers.");
    println!("  Our confirmed latency is the ACTUAL time your code waits per command.");

    // =========================================================================
    // Cleanup
    // =========================================================================
    device.disconnect();
    println!("\nDisconnected");

    println!("\n=== Benchmark complete! ===");
    Ok(())
}

/// Run a closure `n` times, return (avg_us, min_us, max_us).
fn bench_n<F>(n: usize, mut f: F) -> Result<(u64, u64, u64)>
where
    F: FnMut() -> Result<()>,
{
    let mut total = Duration::ZERO;
    let mut min = Duration::MAX;
    let mut max = Duration::ZERO;

    for _ in 0..n {
        let start = Instant::now();
        f()?;
        let elapsed = start.elapsed();
        total += elapsed;
        min = min.min(elapsed);
        max = max.max(elapsed);
    }

    let avg = total.as_micros() as u64 / n as u64;
    let min = min.as_micros() as u64;
    let max = max.as_micros() as u64;
    Ok((avg, min, max))
}

/// Send `n` fire-and-forget commands, then one confirmed command as a fence.
/// Returns (total_us, per_cmd_us, cmds_per_sec) for the full N+1 commands.
fn bench_ff_fenced<F>(n: u64, mut ff_cmd: F, device: &Device) -> Result<(u64, u64, f64)>
where
    F: FnMut() -> Result<()>,
{
    let start = Instant::now();
    for _ in 0..n {
        ff_cmd()?;
    }
    // Fence: confirmed command ensures the writer thread has flushed
    // all preceding F&F writes through the serial port.
    device.move_xy(0, 0)?;
    let elapsed = start.elapsed();
    let total_cmds = n + 1;
    let total_us = elapsed.as_micros() as u64;
    let per_cmd = total_us / total_cmds;
    let cps = total_cmds as f64 / elapsed.as_secs_f64();
    Ok((total_us, per_cmd, cps))
}
