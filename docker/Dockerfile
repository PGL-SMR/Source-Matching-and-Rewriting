FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Instalar dependências básicas
RUN apt-get update && apt-get install -y \
    cmake \
    git \
    python3 \
    libboost-serialization-dev \
    libopenblas-dev \
    ninja-build \
    software-properties-common \
    wget \
 && rm -rf /var/lib/apt/lists/*

# Instalar LLVM/Clang via script oficial
WORKDIR /tmp
RUN wget https://apt.llvm.org/llvm.sh \
 && chmod +x llvm.sh \
 && ./llvm.sh 20 \
 && rm llvm.sh

# Clonar clangir (llvm-project)
WORKDIR /opt
RUN git clone --filter=blob:none https://github.com/llvm/clangir.git llvm-project

# Build LLVM + Clang + MLIR + Flang + LLD
WORKDIR /opt/llvm-project

RUN mkdir build

RUN cmake -G Ninja -S ./llvm -B ./build \
 -DCMAKE_BUILD_TYPE=Release \
 -DCMAKE_INSTALL_PREFIX="/usr/local/" \
 -DLLVM_ENABLE_ASSERTIONS=ON \
 -DLLVM_ENABLE_PROJECTS="clang;mlir;flang;lld" \
 -DCLANG_ENABLE_CIR=ON \
 -DLLVM_ENABLE_LLD=ON \
 -DLLVM_ENABLE_RTTI=ON \
 -DCMAKE_CXX_COMPILER=clang++-20 \
 -DCMAKE_C_COMPILER=clang-20 \
 -DLLVM_TARGETS_TO_BUILD="host" \
 -DCMAKE_CXX_FLAGS="-Wno-error=deprecated-declarations -Wno-deprecated-declarations" \
 -DLLVM_ENABLE_RUNTIMES="compiler-rt;flang-rt"

RUN ninja -j1 -C ./build install

# Use flang-new as the default flang.
RUN ln -s ${LLVM_INST_DIR}/bin/flang-new ${LLVM_INST_DIR}/bin/flang

WORKDIR /root

# start bash session
CMD bash
