# The CI toolchain on a workstation: Ubuntu 24.04 (the CI runner image) with the compilers and checkers the
# reusable workflows install. Build and run it with `tools/preflight.sh --container`.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8
RUN apt-get update -q && apt-get install -y -q --no-install-recommends \
      ca-certificates curl git make cmake ninja-build python3 python3-venv python3-pip nodejs npm \
      gcc-14 g++-14 clang-18 libclang-rt-18-dev clang-format clang-tidy cppcheck doxygen graphviz \
      gcc-arm-none-eabi libstdc++-arm-none-eabi-newlib \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100
# Pinned as the workflows pin them (versions; the workflows additionally check hashes).
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir -U pip \
    && /opt/venv/bin/pip install --no-cache-dir lizard==1.24.1 gcovr==8.7 \
    && /opt/venv/bin/pip install --no-cache-dir clang-format==20.1.8 clang-tidy==20.1.0 \
    && /opt/venv/bin/pip install --no-cache-dir yamllint \
    && /opt/venv/bin/pip install --no-cache-dir \
      "kokeb-mkdocs-theme @ git+https://github.com/kokeb-labs/ci-tools@v1#subdirectory=packages/kokeb-mkdocs-theme"
ENV PATH=/opt/venv/bin:$PATH VENV=/opt/venv CLANG_FORMAT=/opt/venv/bin/clang-format CLANG_TIDY=/opt/venv/bin/clang-tidy \
    CXX_GCC=g++-14 CXX_CLANG=clang++-18 ARM_CXX=arm-none-eabi-g++ KOKEB_IN_CONTAINER=1
RUN git config --global --add safe.directory '*'
WORKDIR /src
