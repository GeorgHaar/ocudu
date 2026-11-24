#!/usr/bin/env python3
"""Displays an ideal polarized array pattern from synchronized port IQ blocks."""

import argparse
import json
from pathlib import Path
import socket
import struct
import time

import numpy as np

MAGIC = b"OCIQ1"


def encode_packet(samples, **metadata):
    """Encodes port-major complex samples as interleaved bfloat16 I/Q."""
    samples = np.asarray(samples, dtype=np.complex64)
    components = np.stack((samples.real, samples.imag), axis=-1).astype("<f4")
    bits = components.view("<u4")
    rounded = ((bits + 0x7FFF + ((bits >> 16) & 1)) >> 16).astype("<u2")
    header = dict(metadata, ports=samples.shape[0], samples=samples.shape[1],
                  dtype="bf16", byteorder="little", domain="frequency")
    info = json.dumps(header).encode()
    return MAGIC + struct.pack("!I", len(info)) + info + rounded.tobytes()


def decode_packet(packet):
    if len(packet) < 9 or packet[:5] != MAGIC:
        raise ValueError("Invalid IQ packet magic")
    size = struct.unpack("!I", packet[5:9])[0]
    if size > 8192 or len(packet) < 9 + size:
        raise ValueError("Invalid IQ metadata size")
    info = json.loads(packet[9:9 + size])
    ports, count = int(info["ports"]), int(info["samples"])
    if not 1 <= ports <= 32 or not 1 <= count <= 4096:
        raise ValueError("Invalid IQ dimensions")
    if info.get("dtype") != "bf16" or info.get("byteorder") not in ("little", "big"):
        raise ValueError("Unsupported IQ representation")
    payload = packet[9 + size:]
    if len(payload) != ports * count * 4:
        raise ValueError("IQ payload length does not match dimensions")
    order = "<" if info["byteorder"] == "little" else ">"
    bits = np.frombuffer(payload, dtype=order + "u2").astype(np.uint32) << 16
    values = bits.view(np.float32).reshape(ports, count, 2)
    samples = values[..., 0] + 1j * values[..., 1]
    if not np.isfinite(samples).all():
        raise ValueError("Non-finite IQ values")
    return info, samples


def positions(n1, n2, dx, dy):
    """Returns centered element coordinates in wavelengths, x first and y second."""
    x, y = np.meshgrid(np.arange(n1), np.arange(n2), indexing="ij")
    return np.column_stack(((x.ravel() - (n1 - 1) / 2) * dx,
                            (y.ravel() - (n2 - 1) / 2) * dy,
                            np.zeros(n1 * n2)))


def directions(azimuth, elevation):
    az, el = np.broadcast_arrays(np.deg2rad(azimuth), np.deg2rad(elevation))
    return np.stack((np.cos(el) * np.sin(az), np.sin(el),
                     np.cos(el) * np.cos(az)), axis=-1)


def array_power(covariance, coordinates, polarizations, azimuth, elevation):
    """Adds powers of ideal orthogonal polarizations for a transmit array."""
    steering = np.exp(2j * np.pi * (directions(azimuth, elevation) @ coordinates.T))
    count = len(coordinates)
    power = np.zeros(steering.shape[:-1])
    for pol in range(polarizations):
        block = covariance[pol * count:(pol + 1) * count,
                           pol * count:(pol + 1) * count]
        power += np.einsum("...p,pq,...q->...", steering, block,
                           steering.conj(), optimize=True).real
    return np.maximum(power, 0)


def normalized_db(power, peak):
    if peak <= 0:
        return np.full_like(power, -60.0)
    return 10 * np.log10(np.maximum(power / peak, 1e-6))


def radiation_surface(power, azimuth, elevation, peak):
    """Returns a radial surface whose radius is normalized field amplitude."""
    radius = np.sqrt(np.clip(power / peak, 0, 1)) if peak > 0 else np.zeros_like(power)
    xyz = directions(azimuth, elevation) * radius[..., np.newaxis]
    return xyz, normalized_db(power, peak)


