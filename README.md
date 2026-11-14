Ixolo
=====

Ixolo is a free, open-source desktop operating system built on the
**MINIX 3 microkernel**. It keeps what makes MINIX distinctive (a tiny
kernel, with drivers and servers isolated in user mode and restarted when they
crash) and adds what a modern desktop needs: a 64-bit SMP kernel, UEFI boot,
a Wayland compositor, Qt 6 and the LXQt desktop.

> **Ixolo and MINIX.** Ixolo is the system; MINIX 3 is its kernel and OS
> interface, in the same way macOS runs on Darwin. `uname -s` still says
> `Minix`, and software written for MINIX keeps working unchanged. The project
> was called "MINIX 3 (davmont fork)" until November 2026. It was renamed
> because upstream MINIX 3 has not been developed since 2018 (its last stable
> release was 3.3.0, in 2014), because the MINIX name is not ours to ship a
> product under, and because the system has grown into something different. It is
> **not** maintained or endorsed by the Stichting MINIX Research Foundation.
> See [`docs/NAMING.md`](docs/NAMING.md) for the full reasoning and for what
> the rename does and does not touch.

---

## Key Features

- **Fault Tolerance** — Most of the OS runs as isolated user-mode processes.
  If a driver crashes, the Reincarnation Server restarts it without affecting
  the rest of the system.
- **Microkernel Design** — The MINIX kernel is only about 6,000 lines of
  executable code, which makes it easier to verify and secure.
- **64-bit and SMP** — x86-64 (amd64) with symmetric multi-processing up to
  32 CPUs, and boot from legacy BIOS or UEFI.
- **Graphical desktop** — `wlcompd` (a Wayland compositor) runs Qt 6 and LXQt,
  with a panel, taskbar, four virtual desktops and a terminal. `startlxqt`
  brings it all up with one command.
- **Hardened** — NX, SMEP/SMAP, ASLR, stack protector and FORTIFY across
  userland.
- **POSIX and NetBSD userland** — A NetBSD-derived userland and pkgsrc.
- **Modern toolchain** — Built with LLVM/clang and LLD only, with no GCC and no
  GNU binutils.
- **Educational foundation** — MINIX was created by Andrew S. Tanenbaum, and
  its source remains a world-class resource for learning OS design.

---

## Architecture

Ixolo inherits the MINIX 3 structure of four layers:

| Layer | Name | Responsibility |
|-------|------|----------------|
| 1 | Microkernel | Interrupts, scheduling, message passing |
| 2 | Device Drivers | Run in user space (disk, network, USB, ...) |
| 3 | Server Processes | VFS, Process Manager, Reincarnation Server |
| 4 | User Programs | Shells, editors, applications |

---

## What Ixolo Adds to MINIX 3

Ixolo started from upstream MINIX 3 (`4db99f4`, November 2018). The
headline changes are a working **x86-64 (amd64) kernel port** that boots on
both legacy BIOS and UEFI firmware, and a **graphical desktop** on top of it.
Highlights:

- **Desktop stack** — `wlcompd`, a Wayland compositor with xdg-shell,
  layer-shell, foreign-toplevel and workspace support; libwayland, Qt 6.11
  (statically linked), D-Bus 1.16, GLib 2.80, LXQt 2.4 (session, panel,
  runner, config) and qterminal. See [`docs/WAYLAND.md`](docs/WAYLAND.md) and
  [`releasetools/DESKTOP.md`](releasetools/DESKTOP.md).
- **Memory and POSIX** — Writable shared file mappings and `msync`, `mprotect`,
  `mlock`, `MAP_SHARED` anonymous memory across `fork`, POSIX shared memory on
  a new `tmpfs`, asynchronous and deferred thread cancellation, `ppoll`,
  `getentropy`.
- **Exploit mitigations** — SMEP/SMAP, NX for non-executable mappings, ASLR for stack and
  `mmap`, the stack protector and FORTIFY on amd64, and OpenSSH 10.6.
- **amd64 SMP** — Full AP bring-up, per-CPU GDT/IDT/TSS, BKL serialization,
  validated on `-smp 2/4/8` up to 32 CPUs. (See the "amd64 subsystem state"
  table below.)
- **IPC fastpath + colocation** — SENDREC same-CPU fastpath plus a Tier 1
  scheduler that migrates servers to their dominant client's CPU; **4.86×**
  SMP IPC throughput improvement on `-smp 4` (p50 78k → 16k cycles).
- **Hybrid BIOS + UEFI boot** — Single CD image boots on either firmware via
  El Torito with a GRUB-built EFI System Partition. Multiboot 2.0 + ACPI
  RSDP handoff implemented; linear-framebuffer text console for headless
  UEFI environments.
- **Modern CPU features** — NX/XD enforcement, XSAVE/AVX FPU context, eager
  FPU dispatch on amd64, SSE4 / AVX2 / AES-NI / RDRAND / x2APIC / PCID
  detection.
