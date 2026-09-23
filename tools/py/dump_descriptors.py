#!/usr/bin/env python3
"""Dump the thermal camera's USB and UVC descriptors (docs/PLAN.md, M0 step 2).

Read-only by design (CLAUDE.md rule 1): it uses the descriptors libusb cached
at enumeration plus standard GET_DESCRIPTOR(STRING) reads. It never sets a
configuration, claims an interface, or sends a class or vendor request.

    tools/py/.venv/bin/python tools/py/dump_descriptors.py [--vid 0x1514] [--pid 0x0001]
"""

import argparse
import ctypes.util
import os
import struct
import sys
import uuid

import usb.backend.libusb1
import usb.core
import usb.util

EXPECT_W, EXPECT_H, EXPECT_FPS = 256, 196, 25
HS_MICROFRAMES_PER_S = 8000

DESC_NAMES = {1: "DEVICE", 2: "CONFIGURATION", 3: "STRING", 4: "INTERFACE", 5: "ENDPOINT",
              6: "DEVICE_QUALIFIER", 7: "OTHER_SPEED_CONFIGURATION", 8: "INTERFACE_POWER",
              11: "INTERFACE_ASSOCIATION", 0x24: "CS_INTERFACE", 0x25: "CS_ENDPOINT"}
SPEEDS = {0: "unknown", 1: "low (1.5 Mb/s)", 2: "full (12 Mb/s)", 3: "high (480 Mb/s)",
          4: "super (5 Gb/s)", 5: "super+ (10 Gb/s)"}

# UVC 1.5, table 3-6 (camera terminal bmControls) and table 3-8 (processing unit bmControls).
CT_CONTROLS = ["Scanning Mode", "Auto-Exposure Mode", "Auto-Exposure Priority",
               "Exposure Time (Absolute)", "Exposure Time (Relative)", "Focus (Absolute)",
               "Focus (Relative)", "Iris (Absolute)", "Iris (Relative)", "Zoom (Absolute)",
               "Zoom (Relative)", "PanTilt (Absolute)", "PanTilt (Relative)", "Roll (Absolute)",
               "Roll (Relative)", "Reserved", "Reserved", "Focus, Auto", "Privacy",
               "Focus, Simple", "Window", "Region of Interest"]
PU_CONTROLS = ["Brightness", "Contrast", "Hue", "Saturation", "Sharpness", "Gamma",
               "White Balance Temperature", "White Balance Component", "Backlight Compensation",
               "Gain", "Power Line Frequency", "Hue, Auto", "White Balance Temperature, Auto",
               "White Balance Component, Auto", "Digital Multiplier", "Digital Multiplier Limit",
               "Analog Video Standard", "Analog Video Lock Status", "Contrast, Auto"]
TERMINAL_TYPES = {0x0100: "TT_VENDOR_SPECIFIC", 0x0101: "TT_STREAMING",
                  0x0200: "ITT_VENDOR_SPECIFIC", 0x0201: "ITT_CAMERA",
                  0x0202: "ITT_MEDIA_TRANSPORT_INPUT", 0x0300: "OTT_VENDOR_SPECIFIC",
                  0x0301: "OTT_DISPLAY", 0x0302: "OTT_MEDIA_TRANSPORT_OUTPUT"}
# GUIDs libuvc's UVC_FRAME_FORMAT_ANY recognizes, plus Y16.
KNOWN_GUIDS = {"32595559-0000-0010-8000-00aa00389b71": "YUY2 (YUYV 4:2:2)",
               "59565955-0000-0010-8000-00aa00389b71": "UYVY",
               "3231564e-0000-0010-8000-00aa00389b71": "NV12",
               "30303859-0000-0010-8000-00aa00389b71": "Y800 (GRAY8)",
               "20363159-0000-0010-8000-00aa00389b71": "Y16 (GRAY16)",
               "e436eb7d-524f-11ce-9f53-0020af0ba770": "RGB24 (BGR3)"}
XFER = ["control", "isochronous", "bulk", "interrupt"]
SYNC = ["none", "async", "adaptive", "sync"]
USAGE = ["data", "feedback", "implicit feedback", "reserved"]