def demo_samples(n1, n2, polarizations, rng):
    coords = positions(n1, n2, 0.5, 0.5)
    aim = directions(20.0, 10.0 if n2 > 1 else 0.0)
    spatial = np.exp(-2j * np.pi * (coords @ aim))
    layers = 2 if polarizations == 2 else 1
    weights = np.zeros((n1 * n2 * polarizations, layers), dtype=np.complex64)
    for pol in range(polarizations):
        for layer in range(layers):
            phase = (1j if pol else 1) * (-1 if pol and layer else 1)
            weights[pol * len(coords):(pol + 1) * len(coords), layer] = spatial * phase
    weights /= np.sqrt(weights.size)
    symbols = (rng.choice((-1, 1), (layers, 128)) +
               1j * rng.choice((-1, 1), (layers, 128))) / np.sqrt(2)
    return weights @ symbols


class Stream:
    def __init__(self, args):
        self.args = args
        self.rng = np.random.default_rng(7)
        self.covariance = None
        self.info = {}
        self.received = 0
        self.invalid = 0
        self.last_received = None
        self.sock = None
        if args.capture:
            self.accept(Path(args.capture).read_bytes())
        elif not args.demo:
            host, port = args.listen.rsplit(":", 1)
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.bind((host, int(port)))
            self.sock.setblocking(False)

    def accept(self, packet):
        try:
            info, samples = decode_packet(packet)
            expected = self.args.n1 * self.args.n2 * self.args.polarizations
            if samples.shape[0] != expected:
                raise ValueError("Port count does not match geometry")
            covariance = samples @ samples.conj().T / samples.shape[1]
            self.covariance = covariance if self.covariance is None else (
                0.8 * self.covariance + 0.2 * covariance)
            self.info = info
            self.received += 1
            self.last_received = time.monotonic()
        except (ValueError, KeyError, TypeError, OverflowError):
            self.invalid += 1

    def poll(self):
        if self.args.demo:
            packet = encode_packet(demo_samples(self.args.n1, self.args.n2,
                                                self.args.polarizations, self.rng),
                                   source="SYNTHETIC DEMO", sequence=self.received)
            self.accept(packet)
        elif self.sock:
            for _ in range(128):
                try:
                    packet, _ = self.sock.recvfrom(65535)
                except BlockingIOError:
                    break
                self.accept(packet)


