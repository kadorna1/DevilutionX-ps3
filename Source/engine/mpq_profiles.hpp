#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace devilution {

enum class MpqReleaseProfileId : std::uint8_t {
	Unknown,
	DiabloUsCd100,
	DiabloEuMacGog,
};

enum class MpqFingerprintStatus : std::uint8_t {
	Verified,
	NotPublished,
	NotIndependentlyVerified,
};

struct MpqReleaseProfileConfig {
	MpqReleaseProfileId id;
	std::string_view name;
	// Skip the known-broken publisher logo movie only. Keep the game intro enabled.
	bool skipBrokenLogoMovie;
};

struct MpqArchiveFingerprint {
	std::string_view fileName;
	std::uint64_t sizeBytes; // Zero means no reliable exact size was published.
	std::string_view md5;
	std::string_view sha1;
	std::string_view sha256;
	std::string_view crc32;
	std::string_view crc64;
	std::string_view blake2sp;
	MpqReleaseProfileId profile;
	std::string_view provenance;
};

struct MpqDistributionRecord {
	std::string_view distribution;
	std::string_view media;
	std::string_view archives;
	MpqFingerprintStatus fingerprintStatus;
	MpqReleaseProfileId profile;
	std::string_view notes;
};

inline constexpr std::array<MpqReleaseProfileConfig, 3> MpqReleaseProfiles {{
	{ MpqReleaseProfileId::Unknown, "Unknown / unprofiled Diablo archive", false },
	{ MpqReleaseProfileId::DiabloUsCd100, "Original US retail CD 1.00", true },
	{ MpqReleaseProfileId::DiabloEuMacGog, "European / Mac / GOG archive family", false },
}};

/*
 * Distribution catalogue:
 * - Verified means there is an exact, source-backed whole-file fingerprint below.
 * - Unverified entries remain supported as input layouts, but use generic boot
 *   configuration unless their bytes match one of the verified fingerprints.
 */
inline constexpr std::array<MpqDistributionRecord, 9> MpqDistributionCatalogue {{
	{
		"Original US retail Diablo PC CD, version 1.00",
		"CD-ROM",
		"DIABDAT.MPQ",
		MpqFingerprintStatus::Verified,
		MpqReleaseProfileId::DiabloUsCd100,
		"Exact size and whole-file hashes are published in DevilutionX issue #57."
	},
	{
		"European / Mac retail Diablo archive family",
		"CD-ROM / Mac release",
		"DIABDAT.MPQ",
		MpqFingerprintStatus::Verified,
		MpqReleaseProfileId::DiabloEuMacGog,
		"Whole-file MD5 is recorded by the Freeablo maintainer in issue #427."
	},
	{
		"GOG enhanced Diablo digital release",
		"Digital download / offline installer",
		"DIABDAT.MPQ",
		MpqFingerprintStatus::Verified,
		MpqReleaseProfileId::DiabloEuMacGog,
		"The upstream maintainer confirms the GOG archive retains the EU/Mac MD5."
	},
	{
		"Blizzard Battle.net-compatible Diablo digital release",
		"Digital download",
		"DIABDAT.MPQ",
		MpqFingerprintStatus::NotIndependentlyVerified,
		MpqReleaseProfileId::Unknown,
		"Blizzard describes this as the original unchanged Battle.net-compatible edition. No separately verifiable archive digest was found; a byte-identical file still matches a verified profile."
	},
	{
		"Other regional PC CDs and budget re-releases",
		"CD-ROM",
		"DIABDAT.MPQ",
		MpqFingerprintStatus::NotIndependentlyVerified,
		MpqReleaseProfileId::Unknown,
		"No additional independently verifiable full-game DIABDAT hash was found. Archives are accepted with generic settings until fingerprinted."
	},
	{
		"Hellfire retail expansion CD",
		"CD-ROM",
		"hellfire.mpq; hfmonk.mpq; hfmusic.mpq; hfvoice.mpq",
		MpqFingerprintStatus::NotPublished,
		MpqReleaseProfileId::Unknown,
		"The standard four-archive set is supported; no complete reliable per-archive checksum set was found."
	},
	{
		"Hellfire GOG add-on",
		"Digital download / offline installer",
		"hellfire.mpq; hfmonk.mpq; hfmusic.mpq; hfvoice.mpq",
		MpqFingerprintStatus::NotPublished,
		MpqReleaseProfileId::Unknown,
		"The published GOG extraction procedure yields the standard four Hellfire archives; hashes are not published."
	},
	{
		"Hellfire Battle.net-compatible add-on",
		"Digital download",
		"hellfire.mpq; hfmonk.mpq; hfmusic.mpq; hfvoice.mpq",
		MpqFingerprintStatus::NotPublished,
		MpqReleaseProfileId::Unknown,
		"Accepted source according to the MPQ instructions; per-file archive digests are not published."
	},
	{
		"Diablo shareware / demo",
		"Shareware archive",
		"spawn.mpq",
		MpqFingerprintStatus::NotPublished,
		MpqReleaseProfileId::Unknown,
		"Supported as the reduced shareware game; no release profile is inferred from an unverified hash."
	},
}};

inline constexpr std::array<MpqArchiveFingerprint, 2> MpqArchiveFingerprints {{
	{
		"DIABDAT.MPQ",
		517501242ULL,
		"68f049866b44688a7af65ba766bef75a",
		"fe35cba676a0a194ccdf1d4357de49dff252fa40",
		"6bc120a603fdb383ff91f374e7db599765d6230d81e5a4c1b33f8254911774e7",
		"855951d6",
		"91eb406ca29379c7",
		"86f1016c2bfbae621eaf9a7789537154dcc20bc66238519cc883d0f6e816e40f",
		MpqReleaseProfileId::DiabloUsCd100,
		"DevilutionX issue #57: original US retail CD 1.00"
	},
	{
		"DIABDAT.MPQ",
		0,
		"011bc6518e6166206231080a4440b373",
		"",
		"",
		"",
		"",
		"",
		MpqReleaseProfileId::DiabloEuMacGog,
		"Freeablo issue #427: EU/Mac/GOG archive family"
	},
}};

} // namespace devilution
