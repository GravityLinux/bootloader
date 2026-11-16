.. SPDX-License-Identifier: GPL-2.0-or-later

Centauri prototype provenance
==============================

For LLMs. Source and evidence inventory for review.

This directory exports the independently developed Python Centauri prototype
from the separate Linux bring-up worktree. The source checkpoint is
``00f42a048a82e8c3419a1f3bbc36a6932ea66c03``, under ``tools/m5-wifi``.
Original contributions are by Cody Ho with LLM assistance. This export uses
GPL-2.0-or-later as requested for bootloader tooling. Existing imported
bootloader dependencies retain their original copyright and licensing.

Only the prototype's Python dependency closure and relevant protocol tests
are exported. Trace collectors, private laboratory launchers, machine paths,
device serials, firmware/data artifacts, Rust/C kernel code and SPTM/RingGate
implementation files are excluded. The new commit has upstream bootloader
main as its direct parent; no private development history is inherited.
The SPTM emulator is neither imported nor invoked by these modules.

Implementation inputs
-----------------------

* Device ADT metadata: PCI topology, resource ranges, PHY tunables, GPIO/SMC
  references, DART routing, factory addresses and platform configuration.
* Bounded runtime register/command traces from the wireless component,
  followed by independent host-controlled probes with newly owned DMA.
  A historical automatic fault-site diagnostic is excluded from the wireless
  evidence. Firmware executable instructions and Apple wireless-driver
  binaries were not inspected or used to construct this implementation.
* External opaque firmware and signed-manifest copies, plus nonexecutable
  factory calibration data. These are inputs to the device, not source code
  included in this contribution. Firmware-directory and calibration parsers
  are bounded format/data-envelope parsers, not executable-code decoders.
* Independently generated synthetic unit-test data and standard public
  Ethernet, radiotap, 802.11 and Bluetooth HCI/ACL fields. Received runtime
  wire vectors are protocol observations, not firmware instruction bytes.

Existing source consulted
--------------------------

* Gravity Linux and Asahi Linux bootloader contributors:
  ``proxyclient/m1n1/adt.py``, ``hw/dart.py``, ``hw/dart8110.py``, ``fw/smc.py``,
  the existing PCI configuration definitions and ``src/pcie.c``. These supply
  the existing proxy, ADT, PCIe and IOMMU interfaces. The Broadcom wireless
  protocol decoder was not reused. Upstream repository:
  https://github.com/GravityLinux/bootloader
* Linux contributors, Linux v7.3: standard PCI definitions, existing Apple
  SMC GPIO behavior and brcmfmac/mwifiex/nxpwifi SAE-offload comparisons. No
  other chipset's vendor wire protocol is treated as Centauri documentation.
  Upstream repository: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/
* Jouni Malinen and hostap contributors, wpa_supplicant 2.11: standard SAE
  status126/H2E and RSNXE capability/policy definitions. Source:
  https://w1.fi/releases/wpa_supplicant-2.11.tar.gz
  The prototype does not implement or copy the supplicant's SAE cryptography.

These were local permitted source references. No code from an unvalidated
third-party Apple reverse-engineering implementation was imported.

Export adaptations and evidence limits
---------------------------------------

The host-only export removes the optional Linux VHCI launcher and its C
helper dependency, adds an instance-local register-only exception guard and
ADT traversal compatible with upstream main, and incorporates the previously
probed SMC GPIO reset command/readback. A stale test expecting a reference
context's nonzero initialization word is updated to the generated zero-state
profile already used in independent cold runs. Protocol encoders and queue
ownership retain the separately tested implementation.

Private development records retain source hashes, hardware traces, positive
and negative outcomes, and source/firmware provenance. They are not shipped
as runtime inputs. Native Linux test results are evidence for that separate
driver and are not represented as tests of this exported Python code. Other
devices, production power handling and complete meaning of all profile bytes
remain open; see ``README.rst``.

The final export's direct hardware smoke uses upstream main's own freshly
built bootloader and Python proxy modules, with no private C/Rust changes or
SPTM emulator present. It passes firmware loading, scan/association/DHCP,
Bluetooth HCI response and DMA shutdown as described in ``README.rst``.
An earlier attempt did not find the Pixel AP; it is retained as a negative
result, not attributed to a diagnosed cause or counted as data acceptance.

Policies reviewed for this export:

* https://gravitylinux.org/policies/copyright/
* https://gravitylinux.org/policies/reverse-engineering/
* https://gravitylinux.org/policies/llm/