def show(args):
    if args.save_preview:
        import matplotlib
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation
    from matplotlib.widgets import Slider

    stream = Stream(args)
    fig = plt.figure(figsize=(13.5, 8.5))
    fig.suptitle("Ideales Array-Richtdiagramm aus precodierten Portsignalen", fontsize=17)
    grid = fig.add_gridspec(2, 2, left=.07, right=.95, top=.9, bottom=.23,
                           hspace=.47, wspace=.3)
    geometry_ax = fig.add_subplot(grid[0, 0])
    cut_ax = fig.add_subplot(grid[0, 1])
    map_ax = fig.add_subplot(grid[1, 0])
    status_ax = fig.add_subplot(grid[1, 1])
    status_ax.axis("off")
    status = status_ax.text(0, 1, "", va="top", fontsize=10, linespacing=1.6)
    markers = geometry_ax.scatter([], [], s=100, color="#007f88")
    labels = [geometry_ax.text(0, 0, str(i), fontsize=9)
              for i in range(args.n1 * args.n2)]
    geometry_ax.set(xlabel="x / Wellenlaenge", ylabel="y / Wellenlaenge",
                    title="Positionen pro Polarisation (Ports sind paarweise kolokalisiert)")
    geometry_ax.set_aspect("equal", adjustable="box")
    geometry_ax.grid(alpha=.25)
    azimuth = np.linspace(-90, 90, 361)
    az_map, el_map = np.meshgrid(np.linspace(-90, 90, 181), np.linspace(-90, 90, 91))
    line, = cut_ax.plot(azimuth, np.full_like(azimuth, -60), color="#007f88")
    cut_ax.set(xlim=(-90, 90), ylim=(-60, 1), xlabel="Azimut / Grad",
               ylabel="Relative Leistung / dB", title="Azimutschnitt, Elevation 0 Grad")
    cut_ax.grid(alpha=.25)
    image = map_ax.imshow(np.full_like(az_map, -60), extent=(-90, 90, -90, 90),
                          origin="lower", aspect="auto", vmin=-60, vmax=0, cmap="viridis")
    map_ax.set(xlabel="Azimut / Grad", ylabel="Elevation / Grad",
               title="Vorderer Halbraum: +z ist die Flaechennormale")
    fig.colorbar(image, ax=map_ax, label="Relative Leistung / dB")
    dx = Slider(fig.add_axes([.17, .13, .65, .025]), "dx / lambda", .1, 2.0, valinit=.5)
    dy = Slider(fig.add_axes([.17, .08, .65, .025]), "dy / lambda", .1, 2.0, valinit=.5)
    fig.text(.07, .025, "Ideale isotrope Elemente; keine Kopplung oder Hardwarefehler. "
             "Abstandsregler aendern das Modell, nicht die gNB.", fontsize=10)

    fig3d = None
    surface = None
    last_surface_key = None
    if args.view_3d:
        from matplotlib import colormaps
        from matplotlib.cm import ScalarMappable
        from matplotlib.colors import Normalize

        fig3d = plt.figure(figsize=(10, 9))
        fig3d.suptitle("3D-Abstrahlmuster aus denselben Portsignalen", fontsize=17)
        ax3d = fig3d.add_subplot(projection="3d")
        fig3d.subplots_adjust(left=.06, right=.83, top=.9, bottom=.24)
        ax3d.set(xlim=(-1.05, 1.05), ylim=(-1.05, 1.05), zlim=(-1.05, 1.05),
                 xlabel="x: horizontal", ylabel="y: vertikal", zlabel="z: Panelnormale")
        ax3d.set_box_aspect((1, 1, 1))
        ax3d.view_init(elev=22, azim=-55)
        cmap = colormaps["viridis"]
        color_norm = Normalize(vmin=-40, vmax=0, clip=True)
        fig3d.colorbar(ScalarMappable(norm=color_norm, cmap=cmap), ax=ax3d,
                       shrink=.7, pad=.1, label="Relative Leistung / dB")
        az3d, el3d = np.meshgrid(np.linspace(-180, 180, 121), np.linspace(-90, 90, 61))
        status3d = fig3d.text(.08, .19, "Warte auf IQ-Daten", fontsize=11)
        dx3d = Slider(fig3d.add_axes([.2, .13, .6, .025]), "dx / lambda", .1, 2, valinit=.5)
        dy3d = Slider(fig3d.add_axes([.2, .08, .6, .025]), "dy / lambda", .1, 2, valinit=.5)
        fig3d.text(.08, .035, "Maus: drehen / zoomen. Radius = relative Feldamplitude; Farbe = Leistung.\n"
                   "Vollkugel mit isotropen Elementen: auch die Rueckseite strahlt ab.", fontsize=10)

        def set_main_spacing(slider, value):
            if slider.val != value:
                slider.set_val(value)

        dx3d.on_changed(lambda value: set_main_spacing(dx, value))
        dy3d.on_changed(lambda value: set_main_spacing(dy, value))

    def update(_=None):
        nonlocal surface, last_surface_key
        stream.poll()
        coords = positions(args.n1, args.n2, dx.val, dy.val)
        markers.set_offsets(coords[:, :2])
        for i, annotation in enumerate(labels):
            annotation.set_position((coords[i, 0] + .04, coords[i, 1] + .04))
        limit = max(.6, np.max(np.abs(coords[:, :2])) + .3)
        geometry_ax.set(xlim=(-limit, limit), ylim=(-limit, limit))
        if stream.covariance is None:
            state = "WARTE AUF ECHTE IQ-DATEN"
            peak_text = "Noch kein gueltiger Block empfangen."
        else:
            cut = array_power(stream.covariance, coords, args.polarizations, azimuth, 0)
            field = array_power(stream.covariance, coords, args.polarizations, az_map, el_map)
            peak = max(float(field.max()), float(cut.max()))
            line.set_ydata(normalized_db(cut, peak))
            image.set_data(normalized_db(field, peak))
            maximum = int(np.argmax(cut))
            peak_text = "Schnittmaximum: {:.1f} Grad".format(azimuth[maximum])
            state = "SYNTHETISCHE DEMO" if args.demo else (
                "AUFZEICHNUNG" if args.capture else "GDB-STICHPROBENSTREAM")
        if fig3d is not None and plt.fignum_exists(fig3d.number):
            if dx3d.val != dx.val:
                dx3d.set_val(dx.val)
            if dy3d.val != dy.val:
                dy3d.set_val(dy.val)
            status3d.set_text("{} | dx={:.2f} lambda, dy={:.2f} lambda | Bloecke: {}".format(
                state, dx.val, dy.val, stream.received))
            surface_key = (stream.received, dx.val, dy.val)
            if stream.covariance is not None and surface_key != last_surface_key:
                power3d = array_power(stream.covariance, coords, args.polarizations, az3d, el3d)
                peak3d = max(float(power3d.max()), peak)
                xyz, db3d = radiation_surface(power3d, az3d, el3d, peak3d)
                if surface is not None:
                    surface.remove()
                surface = ax3d.plot_surface(xyz[..., 0], xyz[..., 1], xyz[..., 2],
                                            facecolors=cmap(color_norm(db3d)),
                                            rstride=1, cstride=1, linewidth=0,
                                            antialiased=False, shade=False)
                last_surface_key = surface_key
            fig3d.canvas.draw_idle()
        age = "--" if stream.last_received is None else "{:.1f} s".format(
            time.monotonic() - stream.last_received)
        status.set_text("{}\n{}\n\nPanel: {}x{}, {} Polarisation(en), {} Ports\n"
                        "Modell: f={:.3f} GHz, dx={:.2f} cm, dy={:.2f} cm\n"
                        "Bloecke: {} | ungueltig / falsche Geometrie: {}\n"
                        "Letzter Empfang vor: {}\n\n"
                        "Port = pol*(N1*N2) + ix*N2 + iy\n"
                        "Orthogonale Polarisationen: Leistungen addieren.\n"
                        "Gemittelte Portkovarianz; IFFT ist dafuer nicht noetig.\n"
                        "Bei N2=1 hat dy keinen Einfluss.\n"
                        "GDB-Abgriff bremst die gNB; kein Stream mit voller Datenrate."
                        .format(state, peak_text, args.n1, args.n2, args.polarizations,
                                len(coords) * args.polarizations, args.frequency_ghz,
                                dx.val * 29.9792458 / args.frequency_ghz,
                                dy.val * 29.9792458 / args.frequency_ghz, stream.received,
                                stream.invalid, age))
        if plt.fignum_exists(fig.number):
            fig.canvas.draw_idle()

    dx.on_changed(update)
    dy.on_changed(update)
    update()
    if args.save_preview:
        if stream.covariance is None:
            raise ValueError("A preview requires --demo or a valid --capture file")
        fig.savefig(args.save_preview, dpi=140)
        if fig3d is not None:
            preview = Path(args.save_preview)
            destination = preview.with_name(preview.stem + "_3d" + preview.suffix)
            fig3d.savefig(destination, dpi=140)
            print("3D preview:", destination)
            plt.close(fig3d)
        plt.close(fig)
    else:
        last_tick = 0.0

        def tick(_):
            nonlocal last_tick
            now = time.monotonic()
            if now - last_tick >= .18:
                last_tick = now
                update()

        # Each open window keeps reception active when the other one closes.
        windows = [fig] if fig3d is None else [fig, fig3d]
        animations = [FuncAnimation(window, tick, interval=200, cache_frame_data=False)
                      for window in windows]
        plt.show()
        animations.clear()
    if stream.sock:
        stream.sock.close()


