use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
};

use anyhow::{Context, Result, bail, ensure};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use sha2::{Digest, Sha256};

use crate::{
    Display, Profile, TARGET,
    tooling::{self, Llvm, Scratch},
};

const FLASH_BASE: u64 = 0x0800_0000;
const FLASH_END: u64 = 0x0800_f800;
const RAM_BASE: u64 = 0x2000_0000;
const RAM_END: u64 = 0x2000_5000;

#[derive(Debug, PartialEq, Eq)]
pub struct Layout {
    pub flash_bytes: u64,
    pub static_ram_bytes: u64,
}

#[derive(Serialize, Deserialize)]
struct FileRecord {
    file: String,
    bytes: u64,
    sha256: String,
}

#[derive(Serialize, Deserialize)]
struct Manifest {
    display: String,
    features: Vec<String>,
    target: String,
    profile: String,
    flash_base: String,
    settings_reserved: String,
    flash_bytes: u64,
    static_ram_bytes: u64,
    files: Vec<FileRecord>,
}

pub fn hash(bytes: &[u8]) -> String {
    format!("{:X}", Sha256::digest(bytes))
}

fn package_dir(root: &Path, profile: Profile) -> PathBuf {
    if profile == Profile::Release {
        root.to_owned()
    } else {
        root.join(profile.name())
    }
}

pub fn elf_layout(llvm: &Llvm, elf: &Path) -> Result<Layout> {
    let output = tooling::output(
        Command::new(&llvm.readobj)
            .args([
                "--elf-output-style=JSON",
                "--file-headers",
                "--program-headers",
            ])
            .arg(elf),
    )?;
    let document: Value = serde_json::from_slice(&output.stdout)?;
    validate_layout(document.get(0).context("LLVM returned no ELF metadata")?)
}

fn number(value: &Value, field: &str) -> Result<u64> {
    value
        .get(field)
        .and_then(Value::as_u64)
        .with_context(|| format!("missing ELF field {field}"))
}

fn validate_layout(document: &Value) -> Result<Layout> {
    let header = &document["ElfHeader"];
    ensure!(
        header["Ident"]["Class"]["Value"] == 1
            && header["Machine"]["Value"] == 40
            && header["Ident"]["DataEncoding"]["Value"] == 1,
        "expected an ARM 32-bit little-endian ELF"
    );
    let entry = number(header, "Entry")?;
    ensure!(
        entry & 1 == 1 && (FLASH_BASE..FLASH_END).contains(&(entry & !1)),
        "invalid Cortex-M reset entry"
    );
    let segments = document["ProgramHeaders"]
        .as_array()
        .context("ELF lacks program headers")?;
    let mut start = u64::MAX;
    let mut end = FLASH_BASE;
    let mut ram_end = RAM_BASE;
    for segment in segments {
        let segment = &segment["ProgramHeader"];
        if segment["Type"]["Value"] != 1 {
            continue;
        }
        let file_size = number(segment, "FileSize")?;
        let mem_size = number(segment, "MemSize")?;
        ensure!(
            file_size <= mem_size,
            "ELF segment file size exceeds memory size"
        );
        let physical = number(segment, "PhysicalAddress")?;
        if file_size != 0 {
            let file_end = physical
                .checked_add(file_size)
                .context("ELF load range overflow")?;
            ensure!(
                physical >= FLASH_BASE && file_end <= FLASH_END,
                "ELF load segment enters settings Flash or lies outside program Flash"
            );
            start = start.min(physical);
            end = end.max(file_end);
        }
        let virtual_address = number(segment, "VirtualAddress")?;
        let virtual_end = virtual_address
            .checked_add(mem_size)
            .context("ELF memory range overflow")?;
        if mem_size != 0 {
            if virtual_address >= RAM_BASE && virtual_end <= RAM_END {
                ram_end = ram_end.max(virtual_end);
            } else {
                ensure!(
                    virtual_address >= FLASH_BASE && virtual_end <= FLASH_END,
                    "ELF memory segment is outside program Flash/SRAM"
                );
            }
        }
    }
    ensure!(
        start == FLASH_BASE && end > start && (entry & !1) < end,
        "ELF must contain vectors at 0x08000000 and a valid reset entry"
    );
    Ok(Layout {
        flash_bytes: end - start,
        static_ram_bytes: ram_end - RAM_BASE,
    })
}

