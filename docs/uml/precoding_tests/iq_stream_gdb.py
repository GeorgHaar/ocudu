"""Samples completed multiport PDSCH blocks without changing the C++ sources.

Load in GDB, then run `iq-stream-start` and `continue`. This debugger tap is
deliberately a diagnostic sampler, not a full-rate real-time export.
"""

import gdb
import json
import socket
import struct
import time


class IQStreamBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super().__init__("lib/phy/support/resource_grid_mapper_impl.cpp:183")
        self.enabled = False
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        self.destination = ("127.0.0.1", 19090)
        self.minimum_interval = .2
        self.next_send = 0
        self.sequence = 0
        self.errors = 0
        self.snapshot_path = None
        self.one_shot = False
        self.condition = "prg_weights.data.dimensions_size._M_elems[0] == 2"

    def packet(self):
        weights = gdb.parse_and_eval("prg_weights")["data"]
        dims = weights["dimensions_size"]["_M_elems"]
        layers, ports = int(dims[0]), int(dims[1])
        count = min(64, int(gdb.parse_and_eval("nof_re_block")))
        if count <= 0:
            return None
        inferior = gdb.selected_inferior()
        chunks = []
        contiguous = bool(gdb.parse_and_eval("is_contiguous"))
        if contiguous:
            slices = gdb.parse_and_eval("precoding_buffer_view")["data"]["_M_elems"]
            addresses = [int(slices[port]["ptr"]) for port in range(ports)]
        else:
            tensor = gdb.parse_and_eval("precoding_buffer_copy")["data"]
            stride = int(tensor["dimensions_size"]["_M_elems"][0]) * 4
            base = int(tensor["elements"]["_M_elems"].address)
            addresses = [base + port * stride for port in range(ports)]
        for address in addresses:
            chunks.append(bytes(inferior.read_memory(address, count * 4)))
        endian = "little" if "little endian" in gdb.execute("show endian", to_string=True) else "big"
        metadata = dict(ports=ports, samples=count, dtype="bf16", byteorder=endian,
                        domain="frequency", layers=layers, sequence=self.sequence,
                        i_symbol=int(gdb.parse_and_eval("i_symbol")),
                        block_i_subc=int(gdb.parse_and_eval("i_subc")),
                        contiguous=contiguous, source="GDB sampled PDSCH",
                        note="All ports share the same packed RE positions; block_i_subc is the block origin.")
        header = json.dumps(metadata).encode()
        return b"OCIQ1" + struct.pack("!I", len(header)) + header + b"".join(chunks)

    def stop(self):
        now = time.monotonic()
        if now < self.next_send:
            return False
        # The ci8 mapper is shared; accept only actual PDSCH callers.
        frame = gdb.newest_frame()
        is_pdsch = False
        while frame:
            if "pdsch" in (frame.name() or "").lower():
                is_pdsch = True
                break
            frame = frame.older()
        if not is_pdsch:
            return False
        self.next_send = now + self.minimum_interval
        try:
            packet = self.packet()
            if packet is not None:
                self.sock.sendto(packet, self.destination)
                if self.snapshot_path:
                    with open(self.snapshot_path, "wb") as output:
                        output.write(packet)
                    self.snapshot_path = None
                self.sequence += 1
                if self.one_shot:
                    gdb.post_event(lambda: setattr(self, "enabled", False))
                return self.one_shot
        except (gdb.error, OSError, ValueError, KeyError, TypeError) as error:
            self.errors += 1
            if self.errors == 1:
                gdb.write("IQ stream: {}\n".format(error))
        return False


class IQStreamStart(gdb.Command):
    """iq-stream-start [HOST PORT]: samples completed two-layer PDSCH blocks."""

    def __init__(self):
        super().__init__("iq-stream-start", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        args = gdb.string_to_argv(argument)
        if len(args) not in (0, 2):
            raise gdb.GdbError("Usage: iq-stream-start [HOST PORT]")
        if args:
            tap.destination = (args[0], int(args[1]))
        tap.one_shot = False
        tap.enabled = True
        gdb.write("IQ sampler enabled -> {}:{}. Run: continue\n".format(*tap.destination))


class IQStreamStop(gdb.Command):
    """Disables the IQ sampling breakpoint."""

    def __init__(self):
        super().__init__("iq-stream-stop", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        tap.enabled = False
        gdb.write("IQ sampler stopped: {} blocks, {} errors.\n".format(tap.sequence, tap.errors))


class IQCaptureOnce(gdb.Command):
    """iq-capture-once PATH: saves one completed multiport block and stops there."""

    def __init__(self):
        super().__init__("iq-capture-once", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        args = gdb.string_to_argv(argument)
        if len(args) != 1:
            raise gdb.GdbError("Usage: iq-capture-once PATH")
        tap.snapshot_path = args[0]
        tap.one_shot = True
        tap.next_send = 0
        tap.enabled = True
        gdb.write("One synchronized PDSCH block will be saved to {}. Run: continue\n".format(args[0]))


if not getattr(gdb, "_ocudu_iq_stream_loaded", False):
    tap = IQStreamBreakpoint()
    IQStreamStart()
    IQStreamStop()
    IQCaptureOnce()
    gdb._ocudu_iq_stream_loaded = True
    gdb.write("IQ tap loaded: iq-stream-start, iq-stream-stop, iq-capture-once PATH\n")
