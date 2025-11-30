.. SPDX-License-Identifier: GPL-2.0

=======================
Historical Contributors
=======================

This driver is based on internal development at Habana/Intel between 2021–2024.
The Gaudi3 support introduced here corresponds to the v1.24.1 out-of-tree
driver release, reworked for upstream submission.

Major Internal Contributors
============================

The following individuals made significant contributions to the Gaudi3 device
support during internal development (June 2021 - September 2024):

Tomer Tayar (732 commits)
--------------------------
Major contributions include:

- ASIC initialization and configuration infrastructure
- Hardware block initialization (TPC, MME, EDMA, Decoders, Rotators)
- Event queue (EQ) handling and interrupt aggregators
- Idle status checks for all engines
- MMU_BP/ASID configuration for all engines
- PARC AXI drain and security configuration
- HBM/MC SEI event handling and error reporting
- Device initialization sequences and halt functions
- Cache maintenance and AXI transaction verification
- Coresight integration and debugging infrastructure
- DMA-buf implementation and SG table handling
- Accel subsystem registration and integration
- Graceful hard reset mechanism
- Simulator infrastructure and IRQ handling
- Virtual MSI-X doorbell support
- Privileged and secured RR (Range Register) configuration
- PCIe AXI drain handling
- CBC (Cache Block Controller) initialization


Ofir Bitton (436 commits)
--------------------------
Major contributions include:

- Hardware specifications alignment and updates (v1.0 through v2.50+ specs)
- Interrupt mapping and event handling framework
- ASIC register definitions and headers maintenance
- Simulator support infrastructure (Gaudi2/Gaudi3)
- Clock throttling and hardware management
- Auto-fetcher MMU properties configuration
- Special block iterators and configuration
- KDMA completion mechanism
- Rate limiters and performance optimization
- Debugfs interfaces and error reporting
- Device reset and recovery mechanisms
- Binning support and device masking (TPC, MME, DEC, HBM)
- Security and privilege configuration automation
- Event aggregation and async event handling
- ARC simulation support
- FPGA support removal and cleanup
- Multi-MSI removal and interrupt consolidation


Ohad Sharabi (449 commits)
---------------------------
Major contributions include:

- MMU v3 architecture (HMMU, STLB, DTLB) complete implementation
- Memory management infrastructure and page table handling
- Hardware specifications and NOC configuration (v1.18-1.21)
- HBM initialization, binning, and cluster management
- D2D PHY/MAC initialization with FSM
- Multi-page support (1MB, 2MB, 32MB, 1GB pages)
- Memory allocation, prefetching, and DMA tracing
- Multi-CS implementation and wait mechanisms
- Scrambling and credits configuration automation
- Module iterators and HBM/MC iterators
- MMU cache invalidation flows (all/range)
- Range register infrastructure (LBW/HBW RR)
- COMMS protocol and SP (Scratchpad) support
- Automated scripts for NOC configs, credits, and scrambling
- Preboot and boot-fit support
- DRAM properties and page size configuration
- Host-resident page table support


Oded Gabbay (407 commits)
--------------------------
Major contributions include:

- Driver architecture and core infrastructure
- Firmware interface and specifications maintenance
- DMA-buf import/export full support
- Device registration and cdev management
- Reset flows (hard/soft/compute reset) and recovery
- Compute reset implementation and graceful reset
- Server type detection and device identification
- Driver-firmware interaction protocols and CPU-CP packets
- Code refactoring for upstream submission
- Build system, Kconfig, and Makefile infrastructure
- Accel subsystem migration from misc to DRM accel
- Context management and device lifecycle
- Decoder/encoder (formerly TC/video) infrastructure
- IRQ and interrupt handling unification
- Module parameters and device properties management
- Sysfs interface implementation
- Clock and PLL management
- Legacy code removal and cleanup


Koby Elbaz (312 commits)
-------------------------
Major contributions include:

