# XDK versions of our Xbox 360 ports

Read 2026-10-29. Later ports record their XDK in their own notes. A game's XDK decides how far its Direct3D is from the
layout reNut's renderer was written for, so it is worth checking first when starting a port (N&B's title update is 2.0.7978).

## XDK by game

The 19 games read span XDK builds 2.0.2135 (Zuma) to 2.0.21256 (Peggle 2). Two title updates moved to a newer XDK,
Banjo-Kazooie's (2.0.7978) and A Kingdom for Keflings' (D3D 2.0.7661); every other title update kept its game's. Sorted
oldest XDK first.

| Game | Port | Title ID | XDK | Highest QFE | Title update |
|---|---|---|---|---|---|
| Zuma | [zuma-nx](https://github.com/ChanseyIsTheBest/zuma-nx) | 584107EF | 2.0.2135 | 1 | v0.0.3.1, same XDK |
| Feeding Frenzy | [feedingfrenzy-nx](https://github.com/ChanseyIsTheBest/feedingfrenzy-nx) | 584107F1 | 2.0.2571 | 1 | none |
| Beautiful Katamari | [katamari-nx](https://github.com/ChanseyIsTheBest/katamari-nx) | 4E4D07DC | 2.0.5632 | 0 | none |
| Feeding Frenzy 2: Shipwreck Showdown | [feedingfrenzy2-nx](https://github.com/ChanseyIsTheBest/feedingfrenzy2-nx) | 5841088C | 2.0.6534 | 1 | none |
| Banjo-Kazooie: Nuts & Bolts | [renut-nx](https://github.com/ChanseyIsTheBest/renut-nx) | 4D5307ED | 2.0.7645 | 1 | v0.0.3.1, **2.0.7978 QFE 3** |
| A Kingdom for Keflings | [keflings-nx](https://github.com/ChanseyIsTheBest/keflings-nx) | 584108DB | 2.0.7645 | 1 | v0.0.3.2, D3D 2.0.7661 |
| Peggle | [peggle1-nx](https://github.com/ChanseyIsTheBest/peggle1-nx) | 58410889 | 2.0.7776 | 0 | v0.0.4.2, same XDK |
| Mushihimesama Futari | [futari-nx](https://github.com/ChanseyIsTheBest/futari-nx) | 435A07D7 | 2.0.9328 | 4 | none |
| Ketsui: Kizuna Jigoku Tachi | [ketsui-nx](https://github.com/ChanseyIsTheBest/ketsui-nx) | 465607D1 | 2.0.9328 | 9 | none |
| Skate 3 | [skate3recomp-nx](https://github.com/ChanseyIsTheBest/skate3recomp-nx) | 454108E6 | 2.0.9328 | 9 | v0.0.3.3, same XDK |
| Guwange | [guwange-nx](https://github.com/ChanseyIsTheBest/guwange-nx) | 58410A78 | 2.0.11164 | 3 | v0.0.1.4, same XDK |
| Bejeweled Blitz LIVE | bejeweledblitz-nx | 58410A57 | 2.0.20209 | 3 | none |
| Bangai-O HD: Missile Fury | [bangaio-nx](https://github.com/ChanseyIsTheBest/bangaio-nx) | 58410AF2 | 2.0.20209 | 3 | none |
| Bejeweled 3 | [bejeweled3-nx](https://github.com/ChanseyIsTheBest/bejeweled3-nx) | 584111D6 | 2.0.20764 | 0 | none |
| Zuma's Revenge! | [zumasrevenge-nx](https://github.com/ChanseyIsTheBest/zumasrevenge-nx) | 584111E9 | 2.0.20871 | 2 | none |
| Sonic Adventure 2 | [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx) | 58411202 | 2.0.21076 | 9 | none |
| Sonic & All-Stars Racing Transformed | sasrt-nx | 5345085D | 2.0.21119 | 0 | none |
| NiGHTS into Dreams... | [nights-nx](https://github.com/ChanseyIsTheBest/nights-nx) | 58411268 | 2.0.21173 | 0 | v0.0.1.1, same XDK |
| Peggle 2 | [peggle2-nx](https://github.com/ChanseyIsTheBest/peggle2-nx) | 58411436 | 2.0.21256 | 0 | none |

Not read: Sonic Unleashed ([UnleashedRecomp-NX](https://github.com/ChanseyIsTheBest/UnleashedRecomp-NX)) and Sonic the
Hedgehog (2006) ([MarathonRecomp-NX](https://github.com/ChanseyIsTheBest/MarathonRecomp-NX)).

## Details

Each game imports xboxkrnl.exe and xam.xex at its XDK build except Beautiful Katamari (2.0.5766, against its 2.0.5632
libraries), and both libraries ask for the same minimum kernel. Only the executables built with XDK 2.0.11164 or later
record their compiler and linker.

| Game | Executable version | Kernel imports | Minimum kernel | Compiler (C1/C2) | Linker |
|---|---|---|---|---|---|
| Zuma | 0.0.1.1 (TU 0.0.3.1) | 2.0.2135.0 | 2.0.1861.0 | not recorded | not recorded |
| Feeding Frenzy | 0.0.1.1 | 2.0.2571.0 | 2.0.1861.0 | not recorded | not recorded |
| Beautiful Katamari | 0.0.0.4 | 2.0.5766.0 | 2.0.5766.0 | not recorded | not recorded |
| Feeding Frenzy 2 | 0.0.1.2 | 2.0.6534.0 | 2.0.1861.0 | not recorded | not recorded |
| Banjo-Kazooie: Nuts & Bolts | 0.0.0.1 (TU 0.0.3.1) | 2.0.7645.0 (TU 2.0.7978.0) | 2.0.6717.0 | not recorded | not recorded |
| A Kingdom for Keflings | 0.0.1.2 (TU 0.0.3.2) | 2.0.7645.0 | 2.0.1861.0 | not recorded | not recorded |
| Peggle | 0.0.1.2 (TU 0.0.4.2) | 2.0.7776.0 | 2.0.1861.0 | not recorded | not recorded |
| Mushihimesama Futari | 0.0.0.1 | 2.0.9328.0 | 2.0.8498.0 | not recorded | not recorded |
| Ketsui: Kizuna Jigoku Tachi | 0.0.0.1 | 2.0.9328.0 | 2.0.8955.0 | not recorded | not recorded |
| Skate 3 | 0.0.0.3 (TU 0.0.3.3) | 2.0.9328.0 | 2.0.8955.0 | not recorded | not recorded |
| Guwange | 0.0.0.4 (TU 0.0.1.4) | 2.0.11164.0 | 2.0.9199.0 | 16.0.10224.0 | 10.0.10224.0 |
| Bejeweled Blitz LIVE | 0.0.0.3 | 2.0.20209.0 | 2.0.9199.0 | 16.0.10224.0 | 10.0.10224.0 |
| Bangai-O HD: Missile Fury | 0.0.0.2 | 2.0.20209.0 | 2.0.9199.0 | 16.0.10224.0 | 10.0.10224.0 |
| Bejeweled 3 | 0.0.0.1 | 2.0.20764.0 | 2.0.12611.0 | 16.0.11886.0 | 10.0.11886.0 |
| Zuma's Revenge! | 0.0.0.1 | 2.0.20871.0 | 2.0.12611.0 | 16.0.11886.0 | 10.0.11886.0 |
| Sonic Adventure 2 | 0.0.0.3 | 2.0.21076.0 | 2.0.12611.0 | 16.0.11886.0 | 10.0.11886.0 |
| Sonic & All-Stars Racing Transformed | 0.0.0.2 | 2.0.21119.0 | 2.0.15574.0 | 16.0.11886.0 | 10.0.11886.0 |
| NiGHTS into Dreams... | 0.0.0.1 (TU 0.0.1.1) | 2.0.21173.0 | 2.0.12611.0 | 16.0.11886.0 | 10.0.11886.0 |
| Peggle 2 | 0.0.0.4 | 2.0.21256.0 | 2.0.12611.0 | 16.0.11886.0 | 10.0.11886.0 |

The extra modules some games load match their game's XDK: Sonic Adventure 2's Data_DLL.xex (2.0.21076), Futari's character
modules (2.0.9328) and Skate 3's EAWebkit.xex (2.0.9328 QFE 9).

## How these were read

The versions come from each game's own `default.xex` header.

- **XDK**: the static-library list (XEX2 header `0x000200FF`) names every XDK library linked in, such as XAPILIB, D3D9,
  XAUDIO2 and XBOXKRNL, with its version. The build number is the XDK; the highest QFE (quick-fix) level among them is listed
  separately, so 2.0.9328.9 reads as build 9328, QFE 9.
- **Kernel imports**: the import-library list (`0x000103FF`) gives the xboxkrnl.exe and xam.xex version each game linked
  against, and the minimum version it runs on.
- **Title updates**: each `.xexp` header delta was applied to the base header the way ReXGlue's `XexModule::ApplyPatch` does
  it (LZX delta, with the SDK's libmspack), then the same lists were read. A Kingdom for Keflings' update was read from D3D's
  build constant in its recompiled image instead (0x000200001DED4000 = 2.0.7661.0; the base game's is 2.0.7645.1).
- Only build numbers are given. They are not mapped to XDK release names ("November 2009" and the like), since nothing here
  confirms that mapping.
- The C++ runtime (LIBCPMT) in the newest games is 2.0.11886, the compiler's own build, not their XDK.