def find_backend(explicit):
    candidates = [explicit, os.environ.get("LIBUSB_PATH"), ctypes.util.find_library("usb-1.0"),
                  "/opt/homebrew/lib/libusb-1.0.dylib", "/usr/local/lib/libusb-1.0.dylib"]
    for path in candidates:
        if not path:
            continue
        backend = usb.backend.libusb1.get_backend(find_library=lambda _name, p=path: p)
        if backend is not None:
            return backend, path
    sys.exit("libusb-1.0 not found: brew install libusb, or pass --libusb PATH")


def string(dev, index):
    """A standard string-descriptor read; None when the descriptor has no string."""
    if not index:
        return None
    try:
        return usb.util.get_string(dev, index)
    except (usb.core.USBError, ValueError, NotImplementedError) as e:
        return f"<unreadable: {e}>"


def named(dev, index):
    s = string(dev, index)
    return f"{index}" + (f' "{s}"' if s is not None else "")


def split_descriptors(blob):
    blob = bytes(blob)
    i = 0
    while i < len(blob):
        n = blob[i]
        if n < 2 or i + n > len(blob):
            yield blob[i:]  # malformed tail, printed as hex
            return
        yield blob[i:i + n]
        i += n


def hexbytes(b):
    return " ".join(f"{x:02x}" for x in b)


def guid_text(raw):
    g = str(uuid.UUID(bytes_le=bytes(raw)))
    fourcc = "".join(chr(c) if 32 <= c < 127 else "." for c in raw[:4])
    return f"{g} (FourCC '{fourcc}'; {KNOWN_GUIDS.get(g, 'not a GUID libuvc knows')})"


def bits(value, names):
    return [names[i] if i < len(names) else f"bit {i}" for i in range(value.bit_length())
            if value >> i & 1]


def intervals_text(kind, values):
    if kind == 0:
        lo, hi, step = values
        return (f"continuous {lo}..{hi} step {step} (100 ns units) = "
                f"{1e7 / hi:.2f}..{1e7 / lo:.2f} fps")
    return ", ".join(f"{v} ({1e7 / v:.2f} fps)" for v in values)


class Report:
    def __init__(self):
        self.lines = []
        self.frames = []        # (format, bpp, width, height, interval kind, intervals)
        self.streaming_eps = []  # (interface, alt, address, xfer, bytes per microframe, bInterval)
        self.ct_controls = None

    def out(self, depth, text):
        self.lines.append("  " * depth + text)


