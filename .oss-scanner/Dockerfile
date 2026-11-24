# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

# OSS Scanner build of OCUDU from the build context.
#
# What is built, in /src/build, with AddressSanitizer, -O0 -g, frame pointers and asserts enabled
# (ASSERT_LEVEL=NORMAL):
#   - the libFuzzer harnesses under tests/fuzz (OFH, NGAP, RRC), with their seed corpora;
#   - the unit tests of the layers a UE reaches over the air, of the user plane, and of the CU-CP's peer interfaces
#     (see PROTOCOL_TESTS below), with every library they link.
#
# The build must finish within the scanner's time limit, which a full build does not. To fit, UBSan is left out,
# the code is not optimised, and the applications and the remaining unit tests are not built. Everything is
# configured, so build any of them later with e.g. `ninja -C /src/build gnb` or
# `ninja -C /src/build tests/unittests/e1ap/all`.
#
# Hardware backends (UHD, DPDK, Sidekiq, CUDA, MKL, ARMPL, FFTZ) are disabled: they need vendor SDKs or
# devices that are not available offline. ZeroMQ stays enabled so the gnb can run against a virtual radio.
# ROHC is built from the release tarball so the ROHC adapter in PDCP is compiled in.
FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive \
    CC=clang \
    CXX=clang++ \
    ROHC_DIR=/opt/rohc \
    ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:symbolize=1

# OCUDU and ROHC build dependencies, from the scripts shared with the other OCUDU images.
COPY docker/scripts/ /src/docker/scripts/
RUN /src/docker/scripts/install_dependencies.sh build \
    && /src/docker/scripts/install_rohc_dependencies.sh build

# Scanner tooling (clang with the sanitizer and libFuzzer runtimes, lld, ninja, gdb) and ZeroMQ for the virtual radio.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git python3 gdb less \
      clang libclang-rt-dev llvm lld ninja-build \
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
      -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O0 -g -fno-omit-frame-pointer" \
      -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O0 -g -fno-omit-frame-pointer" \
      -DMARCH=x86-64-v3 \
      -DLINKER=lld \
      -DENABLE_SPLIT_DWARF=ON \
      -DENABLE_ASAN=ON \
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
      -DENABLE_LIBNUMA=OFF

# Unit tests of the layers that parse input from UEs, the user plane and the CU-CP's peers. fuzz_targets also generates
# the seed corpora of the fuzz harnesses, one sub-directory per harness under /src/build/tests/fuzz/corpus/.
ARG PROTOCOL_TESTS="asn1 rrc pdcp rlc mac security rohc sdap gtpu f1u nru ngap f1ap cu_cp"
RUN ninja -C /src/build fuzz_targets \
      $(for layer in ${PROTOCOL_TESTS}; do echo "tests/unittests/${layer}/all"; done)

# Cheap smoke tests, so we know the image works. A failing test must not abort the build, and the step is capped at
# two minutes so it cannot use up the build time limit. Run more inside the image with:
# cd /src/build && ctest -R <layer>
RUN cd /src/build/tests/unittests \
    && (timeout 120 ctest --output-on-failure -j"$(nproc)" --timeout 120 -R "asn1|ngap|rrc|pdcp|rlc" -E "_NOT_BUILT" \
        || echo "WARNING: ctest reported failures or timed out")