/// Decode Intel HEX independently of objcopy, checking every checksum/address.
/// Holes have the same zero fill as objcopy's binary output.
fn decode_hex(text: &str) -> Result<Vec<u8>> {
    let mut image = vec![0; (FLASH_END - FLASH_BASE) as usize];
    let mut base = 0_u64;
    let mut first = u64::MAX;
    let mut end = 0;
    let mut eof = false;
    for line in text.lines().filter(|line| !line.is_empty()) {
        ensure!(!eof, "HEX data follows EOF");
        let hex = line.strip_prefix(':').context("invalid HEX record")?;
        ensure!(
            hex.is_ascii() && hex.len() >= 10 && hex.len().is_multiple_of(2),
            "invalid HEX record length"
        );
        let record = (0..hex.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&hex[i..i + 2], 16))
            .collect::<std::result::Result<Vec<_>, _>>()?;
        ensure!(
            record.len() == usize::from(record[0]) + 5
                && record
                    .iter()
                    .fold(0_u8, |sum, &byte| sum.wrapping_add(byte))
                    == 0,
            "invalid HEX length/checksum"
        );
        let offset = u64::from(u16::from_be_bytes([record[1], record[2]]));
        let data = &record[4..record.len() - 1];
        match record[3] {
            0 => {
                let address = base + offset;
                let limit = address + data.len() as u64;
                ensure!(
                    address >= FLASH_BASE && limit <= FLASH_END,
                    "HEX address outside program Flash"
                );
                if !data.is_empty() {
                    first = first.min(address);
                    end = end.max(limit);
                    image[(address - FLASH_BASE) as usize..(limit - FLASH_BASE) as usize]
                        .copy_from_slice(data);
                }
            }
            1 => {
                ensure!(data.is_empty() && offset == 0, "invalid HEX EOF");
                eof = true;
            }
            2 | 4 => {
                ensure!(data.len() == 2 && offset == 0, "invalid HEX address record");
                base = u64::from(u16::from_be_bytes([data[0], data[1]]))
                    << if record[3] == 4 { 16 } else { 4 };
            }
            5 => {
                ensure!(data.len() == 4 && offset == 0, "invalid HEX entry record");
                let entry = u64::from(u32::from_be_bytes(data.try_into()?));
                ensure!(
                    entry & 1 == 1 && (FLASH_BASE..FLASH_END).contains(&(entry & !1)),
                    "invalid HEX entry address"
                );
            }
            kind => bail!("unsupported HEX record type {kind}"),
        }
    }
    ensure!(
        eof && first == FLASH_BASE && end > first,
        "HEX lacks vectors or EOF"
    );
    image.truncate((end - FLASH_BASE) as usize);
    Ok(image)
}