def parse_vc(rep, dev, d, depth):
    sub = d[2]
    if sub == 0x01 and len(d) >= 12:
        bcd, total, clock, n = struct.unpack_from("<HHIB", d, 3)
        rep.out(depth, f"VC_HEADER: bcdUVC {bcd >> 8}.{bcd & 0xff:02x}, wTotalLength {total}, "
                       f"dwClockFrequency {clock} Hz, streaming interfaces {list(d[12:12 + n])}")
    elif sub == 0x02 and len(d) >= 8:
        tid, ttype, assoc, iterm = struct.unpack_from("<BHBB", d, 3)
        rep.out(depth, f"INPUT_TERMINAL id {tid}: {TERMINAL_TYPES.get(ttype, hex(ttype))}, "
                       f"bAssocTerminal {assoc}, iTerminal {named(dev, iterm)}")
        if ttype == 0x0201 and len(d) >= 15:
            fmin, fmax, ocular, csize = struct.unpack_from("<HHHB", d, 8)
            bm = int.from_bytes(d[15:15 + csize], "little")
            rep.ct_controls = bits(bm, CT_CONTROLS)
            rep.out(depth + 1, f"wObjectiveFocalLengthMin {fmin}, Max {fmax}, "
                               f"wOcularFocalLength {ocular}")
            rep.out(depth + 1, f"bmControls 0x{bm:0{csize * 2}x}: "
                               f"{', '.join(rep.ct_controls) or 'none'}")
    elif sub == 0x03 and len(d) >= 9:
        tid, ttype, assoc, src, iterm = struct.unpack_from("<BHBBB", d, 3)
        rep.out(depth, f"OUTPUT_TERMINAL id {tid}: {TERMINAL_TYPES.get(ttype, hex(ttype))}, "
                       f"bSourceID {src}, iTerminal {named(dev, iterm)}")
    elif sub == 0x04 and len(d) >= 6:
        uid, pins = d[3], d[4]
        rep.out(depth, f"SELECTOR_UNIT id {uid}: sources {list(d[5:5 + pins])}, "
                       f"iSelector {named(dev, d[5 + pins])}")
    elif sub == 0x05 and len(d) >= 8:
        uid, src, mult, csize = struct.unpack_from("<BBHB", d, 3)
        bm = int.from_bytes(d[8:8 + csize], "little")
        iproc = d[8 + csize] if len(d) > 8 + csize else 0
        rep.out(depth, f"PROCESSING_UNIT id {uid}: bSourceID {src}, wMaxMultiplier {mult}, "
                       f"iProcessing {named(dev, iproc)}")
        rep.out(depth + 1, f"bmControls 0x{bm:0{csize * 2}x}: {', '.join(bits(bm, PU_CONTROLS)) or 'none'}")
    elif sub == 0x06 and len(d) >= 24:
        uid = d[3]
        guid = str(uuid.UUID(bytes_le=bytes(d[4:20])))
        ncontrols, pins = d[20], d[21]
        csize = d[22 + pins]
        bm = int.from_bytes(d[23 + pins:23 + pins + csize], "little")
        iext = d[23 + pins + csize] if len(d) > 23 + pins + csize else 0
        rep.out(depth, f"EXTENSION_UNIT id {uid}: guid {guid}, bNumControls {ncontrols}, "
                       f"sources {list(d[22:22 + pins])}, bmControls 0x{bm:0{max(csize, 1) * 2}x}, "
                       f"iExtension {named(dev, iext)}")
    else:
        rep.out(depth, f"VC subtype 0x{sub:02x}: {hexbytes(d)}")


