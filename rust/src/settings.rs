//! Live device settings. Apply/import do not save or reboot; Save persists to
//! NOR. Export authenticates the current snapshot without requiring Save.
use crate::transport::TransportHandle;
use crate::{ApiOpcode, MakxdError, Result};
use sha2::{Digest, Sha256};
use std::time::{Duration, Instant};

pub const CONTROLLER: u8 = 1;
pub const TRANSLATION: u8 = 2;
pub const MOUSE: u8 = 4;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ControllerChannel {
    RightStick = 0,
    LeftStick = 1,
    LeftTrigger = 2,
    RightTrigger = 3,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ControllerCurve {
    pub center_deadzone_percent: u8,
    pub anti_deadzone_percent: u8,
    pub change_deadband_percent: u8,
    pub points: [(u8, u8); 5],
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ControllerBehavior {
    pub name: String,
    pub strength_percent: Option<u8>,
    pub curve_enabled: bool,
    pub advanced_enabled: bool,
    pub inertia_percent: u8,
    pub micro_percent: u8,
    pub limit_percent: u8,
    pub magnitude_variance_percent: u8,
    pub angle_variance_percent: u8,
    pub curves: [ControllerCurve; 4],
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Interpolation {
    Off = 0,
    Fixed = 1,
    Auto = 2,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ControllerSettings {
    pub interpolation: Interpolation,
    pub buffer_ms: u8,
    pub timing_variance_percent: u8,
    pub behaviors: [Option<ControllerBehavior>; 4],
    pub curve_enabled: bool,
    pub legacy_strengths: [u8; 3],
    pub selected_profile: u8,
    pub profile_count: u8,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ControllerTranslation {
    pub enabled: bool,
    pub scale: u16,
    pub timeout_ms: u16,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DeviceSettings {
    pub controller: ControllerSettings,
    /// LEFT stick/WASD, RIGHT stick/mouse, LT, RT.
    pub translation: [ControllerTranslation; 4],
    pub mouse_spread_percent: u8,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SettingsInfo {
    pub sections: u8,
    pub kinds: u8,
    pub save_state: u8,
    pub revision: u32,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SettingsSnapshot {
    pub info: SettingsInfo,
    pub settings: DeviceSettings,
}
fn check(value: bool) -> Result<()> {
    if value {
        Ok(())
    } else {
        Err(MakxdError::Settings(4))
    }
}
fn u16(p: &[u8], at: usize) -> u16 {
    u16::from_le_bytes([p[at], p[at + 1]])
}
fn u32(p: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(p[at..at + 4].try_into().unwrap())
}
fn put16(p: &mut [u8], at: usize, v: u16) {
    p[at..at + 2].copy_from_slice(&v.to_le_bytes());
}
fn put32(p: &mut [u8], at: usize, v: u32) {
    p[at..at + 4].copy_from_slice(&v.to_le_bytes());
}
impl ControllerSettings {
    /// Expand an older policy to four independent behaviors before editing.
    pub fn behavior(&mut self, channel: ControllerChannel) -> Result<&mut ControllerBehavior> {
        if self.profile_count < 4 {
            let original = self.behaviors[0].clone().ok_or(MakxdError::Settings(4))?;
            for i in 0..4 {
                let mut b = self.behaviors[i].clone().unwrap_or_else(|| {
                    if i == 3 {
                        self.behaviors[2].clone().unwrap_or(original.clone())
                    } else {
                        original.clone()
                    }
                });
                b.curves[i] = original.curves[i].clone();
                b.strength_percent = Some(
                    b.strength_percent
                        .unwrap_or(self.legacy_strengths[i.min(2)]),
                );
                b.curve_enabled |= self.curve_enabled;
                self.behaviors[i] = Some(b);
            }
            self.profile_count = 4;
            self.selected_profile = 0;
        }
        self.behaviors[channel as usize]
            .as_mut()
            .ok_or(MakxdError::Settings(4))
    }
}
impl DeviceSettings {
    pub(crate) fn encode(&self) -> Result<[u8; 400]> {
        let c = &self.controller;
        let mut p = [0u8; 400];
        check(
            (1..=64).contains(&c.buffer_ms)
                && c.timing_variance_percent <= 100
                && (1..=4).contains(&c.profile_count)
                && c.selected_profile < c.profile_count,
        )?;
        let mut flags = u32::from(c.curve_enabled) | (u32::from(c.timing_variance_percent) << 22);
        for (value, shift) in c.legacy_strengths.iter().zip([1, 8, 15]) {
            check(*value <= 100)?;
            flags |= u32::from(*value) << shift;
        }
        put32(&mut p, 0, flags);
        put32(&mut p, 4, c.interpolation as u32);
        put32(&mut p, 8, c.buffer_ms.into());
        put32(&mut p, 12, c.selected_profile.into());
        put32(&mut p, 16, c.profile_count.into());
        for i in 0..4 {
            let Some(b) = &c.behaviors[i] else {
                check(i >= usize::from(c.profile_count))?;
                continue;
            };
            let at = 20 + 88 * i;
            let name = b.name.as_bytes();
            check(
                (1..=24).contains(&name.len())
                    && name.iter().all(|v| (32..=126).contains(v))
                    && [
                        b.inertia_percent,
                        b.micro_percent,
                        b.limit_percent,
                        b.magnitude_variance_percent,
                        b.angle_variance_percent,
                        b.strength_percent.unwrap_or(0),
                    ]
                    .iter()
                    .all(|v| *v <= 100),
            )?;
            let flags = 1
                | (u32::from(b.inertia_percent) << 1)
                | (u32::from(b.micro_percent) << 8)
                | (u32::from(b.limit_percent) << 15)
                | (u32::from(b.advanced_enabled) << 22)
                | (u32::from(b.curve_enabled) << 23);
            put32(&mut p, at, flags);
            p[at + 4] = name.len() as u8;
            p[at + 5] = b.strength_percent.map_or(15, |s| 128 | s);
            p[at + 6] = b.magnitude_variance_percent;
            p[at + 7] = b.angle_variance_percent;
            p[at + 8..at + 8 + name.len()].copy_from_slice(name);
            for (axis, curve) in b.curves.iter().enumerate() {
                check(
                    curve.center_deadzone_percent <= 50
                        && curve.anti_deadzone_percent <= 50
                        && curve.change_deadband_percent <= 50
                        && curve.points[0] == (0, 0)
                        && curve.points[4] == (100, 100),
                )?;
                let pos = at + 32 + 14 * axis;
                p[pos..pos + 4].copy_from_slice(&[
                    curve.center_deadzone_percent,
                    curve.anti_deadzone_percent,
                    curve.change_deadband_percent,
                    5,
                ]);
                for (point, &(x, y)) in curve.points.iter().enumerate() {
                    check(
                        x <= 100
                            && y <= 100
                            && (point == 0
                                || (x > curve.points[point - 1].0
                                    && y >= curve.points[point - 1].1)),
                    )?;
                    p[pos + 4 + 2 * point] = x;
                    p[pos + 5 + 2 * point] = y;
                }
            }
        }
        for (i, m) in self.translation.iter().enumerate() {
            check(
                m.scale <= if i < 2 { 512 } else { 100 }
                    && (i >= 2 || m.scale > 0)
                    && (1..=1000).contains(&m.timeout_ms),
            )?;
            put16(&mut p, 372 + 6 * i, u16::from(m.enabled));
            put16(&mut p, 374 + 6 * i, m.scale);
            put16(&mut p, 376 + 6 * i, m.timeout_ms);
        }
        check(self.mouse_spread_percent <= 100)?;
        p[396] = self.mouse_spread_percent;
        Ok(p)
    }
    pub(crate) fn decode(p: &[u8]) -> Result<Self> {
        check(p.len() == 400 && p[397..].iter().all(|v| *v == 0))?;
        let flags = u32(p, 0);
        check(
            flags & !0x1fffffff == 0
                && u32(p, 4) <= 2
                && u32(p, 8) <= 64
                && u32(p, 12) < 4
                && u32(p, 16) <= 4,
        )?;
        let mut behaviors: [Option<ControllerBehavior>; 4] = std::array::from_fn(|_| None);
        for (i, entry) in behaviors.iter_mut().enumerate() {
            let at = 20 + 88 * i;
            let flags = u32(p, at);
            if flags == 0 {
                check(p[at..at + 88].iter().all(|v| *v == 0))?;
                continue;
            }
            check(
                flags & !0xffffff == 0
                    && flags & 1 != 0
                    && (1..=24).contains(&p[at + 4])
                    && (p[at + 5] == 15 || p[at + 5] & 128 != 0),
            )?;
            let mut curves = Vec::new();
            for axis in 0..4 {
                let pos = at + 32 + 14 * axis;
                check(p[pos + 3] == 5)?;
                curves.push(ControllerCurve {
                    center_deadzone_percent: p[pos],
                    anti_deadzone_percent: p[pos + 1],
                    change_deadband_percent: p[pos + 2],
                    points: std::array::from_fn(|n| (p[pos + 4 + 2 * n], p[pos + 5 + 2 * n])),
                });
            }
            *entry = Some(ControllerBehavior {
                name: String::from_utf8(p[at + 8..at + 8 + usize::from(p[at + 4])].to_vec())
                    .map_err(|_| MakxdError::Settings(4))?,
                strength_percent: if p[at + 5] == 15 {
                    None
                } else {
                    Some(p[at + 5] & 127)
                },
                curve_enabled: flags & (1 << 23) != 0,
                advanced_enabled: flags & (1 << 22) != 0,
                inertia_percent: ((flags >> 1) & 127) as u8,
                micro_percent: ((flags >> 8) & 127) as u8,
                limit_percent: ((flags >> 15) & 127) as u8,
                magnitude_variance_percent: p[at + 6],
                angle_variance_percent: p[at + 7],
                curves: curves.try_into().unwrap(),
            });
        }
        for i in 0..4 {
            check(u16(p, 372 + 6 * i) <= 1)?;
        }
        let result = Self {
            controller: ControllerSettings {
                interpolation: match u32(p, 4) {
                    0 => Interpolation::Off,
                    1 => Interpolation::Fixed,
                    _ => Interpolation::Auto,
                },
                buffer_ms: u32(p, 8) as u8,
                timing_variance_percent: ((flags >> 22) & 127) as u8,
                behaviors,
                curve_enabled: flags & 1 != 0,
                legacy_strengths: [1, 8, 15].map(|s| ((flags >> s) & 127) as u8),
                selected_profile: u32(p, 12) as u8,
                profile_count: u32(p, 16) as u8,
            },
            translation: std::array::from_fn(|i| ControllerTranslation {
                enabled: u16(p, 372 + 6 * i) == 1,
                scale: u16(p, 374 + 6 * i),
                timeout_ms: u16(p, 376 + 6 * i),
            }),
            mouse_spread_percent: p[396],
        };
        result.encode()?;
        Ok(result)
    }
}
type Query<'a> = dyn FnMut(&[u8]) -> Result<Vec<u8>> + 'a;
fn command(q: &mut Query<'_>, record: u8, op: u8, data: &[u8], pending: bool) -> Result<Vec<u8>> {
    let mut p = vec![record, op];
    p.extend_from_slice(data);
    let r = q(&p)?;
    if r.len() < 3 || r[..2] != [record, op] {
        return Err(MakxdError::Settings(5));
    }
    if r[2] != 0 && !(pending && r[2] == 1) {
        return Err(MakxdError::Settings(r[2]));
    }
    Ok(r)
}
fn selection(info: SettingsInfo, sections: u8) -> Result<[u8; 5]> {
    let mask = if sections == 0 {
        info.sections
    } else {
        sections
    };
    if mask == 0 || mask & !info.sections != 0 {
        return Err(MakxdError::Settings(5));
    }
    let mut p = [0u8; 5];
    put32(&mut p, 0, info.revision);
    p[4] = mask;
    Ok(p)
}
fn info(q: &mut Query<'_>) -> Result<SettingsInfo> {
    let p = command(q, 0x1d, 0, &[], false)?;
    check(
        p.len() == 14
            && p[3] == 1
            && p[4] & !7 == 0
            && p[5] & !7 == 0
            && p[7] == 0
            && u16(&p, 12) == 400,
    )?;
    Ok(SettingsInfo {
        sections: p[4],
        kinds: p[5],
        save_state: p[6],
        revision: u32(&p, 8),
    })
}
fn read(q: &mut Query<'_>) -> Result<SettingsSnapshot> {
    let info = info(q)?;
    let mut image = [0u8; 400];
    for offset in (0..400).step_by(96) {
        let len = (400 - offset).min(96);
        let mut p = [0u8; 7];
        put32(&mut p, 0, info.revision);
        put16(&mut p, 4, offset as u16);
        p[6] = len as u8;
        let r = command(q, 0x1d, 1, &p, false)?;
        check(
            r.len() == 9 + len && u32(&r, 3) == info.revision && usize::from(u16(&r, 7)) == offset,
        )?;
        image[offset..offset + len].copy_from_slice(&r[9..]);
    }
    Ok(SettingsSnapshot {
        info,
        settings: DeviceSettings::decode(&image)?,
    })
}
fn apply(q: &mut Query<'_>, value: &SettingsSnapshot, sections: u8) -> Result<SettingsSnapshot> {
    let image = value.settings.encode()?;
    let begin = command(q, 0x1d, 2, &selection(value.info, sections)?, false)?;
    check(begin.len() == 7)?;
    let token = &begin[3..7];
    let result = (|| -> Result<()> {
        for offset in (0..400).step_by(96) {
            let len = (400 - offset).min(96);
            let mut p = token.to_vec();
            p.extend_from_slice(&(offset as u16).to_le_bytes());
            p.extend_from_slice(&image[offset..offset + len]);
            let r = command(q, 0x1d, 3, &p, false)?;
            check(r.len() == 5 && usize::from(u16(&r, 3)) == offset + len)?;
        }
        command(q, 0x1d, 4, token, false)?;
        Ok(())
    })();
    if let Err(error) = result {
        let _ = command(q, 0x1d, 6, token, false);
        return Err(error);
    }
    read(q)
}
fn save(q: &mut Query<'_>, value: &SettingsSnapshot, sections: u8) -> Result<()> {
    command(q, 0x1d, 5, &selection(value.info, sections)?, true)?;
    let deadline = Instant::now() + Duration::from_secs(15);
    loop {
        match info(q)?.save_state {
            0 => return Ok(()),
            1 => {}
            state => return Err(MakxdError::Settings(state)),
        };
        if Instant::now() >= deadline {
            return Err(MakxdError::Timeout);
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}
fn export(q: &mut Query<'_>, value: &SettingsSnapshot, sections: u8) -> Result<Vec<u8>> {
    let current = read(q)?;
    if current.info.revision != value.info.revision {
        return Err(MakxdError::Settings(3));
    }
    let value = &current;
    let mut plain = vec![
        77,
        75,
        68,
        83,
        1,
        selection(value.info, sections)?[4],
        value.info.kinds,
        0,
    ];
    let mut random = [0u8; 16];
    getrandom::fill(&mut random).map_err(|_| MakxdError::Settings(6))?;
    plain.extend(random);
    plain.extend(value.settings.encode()?);
    let hash = Sha256::digest(&plain);
    let mut file = Vec::new();
    for (index, chunk) in plain.chunks(96).enumerate() {
        let mut p = hash.to_vec();
        p.extend((plain.len() as u16).to_le_bytes());
        p.extend((index as u16).to_le_bytes());
        p.extend((chunk.len() as u16).to_le_bytes());
        p.extend(chunk);
        let mut attempts = 0;
        let r = loop {
            match command(q, 0x1b, 1, &p, false) {
                Ok(r) => break r,
                Err(MakxdError::Settings(2)) if attempts < 99 => {
                    attempts += 1;
                    std::thread::sleep(Duration::from_millis(10));
                }
                Err(e) => return Err(e),
            }
        };
        check(
            r.len() == 76 + chunk.len()
                && r[3..9] == [77, 75, 83, 69, 1, 1]
                && r[22..60] == p[..38],
        )?;
        file.extend_from_slice(&r[3..]);
    }
    Ok(file)
}
fn import(q: &mut Query<'_>, file: &[u8]) -> Result<SettingsSnapshot> {
    let mut current = read(q)?;
    check((74..=30000).contains(&file.len()))?;
    let total = usize::from(u16(file, 51));
    check((1..=16384).contains(&total))?;
    let mut packets = Vec::new();
    let mut offset = 0;
    for (index, start) in (0..total).step_by(96).enumerate() {
        let len = (total - start).min(96);
        check(offset + 73 + len <= file.len())?;
        let p = &file[offset..offset + 73 + len];
        check(
            p[..6] == [77, 75, 83, 69, 1, 1]
                && p[19..51] == file[19..51]
                && usize::from(u16(p, 51)) == total
                && usize::from(u16(p, 53)) == index
                && usize::from(u16(p, 55)) == len,
        )?;
        packets.push(p);
        offset += p.len();
    }
    check(offset == file.len())?;
    let mut plain = Vec::new();
    for p in packets {
        let r = command(q, 0x1b, 2, p, false)?;
        check(r.len() == 3 + usize::from(u16(p, 55)))?;
        plain.extend_from_slice(&r[3..]);
    }
    check(
        Sha256::digest(&plain).as_slice() == &file[19..51]
            && plain.len() == 424
            && plain[..5] == [77, 75, 68, 83, 1]
            && plain[5] != 0
            && plain[6] & !7 == 0
            && plain[7] == 0,
    )?;
    selection(current.info, plain[5])?;
    current.settings = DeviceSettings::decode(&plain[24..])?;
    apply(q, &current, plain[5])
}
#[derive(Clone)]
pub struct DeviceConfiguration {
    transport: TransportHandle,
    timeout: Duration,
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{BufRead, BufReader, Write};
    use std::process::{Command, Stdio};
    #[test]
    fn settings_firmware_roundtrip() {
        let Ok(path) = std::env::var("MAKXD_SETTINGS_PEER") else {
            return;
        };
        let mut child = Command::new(path)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .unwrap();
        let mut input = child.stdin.take().unwrap();
        let mut output = BufReader::new(child.stdout.take().unwrap());
        let mut query = |p: &[u8]| -> Result<Vec<u8>> {
            for b in p {
                write!(input, "{b:02x}").unwrap();
            }
            writeln!(input).unwrap();
            input.flush().unwrap();
            let mut line = String::new();
            output.read_line(&mut line).unwrap();
            let line = line.trim();
            Ok((0..line.len())
                .step_by(2)
                .map(|at| u8::from_str_radix(&line[at..at + 2], 16).unwrap())
                .collect())
        };
        let mut s = read(&mut query).unwrap();
        let original = s.settings.encode().unwrap();
        s.settings.controller.buffer_ms = 23;
        s.settings
            .controller
            .behavior(ControllerChannel::RightStick)
            .unwrap()
            .strength_percent = Some(67);
        s = apply(&mut query, &s, CONTROLLER).unwrap();
        assert_eq!(s.settings.controller.buffer_ms, 23);
        assert_eq!(query(&[0xf1]).unwrap(), [0, 0, 0, 0]);
        let file = export(&mut query, &s, CONTROLLER).unwrap();
        assert_eq!(query(&[0xf1]).unwrap(), [0, 0, 0, 0]);
        query(&[0xf0]).unwrap();
        assert_eq!(
            read(&mut query).unwrap().settings.encode().unwrap(),
            original
        );
        s = import(&mut query, &file).unwrap();
        assert_eq!(s.settings.controller.buffer_ms, 23);
        let mut bad = file.clone();
        *bad.last_mut().unwrap() ^= 1;
        assert!(import(&mut query, &bad).is_err());
        assert_eq!(read(&mut query).unwrap().settings.controller.buffer_ms, 23);
        save(&mut query, &s, CONTROLLER).unwrap();
        query(&[0xf0]).unwrap();
        assert_eq!(read(&mut query).unwrap().settings.controller.buffer_ms, 23);
        let directory =
            std::path::PathBuf::from(std::env::var("MAKXD_SETTINGS_ARTIFACTS").unwrap());
        std::fs::write(directory.join("rust.makxd-settings"), &file).unwrap();
        for source in ["python", "web", "cpp", "csharp"] {
            let path = directory.join(format!("{source}.makxd-settings"));
            if !path.exists() {
                continue;
            }
            let s = import(&mut query, &std::fs::read(path).unwrap()).unwrap();
            assert!(s.settings.controller.buffer_ms >= 1);
            eprintln!("RUST_IMPORT={source}");
        }
        drop(query);
        drop(input);
        assert!(child.wait().unwrap().success());
        eprintln!("RUST_SETTINGS=success live=1 export_unsaved=1 save_reboot=1 corrupt_rejected=1");
    }
}
impl DeviceConfiguration {
    pub(crate) fn new(transport: TransportHandle, timeout: Duration) -> Self {
        Self {
            transport,
            timeout: timeout.max(Duration::from_secs(2)),
        }
    }
    fn run<T>(&self, operation: impl FnOnce(&mut Query<'_>) -> Result<T>) -> Result<T> {
        let _guard = self.transport.settings_guard();
        operation(&mut |p| {
            self.transport
                .send_mak_api(ApiOpcode::Connection, p, self.timeout)
        })
    }
    pub fn info(&self) -> Result<SettingsInfo> {
        self.run(info)
    }
    pub fn read(&self) -> Result<SettingsSnapshot> {
        self.run(read)
    }
    /// Apply now; no NOR write, reboot or release of injected movement.
    pub fn apply(&self, value: &SettingsSnapshot, sections: u8) -> Result<SettingsSnapshot> {
        self.run(|q| apply(q, value, sections))
    }
    pub fn save(&self, value: &SettingsSnapshot, sections: u8) -> Result<()> {
        self.run(|q| save(q, value, sections))
    }
    pub fn export_preset(&self, value: &SettingsSnapshot, sections: u8) -> Result<Vec<u8>> {
        self.run(|q| export(q, value, sections))
    }
    pub fn import_preset(&self, file: &[u8]) -> Result<SettingsSnapshot> {
        self.run(|q| import(q, file))
    }
}
#[cfg(feature = "async")]
pub struct AsyncDeviceConfiguration(DeviceConfiguration);
#[cfg(feature = "async")]
impl AsyncDeviceConfiguration {
    pub(crate) fn new(value: DeviceConfiguration) -> Self {
        Self(value)
    }
    async fn run<T: Send + 'static>(
        &self,
        operation: impl FnOnce(DeviceConfiguration) -> Result<T> + Send + 'static,
    ) -> Result<T> {
        let settings = self.0.clone();
        tokio::task::spawn_blocking(move || operation(settings))
            .await
            .map_err(|e| MakxdError::Protocol(e.to_string()))?
    }
    pub async fn info(&self) -> Result<SettingsInfo> {
        self.run(|s| s.info()).await
    }
    pub async fn read(&self) -> Result<SettingsSnapshot> {
        self.run(|s| s.read()).await
    }
    pub async fn apply(&self, value: SettingsSnapshot, sections: u8) -> Result<SettingsSnapshot> {
        self.run(move |s| s.apply(&value, sections)).await
    }
    pub async fn save(&self, value: SettingsSnapshot, sections: u8) -> Result<()> {
        self.run(move |s| s.save(&value, sections)).await
    }
    pub async fn export_preset(&self, value: SettingsSnapshot, sections: u8) -> Result<Vec<u8>> {
        self.run(move |s| s.export_preset(&value, sections)).await
    }
    pub async fn import_preset(&self, file: Vec<u8>) -> Result<SettingsSnapshot> {
        self.run(move |s| s.import_preset(&file)).await
    }
}
