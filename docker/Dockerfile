FROM ubuntu:latest

RUN apt-get update -y
RUN apt-get install -y curl clang-14 llvm-14-dev g++ make automake gdb wget xz-utils \
    clang-format colordiff git nano perl time libnuma-dev locales gawk build-essential \
    python3-psutil

RUN ln -s /usr/include/llvm14/llvm /usr/include/llvm
RUN ln -s /usr/include/llvm14/llvm-c /usr/include/llvm-c
ENV PATH="/root/.cargo/bin:/usr/lib/llvm-14/bin:${PATH}"

RUN echo "set auto-load safe-path /" > /root/.gdbinit
RUN curl -qsL 'https://install.pwndbg.re' | sh -s -- -t pwndbg-gdb

RUN apt-get install -y python3 python3-pip
RUN python3 -m pip install pre-commit matplotlib --break-system-packages

CMD /bin/sh -c /src/docker/entrypoint.sh