def self_test():
    samples = np.array([[.216930464 + .379628301j, 1j], [-1 + 2j, 0]], np.complex64)
    _, decoded = decode_packet(encode_packet(samples))
    assert decoded[0, 0] == np.complex64(.216796875 + .37890625j)
    assert decoded[1, 0] == -1 + 2j
    try:
        decode_packet(encode_packet(samples)[:-1])
        raise AssertionError("Truncated packet accepted")
    except ValueError:
        pass
    coords = positions(4, 1, .5, .5)
    scan = np.linspace(-90, 90, 1801)
    aim = 20.0
    weights = np.exp(-2j * np.pi * (coords @ directions(aim, 0)))
    covariance = np.outer(weights, weights.conj())
    power = array_power(covariance, coords, 1, scan, 0)
    assert abs(scan[np.argmax(power)] - aim) < .11
    # Doubling spacing halves the sine of the steering angle for the same weights.
    changed = array_power(covariance, positions(4, 1, 1, .5), 1, scan, 0)
    target = np.rad2deg(np.arcsin(np.sin(np.deg2rad(aim)) / 2))
    window = abs(scan - target) < .15
    assert changed[window].max() > .999 * changed.max()
    # Ideal orthogonal polarizations add power and never cancel each other.
    cross = np.array([1, -1], dtype=complex)
    polarized = array_power(np.outer(cross, cross.conj()), positions(1, 1, .5, .5),
                            2, np.array([0.0]), 0)
    assert np.allclose(polarized, 2)
    az, el = np.meshgrid(np.linspace(-180, 180, 121), np.linspace(-90, 90, 61))
    sphere_power = array_power(np.ones((1, 1)), positions(1, 1, .5, .5), 1, az, el)
    sphere, db = radiation_surface(sphere_power, az, el, sphere_power.max())
    assert np.allclose(np.linalg.norm(sphere, axis=-1), 1)
    assert np.allclose(db, 0)
    empty, empty_db = radiation_surface(np.zeros_like(az), az, el, 0)
    assert np.isfinite(empty).all() and np.all(empty == 0) and np.all(empty_db == -60)
    # An ideal planar isotropic array has equal front and back radiation.
    front = array_power(covariance, coords, 1, 20, 15)
    back = array_power(covariance, coords, 1, 160, 15)
    assert np.allclose(front, back)
    print("Passed: bf16, packets, beam direction, spacing, polarization and 3D sphere/zero/front-back cases.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n1", type=int, default=2)
    parser.add_argument("--n2", type=int, default=1)
    parser.add_argument("--polarizations", type=int, choices=(1, 2), default=2)
    parser.add_argument("--frequency-ghz", type=float, default=3.5,
                        help="Model frequency for displaying physical distances; default: 3.5 GHz")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--demo", action="store_true")
    source.add_argument("--capture", help="Saved OCIQ1 packet")
    parser.add_argument("--listen", default="127.0.0.1:19090")
    parser.add_argument("--save-preview", help="Saves a static PNG without opening a window")
    parser.add_argument("--3d", dest="view_3d", action="store_true",
                        help="Opens an additional rotatable 3D pattern with synchronized spacing sliders")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.n1 < 1 or args.n2 < 1 or args.n1 * args.n2 * args.polarizations > 32:
        parser.error("Geometry must contain 1 to 32 ports")
    if not np.isfinite(args.frequency_ghz) or args.frequency_ghz <= 0:
        parser.error("Model frequency must be finite and positive")
    show(args)


if __name__ == "__main__":
    main()
