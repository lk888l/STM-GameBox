use std::{
    env, fs,
    path::{Path, PathBuf},
    process::{Command, Output},
    sync::atomic::{AtomicU64, Ordering},
};

use anyhow::{Context, Result, ensure};

pub fn cargo(root: &Path) -> Command {
    let mut command = Command::new(env::var_os("CARGO").unwrap_or_else(|| "cargo".into()));
    command.current_dir(root);
    command
}

pub fn run(command: &mut Command) -> Result<()> {
    eprintln!("Running {command:?}");
    let status = command
        .status()
        .with_context(|| format!("start {command:?}"))?;
    ensure!(status.success(), "command failed ({status}): {command:?}");
    Ok(())
}

pub fn output(command: &mut Command) -> Result<Output> {
    let output = command
        .output()
        .with_context(|| format!("start {command:?}"))?;
    ensure!(
        output.status.success(),
        "command failed: {command:?}\n{}",
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(output)
}

pub struct Llvm {
    pub objcopy: PathBuf,
    pub readobj: PathBuf,
    pub nm: PathBuf,
}

impl Llvm {
    pub fn discover(root: &Path) -> Result<Self> {
        let rustc = env::var_os("RUSTC").unwrap_or_else(|| "rustc".into());
        let version = output(Command::new(&rustc).current_dir(root).arg("-vV"))?;
        let version = String::from_utf8(version.stdout)?;
        let host = version
            .lines()
            .find_map(|line| line.strip_prefix("host: "))
            .context("rustc did not report its host target")?;
        let sysroot = output(
            Command::new(&rustc)
                .current_dir(root)
                .args(["--print", "sysroot"]),
        )?;
        let sysroot = String::from_utf8(sysroot.stdout)?;
        let tools = PathBuf::from(sysroot.trim())
            .join("lib/rustlib")
            .join(host)
            .join("bin");
        let find = |name: &str| -> Result<PathBuf> {
            let path = tools.join(format!("{name}{}", env::consts::EXE_SUFFIX));
            ensure!(
                path.is_file(),
                "{} is missing; run `rustup component add llvm-tools-preview`",
                path.display()
            );
            Ok(path)
        };
        Ok(Self {
            objcopy: find("llvm-objcopy")?,
            readobj: find("llvm-readobj")?,
            nm: find("llvm-nm")?,
        })
    }

    pub fn convert(&self, source: &Path, destination: &Path, format: &str) -> Result<()> {
        output(
            Command::new(&self.objcopy)
                .args(["-O", format])
                .arg(source)
                .arg(destination),
        )?;
        Ok(())
    }
}

/// An exclusively created staging directory. Cleanup removes only its files,
/// then the empty directory; it never recursively removes a computed path.
pub struct Scratch(pub PathBuf);

impl Scratch {
    pub fn new(parent: &Path) -> Result<Self> {
        static NEXT_ID: AtomicU64 = AtomicU64::new(0);
        fs::create_dir_all(parent)?;
        loop {
            let path = parent.join(format!(
                ".xtask-{}-{}",
                std::process::id(),
                NEXT_ID.fetch_add(1, Ordering::Relaxed)
            ));
            match fs::create_dir(&path) {
                Ok(()) => return Ok(Self(path)),
                Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
                Err(error) => return Err(error.into()),
            }
        }
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        if let Ok(entries) = fs::read_dir(&self.0) {
            for entry in entries.flatten() {
                let _ = fs::remove_file(entry.path());
            }
        }
        let _ = fs::remove_dir(&self.0);
    }
}
