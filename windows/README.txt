Road Rash Native for Windows - development build

Requirements: Windows 10 version 1903 or later, or Windows 11; x64 or ARM64.

Run rr-game.exe and import your own Road Rash 1997 rerelease disc image (ISO)
or extracted ROADRASH folder. Original game data is not included.

Where your data goes
- Portable zip: portable.txt sits next to rr-game.exe, so saves, settings,
  the imported disc and logs are kept in the "data" folder beside it. The
  folder can live on a USB stick. Delete portable.txt to use the per-user
  folders below instead.
- Installed copy (and the zip without portable.txt): saves and settings in
  %APPDATA%\RoadRashNative\RoadRash; the imported disc and logs in
  %LOCALAPPDATA%\RoadRashNative\RoadRash. Uninstalling keeps them unless you
  choose to remove them.

Windows SmartScreen: builds that are not code-signed show "Windows protected
your PC" on first launch. Choose "More info", then "Run anyway".

Problem reports: attach rr-game.log and rr-game.1.log (the previous run) from
the data folder above. From a terminal, "rr-game.exe --version --console"
prints the build, and "--console" shows log output while the game runs.

This program includes SDL3 (zlib licence), stb (MIT licence) and, for online
play, libdatachannel and libjuice (MPL-2.0), usrsctp (BSD-3-Clause), plog (MIT)
and OpenSSL (Apache-2.0). Their licences are in the "licenses" folder; MPL-2.0
source is available from the upstream projects, unmodified.

The port is in development. Gameplay, presentation and original-game parity
remain incomplete.