- **New drivers** — Intel I225/I226 2.5 GbE (`igc`), EHCI USB host (Phase 1),
  OHCI scaffolding (Phase 3a), multi-controller `usbd`, AHCI / legacy IDE
  auto-detect at boot.
- **Allocator** — Switched libc to `jemalloc`.
- **Security hardening** — Tree-wide audit replacing unsafe `strcpy` /
  `sprintf` / `system()` patterns; buffer-overflow fixes in `kgets`, `backup`,
  `fdisk`, `minix-service`; ELF auxvec UID/GID populated correctly.
- **Performance** — 30+ O(N²) `strlen`-in-loop patterns eliminated across the
  tree.
- **Build & toolchain** — LLVM/clang and LLD only (GCC and GNU binutils
  removed), host clang 22 as the cross compiler, NetBSD 10 host-tools sync,
  C++ ATF and `kyua-cli` tests re-enabled.
- **Test coverage** — New ATF tests for `dup2`, libc string functions, the
  ISC event timer library, `hgfs`, `cd9660`, and PM `ptrace`.

For the full change log, see [`RELEASE_NOTES.md`](RELEASE_NOTES.md).

---

## amd64 Subsystem State

A snapshot of where each piece of the amd64 port stands. ✓ = production-grade,
↻ = working with caveats, ⌛ = in progress, ✗ = not implemented.

| Subsystem | Status |
|-----------|--------|
| Boot (legacy BIOS) | ✓ |
| Boot (UEFI / GPT, q35) | ✓ via hybrid El Torito; Multiboot 2.0 handoff |
| SMP | ✓ up to 32 CPUs; validated on `-smp 2/4/8` |
| IPC fastpath + Tier 1 colocation | ✓ 4.86× SMP win (p50 78k → 16k cycles) |
| FPU / SSE | ✓ eager dispatch |
| Storage (AHCI + IDE) | ✓ auto-detect + graceful fallback |
| Console (serial) | ✓ |
| Console (linear framebuffer) | ✓ for UEFI / GOP environments |
| ACPI / APIC | ✓ |
| Intel `igc` 2.5 GbE | ↻ basic send/receive; no coalescing/multi-queue/stats |
| USB EHCI (USB 2.0) | ↻ Phase 1 transfer model; no isoch, no suspend/resume |
| USB OHCI (USB 1.1) | ⌛ Phase 3a scaffolding (PCI + DMA pools); transfer scheduling next |
| USB UHCI | ✗ |
| USB xHCI (USB 3.x) | ✗ |
| UEFI runtime services | ✗ shutdown/reboot via ACPI only |
| Secure Boot | ✗ |

---

## Known Limitations

| Area | Status |
|------|--------|
| **Intel igc driver** | Initial send/receive only. No interrupt coalescing, multi-queue, or statistics support yet. |
| **EHCI USB (amd64)** | Phase 1 only. Isochronous transfers and suspend/resume not implemented. |
| **amd64 USB OHCI** | Phase 3a scaffolding only (PCI discovery + DMA pools). Transfer scheduling next. |
| **amd64 USB UHCI / xHCI** | Not yet wired. |
| **UEFI runtime services** | No `EFI_RUNTIME_SERVICES` integration. Shutdown and reboot rely on ACPI paths only. |
| **Secure Boot** | Not supported. Requires a signed shim and signed GRUB image. |

---

## Getting Started

### Prerequisites

For the best experience, run Ixolo inside a virtual machine.

- **Hypervisor:** QEMU/KVM (best tested), VirtualBox or VMware
- **Image:** build from source (see below). There are no prebuilt Ixolo
  images yet; the upstream MINIX 3 downloads are a different system.
- **Build host:** Linux with clang 20 or newer (the build is tested with
  clang 22).

### Building from Source

```sh
# amd64: replace 16 with your CPU core count, clang-22 with your host clang.
# A fresh object directory needs the `tools` step; later builds can add -u.
sh build.sh -c clang-22 -j16 -mamd64 -O ../build -U tools release
OBJ=../build bash releasetools/amd64_cdimage.sh

# i386 (the original upstream target)
sh build.sh -j4 -mi386 -O ../obj.i386 -U release
bash releasetools/x86_cdimage.sh
```

The build produces `minix_amd64.iso` in the current directory. The image
files keep their `minix_*` names; see [`docs/NAMING.md`](docs/NAMING.md).
To include the LXQt desktop in the image, see
[`releasetools/DESKTOP.md`](releasetools/DESKTOP.md) (`MKDESKTOP=yes`).

### Testing an image

`releasetools/qemutest.py` boots an ISO under QEMU, logs in over the serial
console and runs the POSIX test suite that every ISO now carries in
`/usr/tests/minix-posix`.  It uses KVM when `/dev/kvm` is available and plain
TCG otherwise, so it runs unchanged on a laptop and on a hosted CI runner.

