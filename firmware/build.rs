use std::{env, path::PathBuf};

fn main() {
    let project_root = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("manifest dir"))
        .parent()
        .expect("firmware has workspace parent")
        .to_path_buf();
    println!("cargo:rustc-link-search={}", project_root.display());
    println!(
        "cargo:rerun-if-changed={}",
        project_root.join("memory.x").display()
    );
}
