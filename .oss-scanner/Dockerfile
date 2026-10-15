# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

# OSS Scanner build of OCUDU from the build context.
#
# What is built, in /src/build:
#   - every library, application (gnb, odu, ocu, ocucp, ocuup, ru_emulator, ...) and unit test, with
#     AddressSanitizer + UBSan, -O1 -g, frame pointers and asserts enabled (ASSERT_LEVEL=NORMAL);
#   - the libFuzzer harnesses under tests/fuzz (OFH, NGAP, RRC), with their seed corpora.
#
# Hardware backends (UHD, DPDK, Sidekiq, CUDA, MKL, ARMPL, FFTZ) are disabled: they need vendor SDKs or
# devices that are not available offline. ZeroMQ stays enabled so the gnb can run against a virtual radio.
# ROHC is built from the release tarball so the ROHC adapter in PDCP is compiled in.
FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive \
    CC=clang \
    CXX=clang++ \
    ROHC_DIR=/opt/rohc \
    ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:symbolize=1 \
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

# OCUDU and ROHC build dependencies, from the scripts shared with the other OCUDU images.
COPY docker/scripts/ /src/docker/scripts/
RUN /src/docker/scripts/install_dependencies.sh build \
    && /src/docker/scripts/install_rohc_dependencies.sh build

# Scanner tooling (clang sanitizers, lld, ninja, gdb) and ZeroMQ for the virtual radio.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git python3 gdb less \
      clang llvm lld ninja-build \
      libzmq3-dev \
    && rm -rf /var/lib/apt/lists/*

# scanner contract: the checkout lives inside the image, at /src
COPY . /src
WORKDIR /src

# ROHC is fetched now, while the network is still available.
RUN /src/docker/scripts/build_rohc.sh 2.3.1 x86-64-v3

# MARCH is pinned instead of the default "native": the image is built and scanned on different hosts.
RUN cmake -S /src -B /src/build -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1 -g -fno-omit-frame-pointer" \
      -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1 -g -fno-omit-frame-pointer" \
      -DMARCH=x86-64-v3 \
      -DLINKER=lld \
      -DENABLE_SPLIT_DWARF=ON \
      -DENABLE_ASAN=ON \
      -DENABLE_UBSAN=ON \
      -DENABLE_WERROR=OFF \
      -DENABLE_BACKWARD=OFF \
      -DBUILD_TESTING=ON \
      -DENABLE_FUZZTESTS=ON \
      -DENABLE_UHD=OFF \
      -DENABLE_DPDK=OFF \
      -DENABLE_SIDEKIQ=OFF \
      -DENABLE_CUDA=OFF \
      -DENABLE_MKL=OFF \
      -DENABLE_FFTZ=OFF \
      -DENABLE_ARMPL=OFF \
      -DENABLE_LIBNUMA=OFF \
    && cmake --build /src/build -j"$(nproc)" \
    && cmake --build /src/build -j"$(nproc)" --target fuzz_targets

# Seed corpora for the fuzz harnesses (tests/fuzz/<layer>/corpus/).
RUN python3 tests/fuzz/ofh/gen_corpus.py \
    && python3 tests/fuzz/ngap/gen_corpus.py \
    && python3 tests/fuzz/rrc/gen_corpus.py

# Cheap smoke tests, so we know the image works. A failing test must not abort the build. Run the full suite inside the
# image with: cd /src/build && ctest -j"$(nproc)"
RUN cd /src/build \
    && (ctest --output-on-failure -j"$(nproc)" --timeout 300 -R "asn1|ngap|rrc|pdcp|rlc|ofh" \
        || echo "WARNING: ctest reported failures")
