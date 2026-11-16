# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep the standalone hardware prototype's exception reports register-only."""


def disable_code_reads(utils):
    """Disable fault-site disassembly, including on older upstream proxies.

    Setting allow_disassembly alone has no effect on versions that predate
    that option. Override the instance method before any hardware access.
    No global proxy or hypervisor behavior is changed.
    """
    def omit_disassembly(*args, **kwargs):
        print('Code context omitted by the Centauri hardware prototype')

    utils.allow_disassembly = False
    utils.disassemble_at = omit_disassembly