pub fn export(
    llvm: &Llvm,
    elf: &Path,
    output: &Path,
    display: Display,
    profile: Profile,
) -> Result<()> {
    let layout = elf_layout(llvm, elf)?;
    let destination = package_dir(output, profile);
    let stage = Scratch::new(&destination)?;
    let stem = format!("gamebox-f103-{}", display.name());
    let elf_name = format!("{stem}.elf");
    let bin_name = format!("{stem}.bin");
    let hex_name = format!("{stem}.hex");
    let manifest_name = format!("{stem}.json");
    fs::copy(elf, stage.0.join(&elf_name))?;
    llvm.convert(&stage.0.join(&elf_name), &stage.0.join(&bin_name), "binary")?;
    llvm.convert(&stage.0.join(&elf_name), &stage.0.join(&hex_name), "ihex")?;
    let bin = fs::read(stage.0.join(&bin_name))?;
    ensure!(
        bin.len() as u64 == layout.flash_bytes,
        "BIN length does not match ELF load addresses"
    );
    ensure!(
        decode_hex(&fs::read_to_string(stage.0.join(&hex_name))?)? == bin,
        "HEX/BIN mismatch"
    );
    let mut files = Vec::new();
    for file in [&elf_name, &bin_name, &hex_name] {
        let bytes = fs::read(stage.0.join(file))?;
        files.push(FileRecord {
            file: file.clone(),
            bytes: bytes.len() as u64,
            sha256: hash(&bytes),
        });
    }
    let manifest = Manifest {
        display: display.name().into(),
        features: vec![format!("oled-{}", display.name())],
        target: TARGET.into(),
        profile: profile.name().into(),
        flash_base: "0x08000000".into(),
        settings_reserved: "0x0800F800..0x08010000".into(),
        flash_bytes: layout.flash_bytes,
        static_ram_bytes: layout.static_ram_bytes,
        files,
    };
    fs::write(
        stage.0.join(&manifest_name),
        serde_json::to_string_pretty(&manifest)? + "\n",
    )?;
    // No existing package is touched until all conversion and validation passes.
    // Publish the manifest last; interrupted publication is detectable by hashes.
    for name in [&elf_name, &bin_name, &hex_name, &manifest_name] {
        fs::rename(stage.0.join(name), destination.join(name))?;
    }
    println!(
        "Packaged {} {}: Flash {}/63488 B, static SRAM {}/20480 B -> {}",
        display.name(),
        profile.name(),
        layout.flash_bytes,
        layout.static_ram_bytes,
        destination.display()
    );
    verify(llvm, output, display, profile)
}