def parse_vs(rep, dev, d, depth, fmt):
    """Returns the current format label, which frame descriptors inherit."""
    sub = d[2]
    if sub == 0x01 and len(d) >= 13:
        nfmt, total, ep, info, link, still, trig, trig_use, csize = struct.unpack_from("<BHBBBBBBB", d, 3)
        rep.out(depth, f"VS_INPUT_HEADER: bNumFormats {nfmt}, wTotalLength {total}, "
                       f"bEndpointAddress 0x{ep:02x}, bmInfo 0x{info:02x}, bTerminalLink {link}, "
                       f"bStillCaptureMethod {still}, bTriggerSupport {trig}, bTriggerUsage {trig_use}")
    elif sub in (0x04, 0x10) and len(d) >= 27:
        index, nframes = d[3], d[4]
        bpp, default, ax, ay, interlace, protect = struct.unpack_from("<BBBBBB", d, 21)
        kind = "UNCOMPRESSED" if sub == 0x04 else "FRAME_BASED"
        rep.out(depth, f"VS_FORMAT_{kind} #{index}: {nframes} frame descriptor(s), "
                       f"bBitsPerPixel {bpp}, bDefaultFrameIndex {default}, aspect {ax}:{ay}, "
                       f"bmInterlaceFlags 0x{interlace:02x}, bCopyProtect {protect}")
        rep.out(depth + 1, f"guidFormat {guid_text(d[5:21])}")
        fmt = (f"format #{index} {KNOWN_GUIDS.get(str(uuid.UUID(bytes_le=bytes(d[5:21]))), 'unknown GUID')}", bpp)
    elif sub == 0x06 and len(d) >= 11:
        index, nframes, flags, default = d[3], d[4], d[5], d[6]
        rep.out(depth, f"VS_FORMAT_MJPEG #{index}: {nframes} frame descriptor(s), bmFlags 0x{flags:02x}, "
                       f"bDefaultFrameIndex {default}")
        fmt = (f"format #{index} MJPEG", None)
    elif sub in (0x05, 0x07) and len(d) >= 26:
        index, caps, w, h, rmin, rmax, bufsize, default, kind = struct.unpack_from("<BBHHIIIIB", d, 3)
        vals = [struct.unpack_from("<I", d, 26 + 4 * i)[0] for i in range(3 if kind == 0 else kind)]
        rep.out(depth, f"VS_FRAME #{index}: {w}×{h}, bmCapabilities 0x{caps:02x}, bit rate "
                       f"{rmin}..{rmax} b/s, dwMaxVideoFrameBufferSize {bufsize} "
                       f"(= {bufsize / max(w * h, 1):.2f} bytes/pixel)")
        rep.out(depth + 1, f"dwDefaultFrameInterval {default} ({1e7 / default:.2f} fps); "
                           f"intervals: {intervals_text(kind, vals)}")
        rep.frames.append((fmt[0] if fmt else "?", fmt[1] if fmt else None, w, h, kind, vals))
    elif sub == 0x11 and len(d) >= 26:
        index, caps, w, h, rmin, rmax, default, kind, bpl = struct.unpack_from("<BBHHIIIBI", d, 3)
        vals = [struct.unpack_from("<I", d, 26 + 4 * i)[0] for i in range(3 if kind == 0 else kind)]
        rep.out(depth, f"VS_FRAME_FRAME_BASED #{index}: {w}×{h}, dwBytesPerLine {bpl}, "
                       f"default interval {default}; intervals: {intervals_text(kind, vals)}")
        rep.frames.append((fmt[0] if fmt else "?", fmt[1] if fmt else None, w, h, kind, vals))
    elif sub == 0x03 and len(d) >= 5:
        ep, n = d[3], d[4]
        sizes = [struct.unpack_from("<HH", d, 5 + 4 * i) for i in range(n)]
        rep.out(depth, f"VS_STILL_IMAGE_FRAME: bEndpointAddress 0x{ep:02x}, sizes "
                       f"{', '.join(f'{w}×{h}' for w, h in sizes)}")
    elif sub == 0x0D and len(d) >= 6:
        rep.out(depth, f"VS_COLORFORMAT: bColorPrimaries {d[3]}, bTransferCharacteristics {d[4]}, "
                       f"bMatrixCoefficients {d[5]}")
    else:
        rep.out(depth, f"VS subtype 0x{sub:02x}: {hexbytes(d)}")
    return fmt


def parse_extra(rep, dev, blob, depth, subclass):
    fmt = None
    for d in split_descriptors(blob):
        if len(d) < 3:
            rep.out(depth, f"malformed: {hexbytes(d)}")
            continue
        dtype = d[1]
        if dtype == 0x0B and len(d) >= 8:
            rep.out(depth, f"INTERFACE_ASSOCIATION: interfaces {d[2]}..{d[2] + d[3] - 1}, "
                           f"function class 0x{d[4]:02x} subclass 0x{d[5]:02x} protocol 0x{d[6]:02x}, "
                           f"iFunction {named(dev, d[7])}")
        elif dtype == 0x24 and subclass == 1:
            parse_vc(rep, dev, d, depth)
        elif dtype == 0x24 and subclass == 2:
            fmt = parse_vs(rep, dev, d, depth, fmt)
        elif dtype == 0x25 and d[2] == 0x03 and len(d) >= 5:
            rep.out(depth, f"CS_ENDPOINT EP_INTERRUPT: wMaxTransferSize {struct.unpack_from('<H', d, 3)[0]}")
        else:
            rep.out(depth, f"{DESC_NAMES.get(dtype, f'type 0x{dtype:02x}')}: {hexbytes(d)}")


