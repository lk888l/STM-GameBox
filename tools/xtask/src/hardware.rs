use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
    process::Command,
};

use anyhow::{Context, Result, ensure};
use clap::Args;
use serde::Serialize;

use crate::{
    artifacts,
    tooling::{self, Llvm},
};

const SYMBOLS: &[&str] = &[
    "HELD_KEYS",
    "BUTTON_SAMPLES",
    "KEY_EVENTS_PROCESSED",
    "MAX_KEY_PRESS_AGE_MS",
    "MAX_BUTTON_SCAN_GAP_US",
    "MAX_RENDER_TIME_US",
    "RENDERED_FRAMES",
    "DROPPED_KEY_EVENTS",
    "DROPPED_UART_EVENTS",
    "OLED_ERRORS",
    "OLED_TRANSFERS",
];
const KEYS: &[&str] = &[
    "UP", "DOWN", "LEFT", "RIGHT", "JUMP", "FUNC", "ENTER", "BACK",
];

#[derive(Args)]
pub struct ButtonArgs {
    /// ST-Link serial number. Required to prevent automatic probe selection.
    #[arg(long, value_parser = serial)]
    probe: String,
    /// Must match the flashed firmware; this command never flashes an image.
    #[arg(long, default_value = "artifacts/firmware/gamebox-f103-spi.elf")]
    elf: PathBuf,
    #[arg(long, default_value_t = 30, value_parser = clap::value_parser!(u32).range(1..=100))]
    press_count: u32,
    #[arg(long, default_value_t = 60, value_parser = clap::value_parser!(u32).range(40..=200))]
    hold_ms: u32,
    #[arg(long, default_value = "target/button-regression")]
    output_dir: PathBuf,
    /// Validate the ELF and generate Tcl without opening the probe.
    #[arg(long)]
    dry_run: bool,
}

fn serial(text: &str) -> std::result::Result<String, String> {
    if !text.is_empty() && text.bytes().all(|byte| byte.is_ascii_alphanumeric()) {
        Ok(text.to_owned())
    } else {
        Err("use the ST-Link's alphanumeric serial number".into())
    }
}

fn tcl_path(path: &Path) -> Result<String> {
    let text = path
        .to_str()
        .context("OpenOCD paths must be UTF-8")?
        .replace('\\', "/");
    let text = text.strip_prefix("//?/").unwrap_or(&text);
    ensure!(
        !text.contains(['{', '}', '\r', '\n']),
        "unsupported characters in OpenOCD path"
    );
    Ok(format!("{{{text}}}"))
}

fn symbols(output: &str) -> Result<BTreeMap<String, u32>> {
    let mut symbols = BTreeMap::new();
    for line in output.lines() {
        let mut fields = line.split_whitespace();
        let Some(address) = fields.next() else {
            continue;
        };
        let _kind = fields.next();
        let Some(name) = fields.next() else {
            continue;
        };
        let Some(name) = name.strip_prefix("gamebox_f103_firmware::services::") else {
            continue;
        };
        // LLVM's demangler retains Rust's ::h<hash> and optional (.0) suffix.
        let name = name.split("::").next().unwrap_or(name);
        if SYMBOLS.contains(&name) {
            let address = u32::from_str_radix(address, 16)?;
            ensure!(
                (0x2000_0000..0x2000_4ffd).contains(&address),
                "diagnostic symbol lies outside SRAM"
            );
            ensure!(
                symbols.insert(name.to_owned(), address).is_none(),
                "ambiguous diagnostic symbol {name}"
            );
        }
    }
    for &name in SYMBOLS {
        ensure!(
            symbols.contains_key(name),
            "ELF lacks diagnostic symbol {name}"
        );
    }
    Ok(symbols)
}

#[derive(Serialize)]
struct KeyResult {
    key: String,
    #[serde(flatten)]
    metrics: BTreeMap<String, u64>,
    passed: bool,
}

fn key_results(log: &str, count: u32) -> Result<Vec<KeyResult>> {
    let mut results = Vec::new();
    let mut seen = BTreeSet::new();
    for line in log.lines().filter_map(|line| line.strip_prefix("KEY ")) {
        let mut fields = line.split_whitespace();
        let key = fields.next().context("key result has no name")?;
        ensure!(
            KEYS.contains(&key) && seen.insert(key),
            "unknown/duplicate key result {key}"
        );
        let mut metrics = BTreeMap::new();
        for field in fields {
            let (name, number) = field.split_once('=').context("invalid key metric")?;
            let value = if let Some(hex) = number.strip_prefix("0x") {
                u64::from_str_radix(hex, 16)?
            } else {
                number.parse()?
            };
            ensure!(
                metrics.insert(name.to_owned(), value).is_none(),
                "duplicate key metric {name}"
            );
        }
        let get = |name: &str| -> Result<u64> {
            metrics
                .get(name)
                .copied()
                .with_context(|| format!("missing {name} metric"))
        };
        let passed = get("presses")? == u64::from(count)
            && get("releases")? == u64::from(count)
            && get("events")? >= 2 * u64::from(count)
            && get("scan_gap_us")? <= 20_000
            && get("press_age_ms")? <= 20
            && get("input_drops")? == 0
            && get("uart_drops")? == 0
            && get("oled_errors")? == 0;
        results.push(KeyResult {
            key: key.into(),
            metrics,
            passed,
        });
    }
    Ok(results)
}

