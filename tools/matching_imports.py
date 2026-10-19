"""Generate VC6 x86 link metadata without copying SDK libraries."""

from pathlib import Path
import os
import re
import struct


def vc6_exception_abi_object():
    """Define VC6's absolute FS:[0] offset, with no code, data or sections."""
    name = b'__except_list\0'
    header = struct.pack('<HHIIIHH', 0x14c, 0, 0, 20, 1, 0, 0)
    symbol = struct.pack('<IIIhHBB', 0, 4, 0, -1, 0, 2, 0)
    strings = struct.pack('<I', 4 + len(name)) + name
    return header + symbol + strings


def coff_import_symbols(path):
    """Read undefined import symbols from an x86 COFF object, not its strings."""
    data = Path(path).read_bytes()
    if len(data) < 20 or struct.unpack_from('<H', data)[0] != 0x14c:
        raise ValueError('Expected an x86 COFF object')
    offset, count = struct.unpack_from('<II', data, 8)
    strings = offset + count * 18
    if offset < 20 or strings + 4 > len(data):
        raise ValueError('Truncated COFF symbol table')
    string_size, = struct.unpack_from('<I', data, strings)
    if string_size < 4 or strings + string_size > len(data):
        raise ValueError('Truncated COFF string table')
    result = []
    index = 0
    while index < count:
        entry = offset + index * 18
        auxiliary = data[entry + 17]
        if index + 1 + auxiliary > count:
            raise ValueError('Truncated COFF auxiliary records')
        section, = struct.unpack_from('<h', data, entry + 12)
        if section == 0 and data[entry + 16] == 2:
            name = data[entry:entry + 8]
            if name[:4] == b'\0' * 4:
                name_offset, = struct.unpack_from('<I', name, 4)
                if not 4 <= name_offset < string_size:
                    raise ValueError('Invalid COFF symbol name offset')
                name_end = data.find(b'\0', strings + name_offset, strings + string_size)
                if name_end == -1:
                    raise ValueError('Unterminated COFF symbol name')
                name = data[strings + name_offset:name_end]
            else:
                name = name.rstrip(b'\0')
            name = name.decode('ascii')
            if name.startswith('__imp_'):
                result.append(name)
        index += 1 + auxiliary
    return sorted(result)


def validate_stdcall_imports(dll, symbols, export_names='undecorated'):
    """Validate import identities before writing files or invoking the librarian."""
    if not isinstance(dll, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*\.dll", dll):
        raise ValueError('Expected a DLL basename.')
    if not isinstance(symbols, dict) or not symbols:
        raise ValueError('Expected at least one stdcall import.')
    if export_names not in ('undecorated', 'decorated'):
        raise ValueError('Expected undecorated or decorated export names.')
    for name, stack_bytes in symbols.items():
        runtime_throw = (dll == 'MSVCRT.dll' and name == '_CxxThrowException' and
                         type(stack_bytes) is int and stack_bytes == 8 and
                         export_names == 'undecorated')
        if (not isinstance(name, str) or
                (not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name) and not runtime_throw) or
                type(stack_bytes) is not int or not 0 <= stack_bytes <= 65532 or stack_bytes % 4):
            raise ValueError('Expected an undecorated source name and x86 stack byte count.')


def stdcall_export_names(symbols, export_names):
    """Return PE names, keeping source/COFF decoration separate from DLL exports."""
    if export_names == 'decorated':
        return {f'_{name}@{count}' for name, count in symbols.items()}
    if export_names == 'undecorated':
        return set(symbols)
    raise ValueError('Expected undecorated or decorated export names.')


def rewrite_stdcall_import_archive(path, dll, symbols, export_names='undecorated'):
    """Change only LIB.EXE's short-import name mode; public symbols stay intact."""
    validate_stdcall_imports(dll, symbols, export_names)
    data = bytearray(Path(path).read_bytes())
    if data[:8] != b"!<arch>\n":
        raise ValueError("Expected a COFF archive")
    expected = {f"_{name}@{stack_bytes}" for name, stack_bytes in symbols.items()}
    seen = set()
    changed = []
    offset = 8
    while offset < len(data):
        if offset + 60 > len(data) or data[offset + 58:offset + 60] != b"`\n":
            raise ValueError("Incomplete COFF archive member")
        size_text = bytes(data[offset + 48:offset + 58]).strip()
        if not size_text.isdigit():
            raise ValueError("Invalid COFF archive member size")
        size = int(size_text)
        begin, end = offset + 60, offset + 60 + size
        if end > len(data):
            raise ValueError("COFF archive member extends past archive")
        member = bytes(data[begin:end])
        if member[:4] == b"\0\0\xff\xff":
            if len(member) < 20:
                raise ValueError("Incomplete short import object")
            sig1, sig2, version, machine, timestamp, payload_size, hint, flags = struct.unpack_from("<HHHHIIHH", member)
            if version != 0 or machine != 0x14c or payload_size != len(member) - 20 or flags != (2 << 2):
                raise ValueError("Unexpected LIB.EXE short import object")
            parts = member[20:].split(b"\0")
            if len(parts) != 3 or parts[2] != b"":
                raise ValueError("Unexpected short import object strings")
            symbol, imported_dll = (part.decode("ascii") for part in parts[:2])
            if imported_dll != dll or symbol not in expected or symbol in seen:
                raise ValueError("Unexpected or duplicate stdcall import identity")
            seen.add(symbol)
            # NAME=1 keeps _name@N; UNDECORATE=3 produces name. LIB emits
            # NO_PREFIX=2, which would wrongly retain @N for Win32 or lose
            # the leading underscore for a decorated DLL export.
            mode = 1 if export_names == 'decorated' else 3
            struct.pack_into("<H", data, begin + 18, mode << 2)
            changed.append({"symbol": symbol, "archive_byte_offset": begin + 18,
                            "before": 2 << 2, "after": mode << 2})
        offset = end + (size & 1)
        if offset > len(data):
            raise ValueError("Missing COFF archive member padding")
    if seen != expected:
        raise ValueError("Missing stdcall short import objects")
    Path(path).write_bytes(data)
    return changed


def undecorate_import_archive(path, dll, symbols):
    """Preserve the existing Win32 import-name policy."""
    return rewrite_stdcall_import_archive(path, dll, symbols)


def stdcall_import_library(tool, folder, dll, symbols, export_names='undecorated'):
    """Return a library path and the exact name-mode adjustments made to it."""
    validate_stdcall_imports(dll, symbols, export_names)
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    stem = dll[:-4].lower()
    definition = folder / (stem + ".def")
    definition.write_text("LIBRARY " + dll + "\nEXPORTS\n" + "".join(
        f"    {name}@{count}\n" for name, count in symbols.items()))
    library = folder / (stem + "-import.lib")
    arguments = ["/nologo", "/machine:IX86", "/def:" + definition.name,
                 "/out:" + library.name]
    response = folder / (stem + ".rsp")
    tool.response(response, arguments)
    command = [str(tool.compiler / "LIB.EXE"), "@" + response.name]
    log = tool.run(command if os.name == "nt" else ["wine", *command], folder)
    (folder / (stem + "-librarian.log")).write_text(log)
    changed = rewrite_stdcall_import_archive(library, dll, symbols, export_names)
    return library, changed
