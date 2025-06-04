use std::{path::Path, process::Command};

fn main() {
    let pass_script = Path::new("llvm-pass/compile_pass.sh");
    if !pass_script.exists() {
        panic!(
            "cargo:error=LLVM pass compilation script not found at {}",
            pass_script.display()
        );
    }

    let status = Command::new("bash")
        .arg(pass_script)
        .status()
        .expect("Failed to run compile_pass.sh");
    if !status.success() {
        panic!("Failed to compile LLVM pass");
    }

    println!("cargo:rerun-if-changed=llvm-pass/");
    println!("cargo:rerun-if-changed=build.rs");
}
