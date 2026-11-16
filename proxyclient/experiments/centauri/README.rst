.. SPDX-License-Identifier: GPL-2.0-or-later

Centauri host Python prototype
===============================

For LLMs. Experimental hardware tooling; human review is still required.

This is a host-side Python driver for the Apple Centauri Wi-Fi/Bluetooth
controller. It uses the bootloader's serial proxy to access real hardware.
It can reset the chip, train PCIe, upload externally supplied firmware and
create its own control, Wi-Fi and Bluetooth DMA queues. No hypervisor,
SPTM emulator, macOS session or Linux kernel driver is started by these tools.
The directory contains Python source, synthetic protocol tests and these
notes. Firmware, factory calibration, captured state and credentials are
external inputs and are not distributed here.

The code was developed with hardware/register and typed runtime-data traces,
ADT metadata and controlled probes. Apple executable code was not used as
implementation input. See ``SOURCES.rst`` for provenance and limitations.

Run the tests
-------------

Install the normal proxyclient Python dependencies, then from the repository
root run::

    PYTHONPATH=proxyclient python3 -m unittest discover \
        -s proxyclient/experiments/centauri -p 'test_*.py'

These tests use synthetic memory/register backends and protocol vectors;
they do not open a proxy device. They cover bounded parsing, queue ownership,
DMA failure cleanup, firmware-data envelopes, Wi-Fi scan/SAE/PSK encoders and
Bluetooth HCI behavior. They also verify that exception diagnostics cannot
read faulting code and that the local ADT traversal reaches nested providers.

Firmware and board data
-----------------------

``prepare_centauri_firmware.py`` packages an explicitly supplied vendor image,
signed manifest, NVRAM, calibration and AP-policy records. Executable payloads
are copied opaquely; this does not disassemble them, generate signatures or
establish redistribution rights. Use this device's factory calibration;
calibration blocks remain opaque. NVRAM can be generated from the platform's
``apple,antenna-sku`` property. ``centauri_ap_policy.make_ap_policy`` encodes
decoded platform properties with an explicit four-byte record4 value whose
semantics remain unassigned. Do not substitute another board's data.

For example, with independently obtained local resources::

    python3 proxyclient/experiments/centauri/prepare_centauri_firmware.py \
        --vendor /path/to/vendor-image --manifest /path/to/signed-manifest \
        --antenna-sku-file /path/to/antenna-sku-property \
        --calibration-factory-record /path/to/device-factory-record \
        --ap-policy /path/to/generated-ap-policy --output /path/to/image.bin

Start an owned runtime
-----------------------

Confirm the target device and RAM-chainload a suitable bootloader first. Set
``M1N1DEVICE`` explicitly to its proxy node. No serial-number restriction or
device-selection guess is implemented. Close any previous radio owner and
use a full chip reset before reinitialization. The prototype allocates and
zeroes its own pages; it does not reclaim allocations until reboot.

The following command performs the qualified two-stage upload, creates fresh
runtime interfaces and enters a host Python shell::

    PYTHONPATH=proxyclient python3 \
        proxyclient/experiments/centauri/probe_bootstrap_dma.py \
        --assert-chip-reset --enable-msi --reference-pcie --reference-aux \
        --bootstrap-host-time --bootstrap-d3hot \
        --opaque-payload /path/to/image.bin --boot-runtime --runtime-image \
        --runtime-opaque-payload /path/to/image.bin \
        --runtime-boot-buffer-from-image --keep-ready-dma \
        --initialize-runtime-host --shell

The shell provides ``p`` (proxy), ``u`` (utilities), ``d`` (controller),
``result`` (last upload) and ``runtime_host``. The names ``reference-pcie``
and ``reference-aux`` select independently encoded register operations;
they do not load a recorded replay stream. Initial PHY calibration/tunables
come from the device's ADT. SMC GPIO commands are qualified separately from
the ADT action argument. The local disassembly guard supports upstream
proxy versions that do not implement an ``allow_disassembly`` flag.

``runtime_host.wifi_queues`` supplies management queries and bounded receive
observers. ``centauri_wifi_messages`` encodes address/interface/country
commands; ``centauri_wifi_scan`` encodes passive scans and decodes BSS/public
SAE metadata. ``centauri_wifi_connection`` encodes PSK or SAE requests and
connection/disconnect events. Callers supply credentials in memory and must
clear temporary request data; no network password is embedded in the tools.
An acknowledged management command is not an association-success event.

After association, ``centauri_ethernet_owned.OwnedEthernetQueues`` allocates
fresh Ethernet rings and buffers. Its ``send``/``poll_rx`` methods carry
ordinary Ethernet frames. It is a packet transport, not a host OS network
interface or an included TCP/IP stack. The base Ethernet class retains an
older optional attach-to-existing-queues path; the owned runtime does not
use that path or require captured queue state.

``runtime_host.bluetooth.transport`` provides HCI command/event and ACL
traffic over freshly generated queues. ``centauri_bluetooth`` includes
bounded controller probes and named vendor operations qualified by controlled
experiments. The Linux VHCI/C-library launcher and retained-memory Linux
adapter are intentionally excluded from this host prototype.

Exiting the shell closes the runtime and disables busmastering/DMA streams.
Do not read an unbound or memory-disabled function's BAR. Reset the chip
before another initialization rather than adopting stale rings or caches.

Qualified scope
----------------

The M5 Air/T8142 experiments qualify fresh firmware loading and generated
contexts, passive 2.4 GHz scans, WPA2-PSK/CCMP, SAE with required PMF, Ethernet
DHCP exchanges and Bluetooth HCI/ACL behavior. The H2E request encoder supplies
standard RSNXE data; callers must check peer capability and observed method.
It does not claim an undocumented exclusive firmware PWE-selector bit.

Other bands, roaming, power management, automatic recovery, full Bluetooth
profiles/LE/SCO and other boards need further qualification. Some profile
words and factory calibration contents remain unassigned or opaque. Working
bring-up does not establish complete hardware understanding or production
readiness. Source stays separate from the Rust Linux implementation.

The exported code was also smoke-tested against unmodified upstream
bootloader main ``f02d4a52017915febb222539636b5b025a70407a`` built with
``NO_DISPLAY=1``: full SMC GPIO chip reset, both firmware stages,25 decoded
scan reports with no decoder errors, WPA2 association and a fresh DHCP ACK
through generated Ethernet queues. HCI Read Buffer Size returns ACL MTU2586.
All three busmasters are disabled at close and DART error is zero. This run
checks Bluetooth HCI startup, not a repeat of full Bluetooth pairing/profile
qualification. The previous Pixel WPA2 test belongs to the separate native
Linux driver; the exported Python smoke uses the other authorized test AP.
