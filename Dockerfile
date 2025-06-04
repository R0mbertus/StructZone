FROM fedora:latest

RUN dnf update -y
RUN dnf install -y rustup
RUN dnf install -y clang clang++
RUN echo 1 | rustup-init
RUN dnf install -y which bash
RUN dnf install -y llvm14-devel

RUN ln -s /usr/include/llvm14/llvm /usr/include/llvm
RUN ln -s /usr/include/llvm14/llvm-c /usr/include/llvm-c
ENV PATH="/root/.cargo/bin:/usr/lib64/llvm14/bin:${PATH}"

RUN dnf install -y libubsan

RUN curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to /usr/local/bin

CMD /bin/sh -c /src/entrypoint.sh
