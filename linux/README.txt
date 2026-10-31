Road Rash Native for Linux - development build

Requirements: a 64-bit x86-64 or ARM64 Linux with glibc 2.31 or later
(SteamOS 3, Ubuntu 20.04+, Debian 11+, Fedora 34+, Arch), X11 or Wayland, and
PipeWire, PulseAudio or ALSA. Nothing else needs installing: the game loads
the system's display, audio and controller libraries when it starts.

Run ./rr-game (tarball) or the .AppImage and import your own Road Rash 1997
rerelease disc image (ISO) or extracted ROADRASH folder. Original game data is
not included.

Menu entry (tarball): ./install-desktop-entry.sh adds Road Rash Native to
your application menu for this folder; run it again after moving the folder,
or with --remove to take the entry away.

Where your data goes: saves, settings, the imported disc and the log
(rr-game.log) are in ~/.local/share/RoadRashNative/RoadRash (or under
$XDG_DATA_HOME when it is set). The Flatpak keeps the same layout in
~/.var/app/org.roadrash.nativeport/data/RoadRashNative/RoadRash.
Deleting the program keeps them.

Problem reports: attach rr-game.log and rr-game.1.log (the previous run) from
the data folder above. "./rr-game --version" prints the build.

This program includes SDL3 (zlib licence), stb (MIT licence), DejaVu Sans
(Bitstream Vera licence; used when the system has no DejaVu, Noto or
Liberation font) and, for online play, libdatachannel and libjuice
(MPL-2.0), usrsctp (BSD-3-Clause), plog (MIT) and OpenSSL (Apache-2.0).
Their licences are in the "licenses" folder. MPL-2.0 source is available from
the upstream projects; the one change to libdatachannel (certificate checks
stay on with a caller-supplied root store) is in the port's cmake/net.cmake.

The port is in development. Gameplay, presentation and original-game parity
remain incomplete.