def endpoint(rep, ep, depth, intf):
    addr, attr = ep.bEndpointAddress, ep.bmAttributes
    xfer = XFER[attr & 3]
    size, extra = ep.wMaxPacketSize & 0x7FF, (ep.wMaxPacketSize >> 11) & 3
    text = (f"ENDPOINT 0x{addr:02x} {'IN' if addr & 0x80 else 'OUT'} {xfer}, "
            f"wMaxPacketSize 0x{ep.wMaxPacketSize:04x} = {size} bytes"
            + (f" × {extra + 1} per microframe" if extra else "") + f", bInterval {ep.bInterval}")
    if xfer == "isochronous":
        period = 2 ** (ep.bInterval - 1)
        rate = size * (extra + 1) * HS_MICROFRAMES_PER_S / period
        text += (f" (every {period} microframe(s)), sync {SYNC[(attr >> 2) & 3]}, "
                 f"usage {USAGE[(attr >> 4) & 3]} → {rate / 1e6:.3f} MB/s")
    rep.out(depth, text)
    if intf.bInterfaceClass == 0x0E and intf.bInterfaceSubClass == 2:
        rep.streaming_eps.append((intf.bInterfaceNumber, intf.bAlternateSetting, addr, xfer,
                                  size * (extra + 1), ep.bInterval))
    if ep.extra_descriptors:
        parse_extra(rep, None, ep.extra_descriptors, depth + 1, None)


def raw_configuration(cfg):
    """The configuration descriptor as the device sent it, rebuilt from libusb's cache."""
    out = bytearray(struct.pack("<BBHBBBBB", cfg.bLength, cfg.bDescriptorType, cfg.wTotalLength,
                                cfg.bNumInterfaces, cfg.bConfigurationValue, cfg.iConfiguration,
                                cfg.bmAttributes, cfg.bMaxPower))
    out += bytes(cfg.extra_descriptors)
    for intf in cfg:
        out += struct.pack("<BBBBBBBBB", intf.bLength, intf.bDescriptorType, intf.bInterfaceNumber,
                           intf.bAlternateSetting, intf.bNumEndpoints, intf.bInterfaceClass,
                           intf.bInterfaceSubClass, intf.bInterfaceProtocol, intf.iInterface)
        out += bytes(intf.extra_descriptors)
        for ep in intf:
            fields = struct.pack("<BBBBHB", ep.bLength, ep.bDescriptorType, ep.bEndpointAddress,
                                 ep.bmAttributes, ep.wMaxPacketSize, ep.bInterval)
            if ep.bLength == 9:
                fields += struct.pack("<BB", ep.bRefresh, ep.bSynchAddress)
            out += fields + bytes(ep.extra_descriptors)
    return bytes(out)


