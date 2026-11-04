"""Small host-side checks for the A7M4 runtime. No firmware or QEMU is started."""
import sys
import io
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import a7m4_import
import a7m4_qemu
import a7m4_session


def test_package_identity_is_pinned():
    assert a7m4_import.MODEL == 0x1030081
    assert a7m4_import.REGION == 0
    assert set(a7m4_import.VERSIONS) >= {'6.00', '6.02'}


def test_session_uses_original_init_order_and_firmware_entry():
    commands = a7m4_session.setup_commands()
    assert commands[0].startswith('insmod /kmod/devno.ko')
    assert any('/kmod/dma330.ko phy_inst_area=0x4102E4000' in c for c in commands)
    assert any('/usr/kmod/dmm.ko DmmConfig=/setting/DmmCfg.bin' in c for c in commands)
    assert "bootin.elf 'target=qemu '" in a7m4_session.APP_COMMAND


def test_cmdline_forwards_memrsv_but_not_mem_4g(tmp_path):
    kemco = tmp_path / 'kemco.txt'
    kemco.write_text('kemco\nmem=4G\ncfg.uart=1\n'
                     'memrsv=0x400000@0x402000000\n'
                     'memrsv=0x00001000@0x400000000\n*\n')
    line = a7m4_qemu.build_cmdline(kemco, 4096)
    assert 'mem=4G' not in line
    assert 'cfg.uart=1' not in line
    assert 'memrsv=0x400000@0x402000000' in line
    assert 'memrsv=0x00001000@0x400000000' in line


@pytest.mark.parametrize('version, expected', [('6.00', b'\x00\x06'),
                                               ('6.01', b'\x01\x06'),
                                               ('6.02', b'\x02\x06')])
def test_version_record_matches_original_bcd_reader(version, expected):
    assert a7m4_import.version_record(version) == expected
    minor, major = a7m4_import.version_record(version)
    assert '%d.%02d' % ((major >> 4) * 10 + (major & 15),
                        (minor >> 4) * 10 + (minor & 15)) == version


def test_version_record_rejects_unverified_versions():
    for version in ('6.03', '0.00', '6.2', '', '../6.02'):
        with pytest.raises(ValueError):
            a7m4_import.version_record(version)


def test_original_bitmap_png_channels_and_length():
    from experimental.bitmap_preview import bitmap_png
    from PIL import Image
    raw = bytearray(614400)
    raw[:8] = bytes.fromhex('0ff00f0fff000000')
    image = Image.open(io.BytesIO(bitmap_png(raw)))
    assert image.size == (640, 480) and image.mode == 'RGBA'
    assert [image.getpixel((x, 0)) for x in range(4)] == [
        (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (0, 0, 0, 0)]
    with pytest.raises(ValueError):
        bitmap_png(raw[:-1])


def test_official_source_manifest_is_complete():
    root = Path(__file__).resolve().parents[2] / 'source'
    sums = root / 'upstream/SOURCE-SHA256SUMS'
    if not sums.is_file():
        pytest.skip('downloaded source tree is not present')
    lines = [line for line in sums.read_text().splitlines() if line.strip()]
    assert len(lines) == 41  # 40 packages + urls.txt
    assert any('linux-kernel.tar.gz' in line for line in lines)
    missing = [line.split('  ', 1)[1] for line in lines
               if not (root / 'upstream' / line.split('  ', 1)[1]).is_file()]
    assert not missing, missing[:3]
