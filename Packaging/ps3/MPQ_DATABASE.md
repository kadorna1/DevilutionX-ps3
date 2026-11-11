# Diablo and Hellfire MPQ release database

The compiled boot-time catalogue is in Source/engine/mpq_profiles.hpp. It contains exact archive fingerprints separately from distribution labels whose checksums have not been independently verified.

## Verified full-game DIABDAT.MPQ fingerprints

| Boot profile | Release family | Size | Whole-file MD5 |
|---|---|---:|---|
| DiabloUsCd100 | Original US retail PC CD 1.00 | 517,501,242 bytes | 68f049866b44688a7af65ba766bef75a |
| DiabloEuMacGog | European / Mac archive family; GOG-enhanced release | Not published | 011bc6518e6166206231080a4440b373 |

The US checksum set also includes CRC32 855951d6, CRC64 91eb406ca29379c7, SHA-1 fe35cba676a0a194ccdf1d4357de49dff252fa40, SHA-256 6bc120a603fdb383ff91f374e7db599765d6230d81e5a4c1b33f8254911774e7 and BLAKE2sp 86f1016c2bfbae621eaf9a7789537154dcc20bc66238519cc883d0f6e816e40f. The values and size come from the original US CD report in [DevilutionX issue #57](https://github.com/diasurgical/DevilutionX/issues/57). In that same issue the maintainer identifies the file as the original US version and says its logo video is broken.

The EU/Mac/GOG MD5 is recorded by the Freeablo maintainer in [Freeablo issue #427](https://github.com/wheybags/freeablo/issues/427). The maintainer says the GoG archive retained that hash, the only DIABDAT difference from the US archive was the broken logo.smk in the US version, and later retail updates were distributed as Patch_rt.mpq rather than changing DIABDAT.MPQ.

The game streams the whole-file MD5 in fixed 64 KiB chunks. It never loads the 500 MiB archive into memory. If the checksum is not recognized, the archive is still loaded normally and the generic profile is selected. The log records the unknown archive's size and MD5 so an exact new release can be verified and added later.

## Distribution coverage and what is not fingerprinted yet

- **Original US retail CD 1.00:** exact fingerprint present; the boot profile skips only the broken publisher logo video. The actual Diablo intro video remains enabled.
- **European / Mac CD family:** exact MD5 present; normal logo and intro behavior.
- **GOG-enhanced digital release:** the upstream maintainer identifies its DIABDAT.MPQ as matching the EU/Mac family, so it reuses that profile.
- **Blizzard Battle.net-compatible digital release:** Blizzard says this is the original, unchanged Battle.net-compatible edition, distinct from the GOG-enhanced release. The Battle.net product is accepted by the DevilutionX MPQ instructions, but no independently published whole-file hash tying that specific install to either catalogue entry was found. It will still match automatically if its actual bytes equal a verified archive; otherwise it uses the generic profile. See [Blizzard's edition FAQ](https://news.blizzard.com/en-us/article/22887361/diablo-now-available-on-gog-com) and [MPQ requirements](https://devilutionx.com/mpq).
- **Other regional PC CDs and budget re-releases:** no additional independently verified full-game DIABDAT hashes were found in the upstream compatibility record. They are accepted with generic loading behavior; no guessed regional checksums are included.
- **Hellfire retail CD, GOG add-on and Battle.net-compatible add-on:** the required expansion set is hellfire.mpq, hfmonk.mpq, hfmusic.mpq and hfvoice.mpq. Optional hfopt2.mpq, hfopt1.mpq, hfbarb.mpq and hfbard.mpq are also detected and mounted in the upstream resource precedence order. The GOG extraction guide confirms the four required names. No complete trustworthy checksum set for individual Hellfire archives was found, so none are assigned guessed hashes.
- **Shareware/demo:** spawn.mpq remains supported as the shareware archive and is catalogued separately from full-game releases. When spawn.mpq is selected, optional Patch_sh.mpq is used; full retail DIABDAT.MPQ uses Patch_rt.mpq instead.
- **Language assets:** locale-code voice archives (such as pl.mpq and ru.mpq) and CJK fonts.mpq remain separate from the base-game release identity. They are loaded using the existing locale/font systems or the configurable overlay list.

The GOG installer extraction guide lists DIABDAT.MPQ plus the four Hellfire MPQs: [DevilutionX GOG extraction instructions](https://github.com/diasurgical/DevilutionX/wiki/Extracting-MPQs-from-the-GOG-installer). Current MPQ requirements also list CD, GOG and Battle.net as accepted sources: [DevilutionX MPQ instructions](https://devilutionx.com/mpq).

## Retail patch handling

The loader detects Patch_rt.mpq for full retail data and Patch_sh.mpq for shareware data. The selected patch is mounted at priority 1050, above DIABDAT.MPQ/spawn.mpq (1000), below the PS3 VDEC sidecar archive (1100), and below all Hellfire archives. Patch files are optional. This follows the upstream maintainer's resource-order notes, which put patch archives directly above the base game archive.

## Hellfire and retail archive precedence

When present, optional Hellfire packs use the higher-priority-first order documented by the upstream maintainer: hfopt2, hfopt1, hfvoice, hfmusic, hfbarb, hfbard, hfmonk, then hellfire. User-configured overlays remain above these built-in packs. Patch_rt or Patch_sh is mounted above the base game archive but below the Hellfire pack family. See [the upstream resource-order discussion](https://github.com/diasurgical/DevilutionX/discussions/1262).

## Filename casing

The loader accepts common filename spellings such as DIABDAT.MPQ, diabdat.mpq, Diabdat.Mpq and equivalent casing for Hellfire archives. This matters on PS3, whose storage filesystem is case-sensitive even though original PC systems typically ignored filename case.

## Creating fingerprints for an unlisted disc or installer

Run the helper on MPQs extracted from a copy you are entitled to use:

    python3 Packaging/ps3/scan_mpq_fingerprints.py /path/to/game-data --output mpq-fingerprints.json

The helper streams each file and records exact byte size, MD5, SHA-1, SHA-256 and CRC32. It does not upload, alter or package the game files. Compare the resulting manifest against this database before proposing a new verified catalogue record. A release should not receive a special boot profile unless its actual data requires a different engine configuration.

## Extending the database

1. Identify the edition and source (disc label/revision, installer name/version, storefront and language).
2. Generate a checksum manifest for all MPQs in that set.
3. Verify the hashes against an independently obtained copy or a reliable primary source.
4. Add exact hashes and provenance to MpqArchiveFingerprints in Source/engine/mpq_profiles.hpp.
5. Add or update the distribution catalogue record. Add a distinct boot profile only when the release demonstrably requires different loading behavior.
6. Keep the generic fallback so valid new archives continue to work before they are added to the catalogue.
