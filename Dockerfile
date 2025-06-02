FROM fedora:latest

RUN dnf update -y
RUN dnf install -y rustup
RUN dnf install -y clang clang++
RUN echo 1 | rustup-init
ENV PATH="/root/.cargo/bin:${PATH}"

CMD cargo run --release --manifest-path=lib/LibAFL/Cargo.toml