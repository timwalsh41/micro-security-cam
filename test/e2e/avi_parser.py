"""Minimal AVI (RIFF) structure parser for the e2e suite.

Validates completed clips downloaded from the device: RIFF/AVI
envelope, video frame count, and (for mic-recording coverage) the
audio stream format. Pure byte parsing, no dependencies.
"""
import struct

# Chunk four-CCs we care about.
FOURCC_RIFF = b"RIFF"
FOURCC_AVI = b"AVI "
FOURCC_LIST = b"LIST"
FOURCC_HDRL = b"hdrl"
FOURCC_MOVI = b"movi"
FOURCC_STRL = b"strl"
FOURCC_STRH = b"strh"
FOURCC_STRF = b"strf"
FOURCC_VIDS = b"vids"
FOURCC_AUDS = b"auds"
FOURCC_VIDEO_FRAME = b"00dc"


class AviFormatError(Exception):
    """The downloaded clip is not a structurally valid AVI file."""


def _u32(data, offset):
    """Read a little-endian uint32."""
    if offset + 4 > len(data):
        raise AviFormatError(f"truncated u32 at offset {offset}")
    return struct.unpack_from("<I", data, offset)[0]


def _u16(data, offset):
    """Read a little-endian uint16."""
    if offset + 2 > len(data):
        raise AviFormatError(f"truncated u16 at offset {offset}")
    return struct.unpack_from("<H", data, offset)[0]


def parse_avi(data):
    """Parse a completed AVI clip.

    Returns a dict with:
      ``riff_size``  declared RIFF payload size (file size - 8)
      ``frames``     number of ``00dc`` video frame chunks in ``movi``
      ``audio``      dict with ``channels``, ``rate``, ``bits`` for the
                     audio stream, or None when the clip has no audio
    Raises AviFormatError on structural problems.
    """
    if len(data) < 12:
        raise AviFormatError("file too small for a RIFF header")
    if data[0:4] != FOURCC_RIFF:
        raise AviFormatError("missing RIFF four-CC")
    riff_size = _u32(data, 4)
    if data[8:12] != FOURCC_AVI:
        raise AviFormatError("missing AVI form type")

    result = {
        "riff_size": riff_size,
        "frames": 0,
        "audio": None,
    }

    _walk_list(data, 12, 8 + riff_size, result)
    return result


def _walk_list(data, start, end, result, list_type=None):
    """Walk chunk records in ``data[start:end]``.

    ``list_type`` is the type of the enclosing LIST (or None at the
    top level); recursion happens into LIST bodies.
    """
    pos = start
    while pos + 8 <= end:
        fourcc = data[pos:pos + 4]
        size = _u32(data, pos + 4)
        body_start = pos + 8
        body_end = min(body_start + size, end)
        if body_end < body_start:
            raise AviFormatError(f"bad chunk size at {pos}")

        if fourcc == FOURCC_LIST:
            if body_end - body_start < 4:
                raise AviFormatError("LIST chunk without a type")
            inner_type = data[body_start:body_start + 4]
            _walk_list(data, body_start + 4, body_end, result,
                       list_type=inner_type.decode("latin-1"))
        elif list_type == "movi" and fourcc == FOURCC_VIDEO_FRAME:
            result["frames"] += 1
        elif list_type == "strl" and fourcc == FOURCC_STRH:
            if body_start + 4 <= body_end and \
                    data[body_start:body_start + 4] == FOURCC_AUDS:
                # The audio format follows in this stream's strf chunk;
                # scan the remainder of this strl list for it.
                audio = _find_audio_strf(data, body_end, end)
                if audio is not None:
                    result["audio"] = audio

        pos = body_start + size + (size & 1)


def _find_audio_strf(data, start, end):
    """Find the strf WAVEFORMATEX chunk after an auds strh, if present."""
    pos = start
    while pos + 8 <= end:
        fourcc = data[pos:pos + 4]
        size = _u32(data, pos + 4)
        body_start = pos + 8
        body_end = min(body_start + size, end)
        if fourcc == FOURCC_STRF:
            if body_end - body_start < 16:
                raise AviFormatError("audio strf chunk too small")
            return {
                "channels": _u16(data, body_start + 2),
                "rate": _u32(data, body_start + 4),
                "bits": _u16(data, body_start + 14),
            }
        if fourcc == FOURCC_LIST:
            return None  # next stream started; auds stream had no strf
        pos = body_start + size + (size & 1)
    return None
