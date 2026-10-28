# Original disc font extraction — 2026-10-27

Task: review font loading in the native port and extract usable fonts from the original ISO.
Baseline: 5d36df5. Read-only source review; existing frontend/OMP claims and dirty files preserved. Only this uniquely named handoff is changed in the repository; commercial output is outside it.

## Findings and output

`src/text.cpp` loads TEXT/BADLOC.DLL, FUTB.DLL, FUTD.DLL and FUTR.DLL directly as TrueType; `src/text.hpp` records original installer address 0x0042f720. `src/frontend.cpp` uses Badloc for headings and Futura for supporting menu text. This was a focused font-path review, not a general code audit or new original-runtime capture.

Extracted directly through ISO9660 directory records from the original workspace ISO into `/Users/archit/Dev/rr/extracted-fonts/`. All four byte streams also equal their counterparts under `cdrom/ROADRASH/TEXT/`. Output files: Badloc.ttf, Futura-Bold.ttf, Futura-Demi.ttf, Futura-Regular.ttf, manifest.json, README.txt, font-preview.png and Road-Rash-Fonts.zip. No disc assets added to Git.

Font name tables identify Badloc Regular, Futura Condensed Bold, Futura Condensed Demi Regular and Futura Condensed Regular. Each has 206 glyphs and 207 Unicode mappings. No conversion is required: .DLL is only a misleading filename extension.

## Verification

Python fontTools 4.60.2 parsed every table of all four files. Pillow/FreeType loaded and rendered all four in the preview; preview visually inspected. ISO-to-mounted-disc byte equality assertions passed. SHA-256 values and source paths are in the local manifest. All four original fonts have a head table checksum mismatch (computed with checkSumAdjustment zeroed); extraction preserves these original bytes rather than silently repairing them. Modern antialiasing in the specimen is not an original GDI pixel-parity claim.

No native build needed: no native source changed. No Wine, Ghidra, device or shared build resources used; no processes or claims held. Shared PROGRESS/VERIFICATION/REVERSE_ENGINEERING docs remain owned by the frontend session, so evidence is recorded here under concurrent-work policy.
