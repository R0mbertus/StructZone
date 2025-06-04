FROM ubuntu:latest

RUN apt-get update
RUN apt-get install -y curl
RUN curl https://sh.rustup.rs -sSf > rustup_install.sh
RUN echo 1 | bash ./rustup_install.sh -y
RUN apt-get install -y clang-14
RUN apt-get install -y llvm-14-dev
RUN apt-get install -y g++ make automake

RUN ln -s /usr/include/llvm14/llvm /usr/include/llvm
RUN ln -s /usr/include/llvm14/llvm-c /usr/include/llvm-c
ENV PATH="/root/.cargo/bin:/usr/lib/llvm-14/bin:${PATH}"

RUN curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to /usr/local/bin

# RUN curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to /usr/local/bin

CMD /bin/sh -c /src/entrypoint.sh
