# SPDX-License-Identifier: Apache-2.0
"""Lecture/ecriture PNG en bibliotheque standard (aucune dependance numpy/OpenCV/Pillow).

Lecture : 8 bits, niveaux de gris / RGB / RGBA / gris+alpha, non entrelace.
Ecriture : deterministe (compression fixe), octet pour octet reproductible.
"""
from __future__ import annotations

import struct
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
_CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}


class PngError(ValueError):
    pass


def _chunk(name: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + name + payload + struct.pack(">I", zlib.crc32(name + payload))


def encode(width: int, height: int, mode: str, pixels: bytes) -> bytes:
    """`mode` : "L" (gris) ou "RGB" ; `pixels` : lignes contigues sans remplissage."""
    channels = {"L": 1, "RGB": 3}[mode]
    if width <= 0 or height <= 0 or len(pixels) != width * height * channels:
        raise PngError("dimensions incoherentes avec le tampon de pixels")
    color_type = 0 if channels == 1 else 2
    stride = width * channels
    raw = b"".join(b"\0" + pixels[y * stride : (y + 1) * stride] for y in range(height))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    return SIGNATURE + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b"")


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode(data: bytes) -> tuple[int, int, int, bytes]:
    """Retourne (largeur, hauteur, canaux, pixels) pour un PNG 8 bits non entrelace."""
    if data[:8] != SIGNATURE:
        raise PngError("signature PNG invalide")
    pos, idat, header = 8, bytearray(), None
    while pos + 8 <= len(data):
        length, name = struct.unpack(">I4s", data[pos : pos + 8])
        payload = data[pos + 8 : pos + 8 + length]
        if len(payload) != length:
            raise PngError("fragment PNG tronque")
        if name == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif name == b"IDAT":
            idat += payload
        elif name == b"IEND":
            break
        pos += 12 + length
    if header is None:
        raise PngError("IHDR absent")
    width, height, depth, color_type, _, _, interlace = header
    if depth != 8 or interlace != 0 or color_type not in _CHANNELS:
        raise PngError("seuls les PNG 8 bits non entrelaces gris/RGB/RGBA sont lus")
    channels = _CHANNELS[color_type]
    stride = width * channels
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as exc:
        raise PngError(f"flux IDAT illisible : {exc}") from exc
    if len(raw) != (stride + 1) * height:
        raise PngError("taille de donnees inattendue")
    out = bytearray(stride * height)
    prev = bytearray(stride)
    for y in range(height):
        ftype = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        if ftype == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ftype == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ftype == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                upleft = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(left, prev[i], upleft)) & 255
        elif ftype != 0:
            raise PngError(f"filtre PNG inconnu : {ftype}")
        out[y * stride : (y + 1) * stride] = line
        prev = line
    return width, height, channels, bytes(out)


def to_gray(width: int, height: int, channels: int, pixels: bytes) -> bytes:
    """Reduit a un plan : gris tel quel ; RGB(A) -> maximum des canaux couleur."""
    if channels == 1:
        return pixels
    if channels == 2:
        return pixels[0::2]
    step = channels
    return bytes(max(pixels[i], pixels[i + 1], pixels[i + 2]) for i in range(0, len(pixels), step))
