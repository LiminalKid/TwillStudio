#!/usr/bin/env python3
"""Packages the sndstation build as a Debian package, twill-studio_<version>_arm64.deb, laid out the
way sndstation apps are: the program and its start script in /usr/lib/twill-studio/, the launcher's
entry in /usr/share/sndstation/apps/, the README and licences in /usr/share/doc/twill-studio/.

    cmake --build --preset sndstation --target twill && tools/make-sndstation-deb.py [out-dir]

Default output folder: build/sndstation-deb/. Needs only Python 3 (no dpkg-deb), so it runs on a
Mac and on Linux alike. The SDK's licences are taken from SNDSTATION_DIR (default ~/SDKs/sndstation).
"""
import gzip
import hashlib
import io
import os
import re
import subprocess
import sys
import tarfile
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "sndstation", "twill")
SDK = os.environ.get("SNDSTATION_DIR", os.path.expanduser("~/SDKs/sndstation"))
PKG = "twill-studio"
MTIME = 0  # every file dated the same, so two builds of the same commit give the same package


def version():
    # TWILL_DEB_VERSION for a test build between releases ("0.5.0+500fa1e" sorts above 0.5.0).
    if os.environ.get("TWILL_DEB_VERSION"):
        return os.environ["TWILL_DEB_VERSION"]
    with open(os.path.join(ROOT, "CMakeLists.txt")) as f:
        m = re.search(r"project\(TwillStudio VERSION ([0-9.]+)", f.read())
    return m.group(1)


def files():
    """(path in the package, source file, mode), plus the licences gathered into a temp folder."""
    lic = tempfile.mkdtemp()
    subprocess.run([os.path.join(ROOT, "tools", "copy-licenses.sh"), lic], check=True)
    # The SDK is linked into the program, with the libraries it carries.
    for name, src in [("sndstation", "LICENSE"), ("nlohmann-json", "third_party/nlohmann/LICENSE.MIT"),
                      ("clap", "third_party/clap/LICENSE"), ("mdfourier", "src/mdfourier/LICENSE")]:
        path = os.path.join(SDK, src)
        if not os.path.exists(path):
            sys.exit(f"{path} is missing: set SNDSTATION_DIR to the SDK the build used")
        with open(path, "rb") as f, open(os.path.join(lic, f"LICENSE.{name}.txt"), "wb") as o:
            o.write(f.read())
    out = [
        (f"usr/lib/{PKG}/twill", BIN, 0o755),
        (f"usr/lib/{PKG}/twill-studio.sh", os.path.join(ROOT, "packaging/sndstation/twill-studio.sh"), 0o755),
        (f"usr/share/sndstation/apps/{PKG}.json", os.path.join(ROOT, "packaging/sndstation/twill-studio.json"), 0o644),
        (f"usr/share/doc/{PKG}/README.md", os.path.join(ROOT, "packaging/sndstation/README.md"), 0o644),
    ]
    for name in sorted(os.listdir(lic)):
        out.append((f"usr/share/doc/{PKG}/licenses/{name}", os.path.join(lic, name), 0o644))
    return out


def tar_gz(entries):
    """entries: (path, bytes or None for a folder, mode). Owned by root, dated MTIME."""
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=MTIME) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.GNU_FORMAT) as t:
            for path, data, mode in entries:
                info = tarfile.TarInfo("./" + path if path else ".")
                info.uid = info.gid = 0
                info.uname = info.gname = "root"
                info.mtime = MTIME
                info.mode = mode
                if data is None:
                    info.type = tarfile.DIRTYPE
                    t.addfile(info)
                else:
                    info.size = len(data)
                    t.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def ar(members):
    """A Debian package's outer archive: the common ar format, short names."""
    out = bytearray(b"!<arch>\n")
    for name, data in members:
        out += f"{name:<16}{MTIME:<12}{0:<6}{0:<6}{'100644':<8}{len(data):<10}`\n".encode()
        out += data
        if len(data) % 2:
            out += b"\n"
    return bytes(out)


def main():
    if not os.path.exists(BIN):
        sys.exit("Build first: cmake --preset sndstation && cmake --build --preset sndstation --target twill")
    v = version()
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "sndstation-deb")
    os.makedirs(out_dir, exist_ok=True)

    data_entries, sums, size = [("", None, 0o755)], [], 0
    folders = set()
    for path, src, mode in files():
        parts = path.split("/")
        for i in range(1, len(parts)):
            d = "/".join(parts[:i])
            if d not in folders:
                folders.add(d)
                data_entries.append((d, None, 0o755))
        with open(src, "rb") as f:
            data = f.read()
        data_entries.append((path, data, mode))
        sums.append(f"{hashlib.md5(data).hexdigest()}  {path}\n")
        size += len(data)

    control = f"""Package: {PKG}
Version: {v}
Section: sound
Priority: optional
Architecture: arm64
Depends: sndstation, libc6 (>= 2.27), libsdl2-2.0-0 (>= 2.0.20), libasound2t64 | libasound2
Maintainer: Liminal Kid <liminalkid17@gmail.com>
Installed-Size: {(size + 1023) // 1024}
Homepage: https://github.com/LiminalKid/TwillStudio
Sndstation-Kind: app
Description: Piano-roll groovebox for sndstation handhelds
 Twill Studio makes songs with synths, drums and samples: eight tracks, a piano
 roll, an arranger and a mixer, all played from the handheld's buttons or a
 MIDI keyboard. Listed in the sndstation launcher once installed; songs,
 samples and renders are kept in ~/TwillStudio.
"""
    control_tar = tar_gz([("", None, 0o755), ("control", control.encode(), 0o644),
                          ("md5sums", "".join(sums).encode(), 0o644)])
    deb = ar([("debian-binary", b"2.0\n"), ("control.tar.gz", control_tar),
              ("data.tar.gz", tar_gz(data_entries))])
    path = os.path.join(out_dir, f"{PKG}_{v}_arm64.deb")
    with open(path, "wb") as f:
        f.write(deb)
    print(f"{path}: {len(deb) // 1024} KB, {len(sums)} files")
    for line in sums:
        print("  /" + line.split("  ", 1)[1].rstrip())


if __name__ == "__main__":
    main()