- Protection bits (PB) security configuration (secured/privileged)
- PDMA (Platform DMA) complete implementation (12 channels)
- Special blocks security headers auto-generation
- ETR (Embedded Trace Router) buffers implementation
- KDMA (kernel DMA) channel 0 management
- Security automation scripts and tooling
- HBW test infrastructure for PDMA boot validation
- Fabric Serialization Enhancement support
- Page fault queue (PFQ) implementation
- Boot-time hardware validation via PDMA tests
- PQM (PDMA Queue Manager) configuration
- Dual-die support for PDMA operations
- COMMS protocol support (using SPs instead of GIC/ARC)
- Hard reset flows and recovery with COMMS
- Compute context and soft reset handling
- CPU-CP queue testing infrastructure
- Engine modes setting (run/stop)
- Special blocks iterator configuration
- Security emulation and priv PB assertions


Farah Kassabri (38 commits)
----------------------------
Major contributions include:

- HMMU page table placement in HBM
- MMU v3 map/unmap unified functions
- NVMe Direct IO support infrastructure
- Device boot error checking mechanisms
- Heartbeat mechanism and EQ heartbeat handling
- PCIe flush register configuration
- PDMA engine SEI error handling
- CPU packet timeout diagnostics and debugging
- GAUDI2D revision support
- Dynamic memory property updates
- Preboot status checking
- Interrupt aggregator support


Moti Haimovski (187 commits)
-----------------------------
Major contributions include:

- BMON/SPMU interrupt handling
- CS SEI handler updates
- PDMA parallel channel testing (all 12 channels)
- Completion Queue error recovery
- Signed device info API implementation
- Event handling infrastructure
- Memory mapping with vmalloc support


Dani Liberman (176 commits)
----------------------------
Major contributions include:

- NOC (Network-on-Chip) configuration (v1.18-1.21)
- RAZWI (Read/Write Violation) logger and capture
- Error capture and diagnostics infrastructure
- Device scrubbing using PDMA (up to 4GB+)
- Address decoder (ADDR_DEC) error handling
- Historic data fetching API
- Secured attestation support and TPM
- ARC_FARM_SEI event handlers
- Interrupt storm prevention
- Page fault notification events
- Dynamic HBM reservation for firmware
- Device security status exposure
- PSOC RAZWI handling
- Special blocks access control
- Engine status retrieval API
- PCIe AXI drain initialization


Dafna Hirschfeld (154 commits)
-------------------------------
Major contributions include:

- PMMU (Peripheral MMU) implementation and initialization
- MME (Matrix Multiply Engine) event handling (SPI/SEI)
- TPC SPI+SEI event handling
- QM (Queue Manager) software interrupt support
- NCH (Network Coherence Hub) event handling
- STLB hdcore interrupt handling
- Device scrubbing via debugfs interface
- Debugfs infrastructure refactoring and unification
- Page fault diagnostics and initiator identification
- Memory scrubbing with configurable values
- Debugfs read/write 64-bit support
- AXID to initiator mapping
- Rotator event handling
- Decoder event handling
- ODP (On-Demand Paging) support and page-in flow


Yuri Nudelman (130 commits)
----------------------------
Major contributions include:

- MMU cache invalidation flows and optimization
- PMMU initialization and configuration
- LBW DUP (Duplicator) API implementation
- SRAM initial configuration
- NRTR/GRTR (router) credits initialization
- ODP (On-Demand Paging) base data structures and xarray support
- Unified memory manager for CB (Command Buffer) flows
- State dump infrastructure for stuck CS debugging
- Autonomous controller utilization
- Tracer user API
- Page fault queue (PFQ) management
- Scheduler submission infrastructure
- ETR (Embedded Trace Router) buffers
- Memory manager with handles and topics
- SM (Sync Manager) block exposure
- ARC MMU initialization
- Simulator IRQ refactoring


Tal Cohen (75 commits)
----------------------
Major contributions include:

- EDMA specifications and updates
- FPGA support implementation and removal
- Simulator memory command ioctl
- Register read API implementation
- Eventfd notification support
- ARC cores running mode control
- Threaded IRQ for user interrupts and decoders
- Error collection infrastructure
- Preboot ASCII message support
- Build and bringup automation scripts
- Hard reset implementation for Gaudi3
- Device unavailable notification
- EQ MSIX interrupt support
- Undefined opcode error handling
- Command submission sanity checks