pub fn verify(llvm: &Llvm, output: &Path, display: Display, profile: Profile) -> Result<()> {
    let directory = package_dir(output, profile);
    let stem = format!("gamebox-f103-{}", display.name());
    let manifest: Manifest =
        serde_json::from_slice(&fs::read(directory.join(format!("{stem}.json")))?)?;
    ensure!(
        manifest.display == display.name()
            && manifest.profile == profile.name()
            && manifest.target == TARGET
            && manifest.features == [format!("oled-{}", display.name())]
            && manifest.flash_base == "0x08000000"
            && manifest.settings_reserved == "0x0800F800..0x08010000",
        "package metadata does not match the selected firmware"
    );
    let names = [
        format!("{stem}.elf"),
        format!("{stem}.bin"),
        format!("{stem}.hex"),
    ];
    ensure!(
        manifest.files.len() == names.len(),
        "unexpected package file count"
    );
    for (file, expected) in manifest.files.iter().zip(&names) {
        ensure!(&file.file == expected, "unexpected package filename");
        let bytes = fs::read(directory.join(expected))?;
        ensure!(
            file.bytes == bytes.len() as u64 && file.sha256 == hash(&bytes),
            "{} size/hash mismatch",
            file.file
        );
    }
    let layout = elf_layout(llvm, &directory.join(&names[0]))?;
    let bin = fs::read(directory.join(&names[1]))?;
    ensure!(
        bin.len() as u64 == layout.flash_bytes
            && manifest.flash_bytes == layout.flash_bytes
            && manifest.static_ram_bytes == layout.static_ram_bytes,
        "package memory layout mismatch"
    );
    ensure!(
        decode_hex(&fs::read_to_string(directory.join(&names[2]))?)? == bin,
        "HEX/BIN mismatch"
    );
    let scratch = Scratch::new(&directory)?;
    llvm.convert(
        &directory.join(&names[0]),
        &scratch.0.join("check.bin"),
        "binary",
    )?;
    ensure!(
        fs::read(scratch.0.join("check.bin"))? == bin,
        "ELF/BIN mismatch"
    );
    println!("Verified {} {} package", display.name(), profile.name());
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn metadata() -> Value {
        json!({"ElfHeader":{"Ident":{"Class":{"Value":1},"DataEncoding":{"Value":1}},"Machine":{"Value":40},"Entry":FLASH_BASE+9},
            "ProgramHeaders":[{"ProgramHeader":{"Type":{"Value":1},"PhysicalAddress":FLASH_BASE,"VirtualAddress":FLASH_BASE,"FileSize":16,"MemSize":16}},
            {"ProgramHeader":{"Type":{"Value":1},"PhysicalAddress":RAM_BASE,"VirtualAddress":RAM_BASE,"FileSize":0,"MemSize":64}}]})
    }

    #[test]
    fn validates_flash_load_addresses_and_zero_initialized_ram() {
        assert_eq!(
            validate_layout(&metadata()).unwrap(),
            Layout {
                flash_bytes: 16,
                static_ram_bytes: 64
            }
        );
        let mut bad = metadata();
        bad["ProgramHeaders"][0]["ProgramHeader"]["PhysicalAddress"] = json!(FLASH_END);
        assert!(validate_layout(&bad).is_err());
        bad = metadata();
        bad["ProgramHeaders"][1]["ProgramHeader"]["MemSize"] = json!(RAM_END - RAM_BASE + 1);
        assert!(validate_layout(&bad).is_err());
        bad = metadata();
        bad["ElfHeader"]["Entry"] = json!(FLASH_BASE);
        assert!(validate_layout(&bad).is_err());
    }

    #[test]
    fn hex_checks_addresses_checksums_eof_and_zero_filled_gaps() {
        let valid = ":020000040800F2\n:020000000102FB\n:01000400AA51\n:00000001FF\n";
        assert_eq!(decode_hex(valid).unwrap(), [1, 2, 0, 0, 0xaa]);
        assert!(decode_hex(&valid.replace("FB", "FA")).is_err());
        assert!(decode_hex(&valid.replace("0800F2", "0801F1")).is_err());
        assert!(decode_hex(&valid.replace(":00000001FF\n", "")).is_err());
        assert!(decode_hex(&(valid.to_owned() + ":01000400AA51\n")).is_err());
    }

    #[test]
    fn sha256_matches_known_vector() {
        assert_eq!(
            hash(b"abc"),
            "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"
        );
    }

    #[test]
    #[ignore = "requires the firmware ELF; cargo xtask ci builds it before running this test"]
    fn packaging_recovers_cached_outputs_and_preserves_files_on_conversion_failure() -> Result<()> {
        let root = Path::new(env!("CARGO_MANIFEST_DIR"))
            .parent()
            .unwrap()
            .parent()
            .unwrap();
        let llvm = Llvm::discover(root)?;
        let stage = Scratch::new(&root.join("target/xtask artifact tests"))?;
        let options = crate::BuildArgs {
            display: Display::Spi,
            release: true,
            target_dir: None,
            output_dir: stage.0.clone(),
        };
        crate::workflow::build(root, &options)?;
        let names =
            ["elf", "bin", "hex", "json"].map(|extension| format!("gamebox-f103-spi.{extension}"));
        let original: Vec<Vec<u8>> = names
            .iter()
            .map(|name| fs::read(stage.0.join(name)))
            .collect::<std::io::Result<_>>()?;

        // The ELF remains in Cargo's cache. Removing only the exported BIN must
        // still produce the complete, identical package on the next invocation.
        fs::remove_file(stage.0.join(&names[1]))?;
        crate::workflow::build(root, &options)?;
        for (name, bytes) in names.iter().zip(&original) {
            assert_eq!(&fs::read(stage.0.join(name))?, bytes);
        }

        let failing_llvm = Llvm {
            objcopy: stage.0.join("missing-objcopy"),
            readobj: llvm.readobj.clone(),
            nm: llvm.nm.clone(),
        };
        assert!(
            export(
                &failing_llvm,
                &stage.0.join(&names[0]),
                &stage.0,
                Display::Spi,
                Profile::Release
            )
            .is_err()
        );
        for (name, bytes) in names.iter().zip(&original) {
            assert_eq!(&fs::read(stage.0.join(name))?, bytes);
        }
        assert_eq!(
            fs::read_dir(&stage.0)?.count(),
            names.len(),
            "failed conversion left staging files behind"
        );

        let mut damaged = original[1].clone();
        damaged[0] ^= 1;
        fs::write(stage.0.join(&names[1]), damaged)?;
        assert!(verify(&llvm, &stage.0, Display::Spi, Profile::Release).is_err());
        fs::write(stage.0.join(&names[1]), &original[1])?;
        verify(&llvm, &stage.0, Display::Spi, Profile::Release)
    }
}
