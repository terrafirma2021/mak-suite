use std::collections::HashMap;
use std::time::Duration;

/// Per-command timing statistics.
#[derive(Debug, Clone)]
pub struct CommandStat {
    pub count: u64,
    pub total_us: u64,
    pub avg_us: f64,
    pub min_us: u64,
    pub max_us: u64,
}

#[cfg(feature = "profile")]
mod inner {
    use super::*;
    use std::sync::Mutex;

    static STATS: Mutex<Option<HashMap<&'static str, CommandStat>>> = Mutex::new(None);

    pub fn record(command: &'static str, elapsed: Duration) {
        let us = elapsed.as_micros() as u64;
        let mut guard = STATS.lock().unwrap();
        let map = guard.get_or_insert_with(HashMap::new);
        let stat = map.entry(command).or_insert(CommandStat {
            count: 0,
            total_us: 0,
            avg_us: 0.0,
            min_us: u64::MAX,
            max_us: 0,
        });
        stat.count += 1;
        stat.total_us += us;
        stat.avg_us = stat.total_us as f64 / stat.count as f64;
        stat.min_us = stat.min_us.min(us);
        stat.max_us = stat.max_us.max(us);
    }

    pub fn stats() -> HashMap<&'static str, CommandStat> {
        let guard = STATS.lock().unwrap();
        guard.clone().unwrap_or_default()
    }

    pub fn reset() {
        let mut guard = STATS.lock().unwrap();
        *guard = None;
    }
}

#[cfg(not(feature = "profile"))]
mod inner {
    use super::*;

    #[inline(always)]
    pub fn record(_command: &'static str, _elapsed: Duration) {}

    #[inline(always)]
    pub fn stats() -> HashMap<&'static str, CommandStat> {
        HashMap::new()
    }

    #[inline(always)]
    pub fn reset() {}
}

pub use inner::{record, reset, stats};

/// Convenience macro — times the expression and records it.
/// Compiles to just the expression when the `profile` feature is off.
#[macro_export]
macro_rules! timed {
    ($name:expr, $expr:expr) => {{
        #[cfg(feature = "profile")]
        {
            let _start = ::std::time::Instant::now();
            let _result = $expr;
            $crate::profiler::record($name, _start.elapsed());
            _result
        }
        #[cfg(not(feature = "profile"))]
        {
            $expr
        }
    }};
}