pub fn run(root: &Path, options: &ButtonArgs) -> Result<()> {
    let llvm = Llvm::discover(root)?;
    let elf = root.join(&options.elf);
    artifacts::elf_layout(&llvm, &elf)?;
    let nm = tooling::output(Command::new(&llvm.nm).args(["-n", "--demangle"]).arg(&elf))?;
    let addresses = symbols(&String::from_utf8(nm.stdout)?)?;
    let definitions = addresses
        .iter()
        .map(|(name, address)| format!("set {name} 0x{address:08x}\n"))
        .collect::<String>();
    let template = include_str!("../assets/buttons.tcl");
    let script = definitions
        + &template
            .replace("{@ELF@}", &tcl_path(&elf)?)
            .replace("@COUNT@", &options.press_count.to_string())
            .replace("@HOLD@", &options.hold_ms.to_string());
    let directory = root.join(&options.output_dir);
    fs::create_dir_all(&directory)?;
    let script_path = directory.join("buttons.tcl");
    fs::write(&script_path, script)?;
    if options.dry_run {
        println!("Generated {}; no probe was opened", script_path.display());
        return Ok(());
    }
    println!(
        "Testing ST-Link {}; keep physical buttons released during the test",
        options.probe
    );
    let output = Command::new("openocd")
        .current_dir(root)
        .args(["-f", "interface/stlink.cfg", "-c"])
        .arg(format!("adapter serial {}", options.probe))
        .args([
            "-c",
            "transport select swd",
            "-f",
            "target/stm32f1x.cfg",
            "-c",
            "adapter speed 100",
            "-c",
            "gdb port disabled",
            "-c",
            "tcl port disabled",
            "-c",
            "telnet port disabled",
            "-f",
        ])
        .arg(&script_path)
        .output()
        .context("start OpenOCD; install it only if you need hardware tests")?;
    let log = format!(
        "{}{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    fs::write(directory.join("openocd.log"), &log)?;
    let keys = key_results(&log, options.press_count)?;
    for key in &keys {
        println!("{}: {}", key.key, if key.passed { "PASS" } else { "FAIL" });
    }
    let passed = output.status.success()
        && log.lines().any(|line| line == "REGRESSION_COMPLETE")
        && keys.len() == KEYS.len()
        && keys.iter().all(|key| key.passed);
    let report = serde_json::json!({"probe_serial":options.probe,"elf_sha256":artifacts::hash(&fs::read(&elf)?),
        "press_count_per_key":options.press_count,"hold_milliseconds":options.hold_ms,"keys":keys});
    fs::write(
        directory.join("results.json"),
        serde_json::to_string_pretty(&report)? + "\n",
    )?;
    ensure!(
        passed,
        "button regression failed; inspect {}/openocd.log and results.json",
        directory.display()
    );
    println!("All eight keys passed; the regular application is running");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tcl_paths_preserve_spaces_and_reject_code_delimiters() {
        assert_eq!(
            tcl_path(Path::new("a directory/firmware.elf")).unwrap(),
            "{a directory/firmware.elf}"
        );
        assert!(tcl_path(Path::new("bad}path.elf")).is_err());
        assert!(serial("01380173524300183638414B").is_ok());
        assert!(serial("probe; shutdown").is_err());
    }

    #[test]
    fn diagnostic_addresses_accept_llvm_demangling_and_require_every_symbol() {
        let output = SYMBOLS
            .iter()
            .enumerate()
            .map(|(i, name)| {
                format!(
                    "{:08x} b gamebox_f103_firmware::services::{name}::h12345678 (.0)\n",
                    0x2000_0010 + i * 4
                )
            })
            .collect::<String>();
        assert_eq!(symbols(&output).unwrap().len(), SYMBOLS.len());
        assert!(symbols("").is_err());
    }

    #[test]
    fn hardware_results_decode_hex_values_and_detect_missing_edges() {
        let pass = "KEY UP presses=30 releases=30 events=75 scan_gap_us=0x1bf7 press_age_ms=0x1 input_drops=0x0 uart_drops=0x0 oled_errors=0x0";
        assert!(key_results(pass, 30).unwrap()[0].passed);
        assert!(!key_results(&pass.replace("releases=30", "releases=29"), 30).unwrap()[0].passed);
        assert!(key_results(&(pass.to_owned() + "\n" + pass), 30).is_err());
    }
}
