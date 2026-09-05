use std::{
    env,
    io::{BufRead, BufReader},
    path::{Path, PathBuf},
    process::Stdio,
};

use anyhow::{Context, Result, ensure};
use serde_json::Value;

use crate::{
    BuildArgs, Display, FIRMWARE_BIN, FIRMWARE_PACKAGE, Profile, TARGET, artifacts,
    tooling::{self, Llvm},
};

pub fn build(root: &Path, options: &BuildArgs) -> Result<()> {
    let llvm = Llvm::discover(root)?;
    let profile = Profile::from_release(options.release);
    for &display in options.display.variants() {
        let mut command = tooling::cargo(root);
        command
            .args([
                "build",
                "--locked",
                "--message-format=json-render-diagnostics",
                "-p",
                FIRMWARE_PACKAGE,
                "--bin",
                FIRMWARE_BIN,
                "--target",
                TARGET,
                "--no-default-features",
                "--features",
            ])
            .arg(format!("oled-{}", display.name()));
        if options.release {
            command.arg("--release");
        }
        let target_dir = options
            .target_dir
            .clone()
            .or_else(|| env::var_os("CARGO_TARGET_DIR").map(PathBuf::from))
            .unwrap_or_else(|| PathBuf::from(format!("target/oled-{}", display.name())));
        command.arg("--target-dir").arg(root.join(target_dir));
        eprintln!("Building {} {}", display.name(), profile.name());
        let mut child = command
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()?;
        let mut elf = None;
        for line in
            BufReader::new(child.stdout.take().context("Cargo stdout is unavailable")?).lines()
        {
            let line = line?;
            if let Ok(message) = serde_json::from_str::<Value>(&line) {
                if let Some(path) = executable(&message, display) {
                    elf = Some(path);
                }
            } else {
                eprintln!("{line}");
            }
        }
        ensure!(
            child.wait()?.success(),
            "firmware build failed; no new {} package was exported",
            display.name()
        );
        let elf = elf.context("Cargo succeeded without reporting the requested firmware ELF")?;
        // Cargo emits this artifact message for both fresh and cached builds.
        // Never infer an ELF path or package an earlier image after a failure.
        artifacts::export(
            &llvm,
            &elf,
            &root.join(&options.output_dir),
            display,
            profile,
        )?;
    }
    Ok(())
}

fn executable(message: &Value, display: Display) -> Option<PathBuf> {
    if message["reason"] != "compiler-artifact"
        || message["target"]["name"] != FIRMWARE_BIN
        || !message["target"]["kind"]
            .as_array()?
            .iter()
            .any(|kind| kind == "bin")
    {
        return None;
    }
    let features = message["features"].as_array()?;
    if !features
        .iter()
        .any(|feature| feature == &format!("oled-{}", display.name()))
        || features.iter().any(|feature| {
            feature
                == if display == Display::Spi {
                    "oled-i2c"
                } else {
                    "oled-spi"
                }
        })
    {
        return None;
    }
    message["executable"].as_str().map(PathBuf::from)
}

fn host_command(root: &Path, action: &str) -> std::process::Command {
    let mut command = tooling::cargo(root);
    command.args([
        action,
        "--locked",
        "--target",
        "host-tuple",
        "-p",
        "gamebox-core",
        "-p",
        "oled-driver",
        "-p",
        "font-subset",
        "-p",
        "xtask",
        "--all-features",
    ]);
    command
}

pub fn test(root: &Path) -> Result<()> {
    tooling::run(&mut host_command(root, "test"))
}

pub fn artifact_tests(root: &Path) -> Result<()> {
    tooling::run(tooling::cargo(root).args([
        "test",
        "--locked",
        "--target",
        "host-tuple",
        "-p",
        "xtask",
        "--",
        "--ignored",
    ]))
}

pub fn check(root: &Path) -> Result<()> {
    tooling::run(tooling::cargo(root).args(["fmt", "--all", "--", "--check"]))?;
    tooling::run(host_command(root, "clippy").args(["--all-targets", "--", "-D", "warnings"]))?;
    for display in Display::All.variants() {
        tooling::run(
            tooling::cargo(root)
                .args([
                    "clippy",
                    "--locked",
                    "--release",
                    "-p",
                    FIRMWARE_PACKAGE,
                    "--target",
                    TARGET,
                    "--no-default-features",
                    "--features",
                ])
                .arg(format!("oled-{}", display.name()))
                .args(["--", "-D", "warnings"]),
        )?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn uses_cargo_artifact_messages_including_cache_hits_and_paths_with_spaces() {
        let mut message = json!({"reason":"compiler-artifact","target":{"name":"gamebox-f103","kind":["bin"]},
            "features":["oled-spi"],"fresh":true,"executable":"a custom target/release/gamebox-f103"});
        assert_eq!(
            executable(&message, Display::Spi),
            Some(PathBuf::from("a custom target/release/gamebox-f103"))
        );
        assert_eq!(executable(&message, Display::I2c), None);
        message["target"]["kind"] = json!(["lib"]);
        assert_eq!(executable(&message, Display::Spi), None);
    }
}