Additional Contributors
========================

The Gaudi3 development effort involved 100+ contributors from the Habana Labs
and Intel teams between June 2021 and September 2024. The work included
hardware bring-up, firmware development, driver implementation, testing,
and validation across multiple hardware revisions and PLDM configurations.

Key development areas included:

- Hardware abstraction and ASIC-specific implementations
- Memory management (MMU v1/v2/v3, HMMU, PMMU, DMMU)
- Security infrastructure (PBs, RRs, ASID)
- Power management and clock gating
- Coresight and debugging tools
- Event handling and error reporting
- Reset and recovery mechanisms
- Firmware interface protocols
- Simulator and FPGA support

Pre-silicon bring-up, simulator and FPGA support were part of that internal
effort but are not carried upstream; the upstream driver targets production
silicon driven by released firmware.

Maintainers
===========

Code cleanup and upstream preparation by:
  - Konstantin Sinyuk, accel/habanalabs maintainer
  - Koby Elbaz, accel/habanalabs maintainer


Revision History
=================

v6.19 submission (2025-12-01, withdrawn)
------------------------------------------

The original tags/drm-habanalabs-next-2025-12-01 pull request was withdrawn
after review (see the "[git pull] accel/habanalabs: Gaudi3 support and
updates for v6.19" thread on dri-devel) because it carried several
internal-build-only artifacts and had accidentally reverted a small number
of upstream fixes that were merged into drm-next after this series was
last synced internally:

- unrelated ``.clang-format`` changes
- a hardcoded ``CONFIG_DRM_ACCEL_HABANALABS := m`` override and an
  ``OFED_PATH``/``KBUILD_EXTRA_SYMBOLS`` hook in the driver Makefile
- ``secs_to_jiffies()`` timeout conversions (commit 78cf56f8832a and
  related work) reverted back to ``msecs_to_jiffies(x * 1000)`` at five
  call sites
- ``depends on X86 && X86_64`` in Kconfig narrowed to ``depends on X86_64``

v6.20 resubmission
--------------------

This series was rebuilt from scratch on top of the current drm-tip
integration branch (rather than the November 2025 drm-next snapshot used
for the withdrawn v6.19 attempt), so it picks up everything that landed
upstream in the meantime, including the ``kmalloc_obj()`` conversions
across ``context.c``, ``device.c``, ``hw_queue.c``, ``hwmon.c``, and the
per-ASIC files.

Every issue raised during the v6.19 review is fixed **within** the same
17-commit Gaudi3 introduction series rather than as follow-up patches, so
each commit builds cleanly on its own and no commit in the series
knowingly regresses upstream code. A small script
(``scripts/check-drm-tip-parity.sh``) was added so a regression of this
kind is caught mechanically the next time this driver is synced from the
internal tree; it asserts both the upstream idioms that must be present
and the out-of-tree constructs that must not reappear, and reports no
warnings or accepted deviations against drm-tip.

Content removed for upstream
-----------------------------

The v1.24.1 tree carried a large amount of material that only made sense
inside the internal build:

- The Gaudi3 register headers were trimmed from the full ~11MB/189-file
  RTL-exported dump down to the ~380KB subset of macros the driver
  actually references, since the vast majority of the raw dump was dead
  weight from an upstream-review point of view.
- Pre-silicon bring-up, simulator and FPGA sources (~15k lines) were
  dropped along with the ``HL_DOWNSTREAM`` build gating that selected
  them.
- The out-of-tree compatibility layer was removed in favour of the
  current kernel APIs it was shimming.
- ``gaudi3_coresight_regs.h`` held 26 ``mmFUNNEL_*_OFFSET`` macros that
  were never referenced by Gaudi3 code and, after the register trim,
  expanded to register names that no longer exist. They compiled only
  because unused macros are never expanded, so the header was removed
  and its includes folded into ``gaudi3_coresight.c``.

