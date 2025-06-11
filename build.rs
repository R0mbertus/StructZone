use std::{path::Path, process::Command};

fn main() {
    let pass_script = Path::new("llvm-pass/");
    if !pass_script.exists() {
        panic!(
            "cargo:error=LLVM pass compilation script not found at {}",
            pass_script.display()
        );
    }

    let status = Command::new("make")
        .arg(format!("-C {}", pass_script.display()))
        .status()
        .expect("Failed to compile LLVM pass");
    if !status.success() {
        panic!("Failed to compile LLVM pass");
    }

    println!("cargo:rerun-if-changed=llvm-pass/");
    println!("cargo:rerun-if-changed=build.rs");
}