```sh
python3 releasetools/qemutest.py --iso minix_amd64.iso --suite boot    # boots to login
python3 releasetools/qemutest.py --iso minix_amd64.iso --suite quick   # check-install subset
python3 releasetools/qemutest.py --iso minix_amd64.iso --tests "1 2 3" # a hand-picked list
python3 releasetools/qemutest.py --iso minix_amd64.iso --suite pjdfstest # file-system conformance
python3 releasetools/qemutest.py --iso minix_amd64.iso --suite quick --shard 1/4 # a quarter of it
```

The exit code is 0 only when every test passed; the serial transcript and the
TAP output land in `qemutest-logs/`.  `releasetools/qemutest.xfail` lists the
known failures on amd64: pass it with `--xfail` and those count as expected,
anything new fails the run, and a listed test that starts passing fails the run
until it is removed from the list.  `--shard K/N` runs every Nth test starting
at the Kth, so N machines can split a suite.  `.github/workflows/ci.yml` builds
the ISO once on every push and pull request and runs the quick suite and
pjdfstest across six such shards under the runner's KVM, plus both suites
once more on a two-CPU guest (`--smp 2`); a manual run with `tcg=true` takes
the slow TCG path.

### Running in QEMU

> **Important — `-cpu host` is required for amd64.**
> The kernel uses the `WRFSBASE` instruction for TLS setup; QEMU's default `kvm64`
> CPU model does not expose that feature (CPUID leaf 7 EBX bit 0), so the
> kernel will panic at boot without it.

```sh
# amd64 — bootloader path (recommended)
qemu-system-x86_64 --enable-kvm -cpu host -smp 4 -m 256 \
    -cdrom minix_amd64.iso

# amd64 — direct kernel path (faster iteration)
qemu-system-x86_64 --enable-kvm -cpu host \
    -kernel ../build/minix/kernel/kernel \
    -append "bootcd=1 cdproberoot=1" \
    -cdrom minix_amd64.iso

# i386
qemu-system-i386 --enable-kvm -m 256 -cdrom minix_i386.iso

# amd64 HDD image
qemu-system-x86_64 --enable-kvm -cpu host -m 256 -hda minix_amd64.img
```

### Installation (from ISO)

1. Create a VM (256 MB RAM, 2 GB+ HDD).
2. Mount the ISO and boot (use `-cpu host` for amd64 in QEMU).
3. Log in as `root` (no password).
4. Run `setup` and follow the prompts.
5. Power off, remove the ISO, reboot.
6. To start the desktop on an image built with it, log in and run
   `startlxqt`.

Installation and usage documentation is at [www.ixolo.org](https://www.ixolo.org).

---

## Documentation

- **Website and documentation:** [www.ixolo.org](https://www.ixolo.org)
- **Bug reports and feature requests:** [GitHub issues](https://github.com/davmont/ixolo/issues)

In this repository:

- [`docs/NAMING.md`](docs/NAMING.md): Ixolo vs MINIX, what is called what and why
- [`RELEASE_NOTES.md`](RELEASE_NOTES.md): detailed change log since the fork
- [`docs/WAYLAND.md`](docs/WAYLAND.md) and [`releasetools/DESKTOP.md`](releasetools/DESKTOP.md): the graphics and desktop stack
- [`docs/UEFI_BOOT.md`](docs/UEFI_BOOT.md): UEFI and GPT boot
- [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md): third-party code and how to update it

The legacy MINIX 3 project, for historical reference only. It is no longer
developed and does not document Ixolo, but its design material still describes
the kernel and servers Ixolo inherited:

- [MINIX 3 Wiki](https://wiki.minix3.org) (archive of the upstream project)
- *Operating Systems: Design and Implementation* (3rd ed.), Tanenbaum & Woodhull

---

## Contributing

Pull requests are welcome. Whether you are fixing bugs, porting drivers, or
improving documentation:

- The source tree is at `/usr/src` on a running Ixolo system.
- Call the system *Ixolo* and the kernel *MINIX*, and never rename the
  identifiers listed in [`docs/NAMING.md`](docs/NAMING.md).
- Please follow existing BSD code style.
- Prefer `strlcpy`/`strlcat` over `strcpy`/`strcat`; `snprintf` over `sprintf`.
- For security-sensitive changes, reference the relevant CWE in the commit
  message.

---

## License

Ixolo is released under the **BSD-3-Clause License**, the licence of the
MINIX 3 code it is built on. See the `LICENSE` file for details. Third-party
components keep their own licences; see [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).

MINIX 3 is copyright the Vrije Universiteit, Amsterdam. "MINIX" refers to
the upstream project, which does not maintain or endorse Ixolo.