def summary(rep, dev):
    need = EXPECT_W * EXPECT_H * 2 * EXPECT_FPS
    lines = ["", "== Summary =="]
    widths = sorted({w for _, _, w, _, _, _ in rep.frames})
    lines.append(f"Frame sizes: {', '.join(f'{w}×{h} ({f})' for f, _, w, h, _, _ in rep.frames) or 'none'}")
    hit = [(f, w, h, k, v) for f, _, w, h, k, v in rep.frames if (w, h) == (EXPECT_W, EXPECT_H)]
    if EXPECT_W not in widths:
        lines.append(f"STOP: no frame is {EXPECT_W} wide — every constant in docs/PROTOCOL.md is "
                     f"width-specific. Tell the owner.")
    for f, w, h, k, v in hit:
        fps_ok = (k == 0 and v[0] <= 1e7 / EXPECT_FPS <= v[1]) or (k and int(1e7 / EXPECT_FPS) in v)
        lines.append(f"Expected {EXPECT_W}×{EXPECT_H} @ {EXPECT_FPS} fps: {'FOUND' if fps_ok else 'size found, but no 25 fps interval'} ({f})")
    if not hit:
        lines.append(f"Expected {EXPECT_W}×{EXPECT_H}: NOT FOUND")
    for intf, alt, addr, xfer, per_uframe, interval in rep.streaming_eps:
        if xfer == "isochronous":
            rate = per_uframe * HS_MICROFRAMES_PER_S / 2 ** (interval - 1)
            lines.append(f"Streaming interface {intf} alt {alt}: {xfer} EP 0x{addr:02x}, "
                         f"{per_uframe} bytes/microframe = {rate / 1e6:.3f} MB/s "
                         f"({'enough' if rate > need * 1.05 else 'NOT enough'} for "
                         f"{EXPECT_W}×{EXPECT_H}×2 bytes @ {EXPECT_FPS} fps = {need / 1e6:.3f} MB/s)")
        else:
            lines.append(f"Streaming interface {intf} alt {alt}: {xfer} EP 0x{addr:02x}, "
                         f"wMaxPacketSize {per_uframe}")
    if rep.ct_controls is not None:
        zoom = "Zoom (Absolute)" in rep.ct_controls
        lines.append(f"Camera terminal advertises Zoom (Absolute), our command channel: {'yes' if zoom else 'NO'}")
    lines.append(f"Speed: {SPEEDS.get(dev.speed, dev.speed)}")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--vid", type=lambda s: int(s, 0), default=0x1514)
    ap.add_argument("--pid", type=lambda s: int(s, 0), default=0x0001)
    ap.add_argument("--libusb", help="path to libusb-1.0.dylib")
    args = ap.parse_args()

    backend, libpath = find_backend(args.libusb)
    devices = list(usb.core.find(find_all=True, idVendor=args.vid, idProduct=args.pid, backend=backend))
    if not devices:
        sys.exit(f"no device {args.vid:04x}:{args.pid:04x} found")
    if len(devices) > 1:
        sys.exit(f"{len(devices)} devices match {args.vid:04x}:{args.pid:04x}; unplug the extras")
    dev = devices[0]

    rep = Report()
    rep.out(0, f"libusb: {libpath}")
    rep.out(0, f"Bus {dev.bus} address {dev.address} port path {dev.port_numbers}, "
               f"speed {SPEEDS.get(dev.speed, dev.speed)}")
    rep.out(0, f"DEVICE: bcdUSB 0x{dev.bcdUSB:04x}, class 0x{dev.bDeviceClass:02x} "
               f"subclass 0x{dev.bDeviceSubClass:02x} protocol 0x{dev.bDeviceProtocol:02x}, "
               f"bMaxPacketSize0 {dev.bMaxPacketSize0}")
    rep.out(1, f"idVendor 0x{dev.idVendor:04x}, idProduct 0x{dev.idProduct:04x}, "
               f"bcdDevice 0x{dev.bcdDevice:04x}, bNumConfigurations {dev.bNumConfigurations}")
    rep.out(1, f"iManufacturer {named(dev, dev.iManufacturer)}, iProduct {named(dev, dev.iProduct)}, "
               f"iSerialNumber {named(dev, dev.iSerialNumber)}")

    raws = []
    for cfg in dev:
        rep.out(0, f"CONFIGURATION {cfg.bConfigurationValue}: wTotalLength {cfg.wTotalLength}, "
                   f"bNumInterfaces {cfg.bNumInterfaces}, bmAttributes 0x{cfg.bmAttributes:02x}, "
                   f"MaxPower {cfg.bMaxPower * 2} mA, iConfiguration {named(dev, cfg.iConfiguration)}")
        parse_extra(rep, dev, cfg.extra_descriptors, 1, None)
        for intf in cfg:
            rep.out(1, f"INTERFACE {intf.bInterfaceNumber} alt {intf.bAlternateSetting}: "
                       f"class 0x{intf.bInterfaceClass:02x} subclass 0x{intf.bInterfaceSubClass:02x} "
                       f"protocol 0x{intf.bInterfaceProtocol:02x}, {intf.bNumEndpoints} endpoint(s), "
                       f"iInterface {named(dev, intf.iInterface)}")
            subclass = intf.bInterfaceSubClass if intf.bInterfaceClass == 0x0E else None
            parse_extra(rep, dev, intf.extra_descriptors, 2, subclass)
            for ep in intf:
                endpoint(rep, ep, 2, intf)
        raws.append((cfg.bConfigurationValue, cfg.wTotalLength, raw_configuration(cfg)))
    usb.util.dispose_resources(dev)

    print("\n".join(rep.lines))
    print("\n".join(summary(rep, dev)))
    for value, total, raw in raws:
        check = "matches" if len(raw) == total else "DOES NOT match"
        print(f"\n== Raw configuration {value} ({len(raw)} bytes, {check} wTotalLength {total}) ==")
        for i in range(0, len(raw), 16):
            print(f"{i:04x}  {hexbytes(raw[i:i + 16])}")


if __name__ == "__main__":
    main()
