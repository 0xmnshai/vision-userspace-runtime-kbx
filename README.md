# KBX Vision Runtime ( currently working on)

A high-performance, kernel-bypass Edge AI pipeline for Intel Architecture. This runtime implements a zero-copy data path from camera ingress to bare-metal display scanout using Level Zero, OpenVINO, and Vulkan.

## Core Architecture

* **Zero-Copy Pipeline**: DMA-BUF sharing between V4L2, Level Zero, OpenVINO, and Vulkan.
* **Kernel Bypass**: Asynchronous I/O via `io_uring` and bare-metal display via DRM/KMS Atomic Commit.
* **Low Latency**: Custom SPIR-V kernels for color space conversion and Level Zero immediate command lists.
* **Telemetry**: Nanosecond-level GPU profiling via eBPF kprobes and Intel PMU counters.

## Prerequisites

* **Hardware**: Intel Gen12+ GPU (Tiger Lake, Alder Lake, Arc, etc.)
* **Kernel**: Linux 5.15+ (6.x recommended for improved `io_uring` and eBPF support)
* **Libraries**:
    * OpenVINO Runtime
    * OneAPI Level Zero Loader
    * Vulkan SDK
    * `liburing`, `libdrm`, `libgbm`, `libbpf`, `libnuma`
* **Tools**: `ocloc` (for SPIR-V compilation), `bpftool`, `clang-18`

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

The build process includes offline compilation of SPIR-V kernels and eBPF skeleton generation.

## Project Structure

* `src/compute`: Level Zero engine and USM management.
* `src/infer`: OpenVINO RemoteContext and zero-copy inference.
* `src/gfx`: Vulkan renderer and DRM/KMS atomic display.
* `src/io`: V4L2 camera interface and `io_uring` reactor.
* `src/telemetry`: eBPF kprobes for i915/xe driver profiling.
* `src/kernels`: OpenCL/SPIR-V compute kernels.

## Running

Ensure your user has permissions for `/dev/dri/card0`, `/dev/video0`, and `MEMLOCK` limits for hugepage allocation.

```bash
./kbx_vision_runtime
```
