"""Pack a standard LZ4 frame for U-Boot using the builder's liblz4."""
import ctypes as C
import os
from pathlib import Path

class FrameInfo(C.Structure):
    _fields_ = [
        ("block_size", C.c_int), ("block_mode", C.c_int),
        ("content_checksum", C.c_int), ("frame_type", C.c_int),
        ("content_size", C.c_ulonglong), ("dictionary_id", C.c_uint),
        ("block_checksum", C.c_int),
    ]


class Preferences(C.Structure):
    _fields_ = [
        ("frame_info", FrameInfo), ("compression_level", C.c_int),
        ("auto_flush", C.c_uint), ("favor_decode_speed", C.c_uint),
        ("reserved", C.c_uint * 3),
    ]


# U-Boot's frame decoder requires independent blocks.
preferences = Preferences()
preferences.frame_info.block_mode = 1
block_kib = int(os.environ.get("MP1_LZ4_BLOCK_KIB", "0"))
block_ids = {0: 0, 64: 4, 256: 5, 1024: 6, 4096: 7}
if block_kib not in block_ids:
    raise SystemExit("LZ4 block size must be 0, 64, 256, 1024 or 4096 KiB")
preferences.frame_info.block_size = block_ids[block_kib]
preferences.compression_level = int(os.environ.get("MP1_LZ4_LEVEL", "0"))
favor_decode_speed = int(os.environ.get("MP1_LZ4_FAVOR_DECODE_SPEED", "0"))
if favor_decode_speed not in (0, 1):
    raise SystemExit("LZ4 favor-decode-speed must be 0 or 1")
preferences.favor_decode_speed = favor_decode_speed
options = C.byref(preferences)
lz4 = C.CDLL("liblz4.so.1")
lz4.LZ4F_compressFrameBound.argtypes = (C.c_size_t, C.c_void_p)
lz4.LZ4F_compressFrameBound.restype = C.c_size_t
lz4.LZ4F_compressFrame.argtypes = (C.c_void_p, C.c_size_t, C.c_char_p, C.c_size_t, C.c_void_p)
lz4.LZ4F_compressFrame.restype = C.c_size_t
lz4.LZ4F_isError.argtypes = (C.c_size_t,)
lz4.LZ4F_isError.restype = C.c_uint
raw = Path("Image").read_bytes()
output = C.create_string_buffer(lz4.LZ4F_compressFrameBound(len(raw), options))
size = lz4.LZ4F_compressFrame(output, len(output), raw, len(raw), options)
if lz4.LZ4F_isError(size):
    raise SystemExit("LZ4 compression failed")
frame = output.raw[:size]
if frame[:4] != b"\x04\x22\x4d\x18" or frame[4] & 0x20 == 0:
    raise SystemExit("U-Boot requires a standard LZ4 frame with independent blocks")
Path("Image.lz4").write_bytes(frame)
