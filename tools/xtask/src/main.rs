#![forbid(unsafe_code)]

mod artifacts;
mod hardware;
mod tooling;
mod workflow;

use std::{path::PathBuf, process::ExitCode};

use anyhow::Result;
use clap::{Args, Parser, Subcommand, ValueEnum};

pub const TARGET: &str = "thumbv7m-none-eabi";
pub const FIRMWARE_PACKAGE: &str = "gamebox-f103-firmware";
pub const FIRMWARE_BIN: &str = "gamebox-f103";

#[derive(Parser)]
#[command(about = "Cross-platform GameBox builds, artifacts, checks, and optional hardware tests")]
struct Cli {
    #[command(subcommand)]
    command: Task,
}

#[derive(Subcommand)]
enum Task {
    /// Build firmware and export verified ELF/BIN/HEX/SHA-256 packages.
    Build(BuildArgs),
    /// Check existing packages without building or connecting to hardware.
    Verify(VerifyArgs),
    /// Run the core, OLED, font tool, and xtask tests on this host.
    Test,
    /// Check formatting and lint host code plus both firmware variants.
    Check,
    /// Run checks, tests, all four firmware builds, and artifact regression tests.
    Ci,
    /// Test GPIO-to-UI responsiveness using one explicitly selected ST-Link.
    Buttons(hardware::ButtonArgs),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, ValueEnum)]
pub enum Display {
    Spi,
    I2c,
    All,
}

impl Display {
    pub fn variants(self) -> &'static [Self] {
        match self {
            Self::Spi => &[Self::Spi],
            Self::I2c => &[Self::I2c],
            Self::All => &[Self::Spi, Self::I2c],
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::Spi => "spi",
            Self::I2c => "i2c",
            Self::All => "all",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Profile {
    Debug,
    Release,
}

impl Profile {
    pub fn from_release(release: bool) -> Self {
        if release { Self::Release } else { Self::Debug }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::Debug => "debug",
            Self::Release => "release",
        }
    }
}

#[derive(Args)]
pub struct BuildArgs {
    #[arg(long, value_enum, default_value = "spi")]
    display: Display,
    /// Use the release profile (default: debug).
    #[arg(long)]
    release: bool,
    /// Override Cargo's build cache directory, relative to the workspace.
    #[arg(long)]
    target_dir: Option<PathBuf>,
    /// Package root; debug artifacts go into its debug/ subdirectory.
    #[arg(long, default_value = "artifacts/firmware")]
    output_dir: PathBuf,
}

#[derive(Args)]
pub struct VerifyArgs {
    #[arg(long, value_enum, default_value = "spi")]
    display: Display,
    #[arg(long, conflicts_with = "all_profiles")]
    release: bool,
    #[arg(long)]
    all_profiles: bool,
    #[arg(long, default_value = "artifacts/firmware")]
    output_dir: PathBuf,
}

fn main() -> ExitCode {
    if let Err(error) = execute(Cli::parse()) {
        eprintln!("error: {error:#}");
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}

fn execute(cli: Cli) -> Result<()> {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|path| path.parent())
        .expect("xtask is under tools/xtask")
        .to_path_buf();
    match cli.command {
        Task::Build(options) => workflow::build(&root, &options),
        Task::Verify(options) => {
            let llvm = tooling::Llvm::discover(&root)?;
            let profiles = if options.all_profiles {
                vec![Profile::Debug, Profile::Release]
            } else {
                vec![Profile::from_release(options.release)]
            };
            for profile in profiles {
                for &display in options.display.variants() {
                    artifacts::verify(&llvm, &root.join(&options.output_dir), display, profile)?;
                }
            }
            Ok(())
        }
        Task::Test => workflow::test(&root),
        Task::Check => workflow::check(&root),
        Task::Ci => {
            workflow::check(&root)?;
            workflow::test(&root)?;
            for release in [false, true] {
                workflow::build(
                    &root,
                    &BuildArgs {
                        display: Display::All,
                        release,
                        target_dir: None,
                        output_dir: "artifacts/firmware".into(),
                    },
                )?;
            }
            workflow::artifact_tests(&root)
        }
        Task::Buttons(options) => hardware::run(&root, &options),
    }
}
