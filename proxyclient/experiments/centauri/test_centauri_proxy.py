# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the upstream compatibility paths without reading device memory."""
import contextlib
import io
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from centauri import Centauri, adt_nodes, SMC_GPIO_INFO, SMC_GPIO_OUTPUT
from centauri_proxy import disable_code_reads


class Node:
    def __init__(self, name, children=(), **properties):
        self.name, self.children, self.properties = name, children, properties

    def __iter__(self):
        return iter(self.children)

    def getprop(self, name):
        return self.properties.get(name)


class CompatibilityTests(unittest.TestCase):
    def test_local_walker_includes_direct_children_and_deep_nodes(self):
        root = Node('root', [Node('child', [Node('nested')]), Node('sibling')])
        self.assertEqual([n.name for n in adt_nodes(root)],
                         ['root', 'child', 'nested', 'sibling'])

    def test_disassembly_guard_cannot_read_faulting_code(self):
        memory = Mock(side_effect=AssertionError('Unexpected code read'))
        utils = SimpleNamespace(iface=SimpleNamespace(readmem=memory),
                                disassemble_at=memory)
        disable_code_reads(utils)
        with contextlib.redirect_stdout(io.StringIO()):
            utils.disassemble_at(0x1000, 36, pc=0x1010)
        memory.assert_not_called()
        self.assertFalse(utils.allow_disassembly)

    def reset_driver(self):
        reset = SimpleNamespace(name='pKW4', args=[int.from_bytes(b'gTST', 'big'), 0x800000])
        bridge = SimpleNamespace(getprop=lambda name: reset)
        node = {'pci-bridge0': bridge}
        utils = SimpleNamespace(adt={'/arm-io/smc': SimpleNamespace(get_reg=lambda index: (0x4000, 0x4000))})
        return SimpleNamespace(node=node, u=utils)

    def test_reset_uses_gpio_command_and_verifies_output(self):
        smc = Mock()
        smc.smcep.get_key_info.return_value = (4, 'ui32', 0)
        smc.smcep.rw32.side_effect = [1, 0]
        with patch('m1n1.fw.smc.SMCClient', return_value=smc):
            result = Centauri.reset_key_state(self.reset_driver(), output_level=False)
        self.assertEqual(smc.smcep.rw32.call_args_list,
                         [unittest.mock.call('gTST', SMC_GPIO_INFO),
                          unittest.mock.call('gTST', SMC_GPIO_OUTPUT)])
        smc.smcep.write32.assert_called_once_with('gTST', SMC_GPIO_OUTPUT)
        smc.stop.assert_called_once()
        self.assertEqual(result['value'], '0x0')

    def test_non_gpio_reset_key_is_not_written(self):
        smc = Mock()
        smc.smcep.get_key_info.return_value = (4, 'ui32', 0)
        smc.smcep.rw32.return_value = 0
        with patch('m1n1.fw.smc.SMCClient', return_value=smc):
            with self.assertRaisesRegex(ValueError, 'GPIO'):
                Centauri.reset_key_state(self.reset_driver(), output_level=False)
        smc.smcep.write32.assert_not_called()
        smc.stop.assert_called_once()


if __name__ == '__main__':
    unittest.main()
