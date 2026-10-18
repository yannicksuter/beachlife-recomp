"""Generated ISO and InstallShield 6 payloads; no original content."""
import hashlib
from pathlib import PurePosixPath
import struct
import zlib

from check_iso import SECTOR, both, fixture, record

DESCRIPTOR = 64
TABLE = DESCRIPTOR + 0x600
RECORDS = TABLE + 0x100


def screen_tga():
    """A generated 4x2 indexed RLE image, stored bottom-up like the original screens."""
    header = struct.pack('<BBBHHBHHHHBB', 0, 1, 9, 0, 4, 24, 0, 0, 4, 2, 8, 0)
    palette = bytes((0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255))
    # Top row: red, green, blue, white. Bottom row: white, blue, green, red.
    return header + palette + bytes((7, 3, 2, 1, 0, 0, 1, 2, 3))


def content_files(*, screen=False, configuration=None):
    assets = [('BeachLife.ini', b'[Fixture]\nLanguage=English\n', False),
              ('Data/Script/sample.pis', b'generated fixture\n', False),
              ('Data/Sprites/test.dat', bytes(range(256))*300, False),
              ('Data/empty.bin', b'', False),
              ('Data/loose.txt', b'loose file\x00\xff', True),
              ('Setup.exe', b'not game content', False)]
    if screen:
        assets[1] = ('Data/Screens/FrontScreen.tga', screen_tga(), False)
    if configuration is not None:
        # Keep every compressed game entry contiguous before the loose/support
        # groups. Existing callers without configuration keep identical bytes.
        assets.insert(4, ('Data/Script/GameParameters.pis', bytes(configuration), False))
    header = bytearray(16384)
    struct.pack_into('<5I', header, 0, 0x28635349, 0x01006000, 0, DESCRIPTOR, 0x600)
    directories = list(dict.fromkeys(str(PurePosixPath(path).parent).replace('.', '') for path, _, _ in assets))
    for offset, value in [(0xc, 0x600), (0x14, len(header)-TABLE), (0x18, len(header)-TABLE),
                          (0x1c, len(directories)), (0x28, len(assets)), (0x2c, 0x100), (0x3e, 0x280)]:
        struct.pack_into('<I', header, DESCRIPTOR+offset, value)
    text_position = TABLE+0x800

    def text(value):
        nonlocal text_position
        position = text_position
        body = value.encode('ascii')+b'\0'
        header[position:position+len(body)] = body
        text_position += len(body)
        return position

    for i, name in enumerate(directories):
        struct.pack_into('<I', header, TABLE+i*4, text(name)-TABLE)
    loose_index = len(assets) - 2
    groups = [('Ess_English', 0, 0), ('UK Comp', 1, loose_index-1),
              ('UK Uncomp', loose_index, loose_index), ('<Support>Files', loose_index+1, loose_index+1)]
    for i, (name, first, last) in enumerate(groups):
        node = 0x280+i*0x40
        descriptor = node+12
        name_offset = text(name)-DESCRIPTOR
        struct.pack_into('<3I', header, DESCRIPTOR+node, name_offset, descriptor,
                         node+0x40 if i+1 < len(groups) else 0)
        struct.pack_into('<I', header, DESCRIPTOR+descriptor, name_offset)
        struct.pack_into('<2I', header, DESCRIPTOR+descriptor+0x16, first, last)
    cabinet = bytearray(512)
    struct.pack_into('<5I', cabinet, 0, 0x28635349, 0x01006000, 0, 0, 0)
    struct.pack_into('<QII', cabinet, 20, 512, 0, len(assets)-1)
    loose = {}
    for i, (path, body, external) in enumerate(assets):
        name = PurePosixPath(path)
        compressed = bytearray()
        for start in range(0, len(body), 32768):
            codec = zlib.compressobj(wbits=-15)
            chunk = codec.compress(body[start:start+32768])+codec.flush()
            compressed += struct.pack('<H', len(chunk))+chunk
        flags = 0 if external or not body else 4
        offset = 0 if external else len(cabinet)
        payload = body if external else compressed
        start = RECORDS+i*0x57
        struct.pack_into('<HQQQ', header, start, flags, len(body), len(payload), offset)
        header[start+26:start+42] = hashlib.md5(body).digest()
        struct.pack_into('<IH', header, start+0x3a, text(name.name)-TABLE,
                         directories.index(str(name.parent).replace('.', '')))
        struct.pack_into('<H', header, start+0x55, 2)
        if external:
            loose[path] = body
        else:
            cabinet += payload
    return {'data1.hdr': header, 'data2.cab': cabinet, **loose}, dict((p, b) for p, b, _ in assets[:-1])


def iso_files(files):
    image = fixture()
    for name, body in files.items():
        parts = name.split('/')
        assert len(parts) == 1 or len(parts) == 2 and parts[0] == 'Data'
        block = len(image)//SECTOR
        image += bytes(((len(body)+SECTOR-1)//SECTOR)*SECTOR)
        image[block*SECTOR:block*SECTOR+len(body)] = body
        directory = 22 if len(parts) == 1 else 23
        position = directory*SECTOR
        while image[position]: position += image[position]
        item = record((parts[-1]+';1').encode('utf-16-be'), block, len(body))
        assert position+len(item) < (directory+1)*SECTOR
        image[position:position+len(item)] = item
    for block in (16, 17):
        image[block*SECTOR+80:block*SECTOR+88] = both(len(image)//SECTOR, 4)
    return image
