#include "engine/assets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#else
#include <SDL.h>
#endif

#include "appfat.h"
#include "game_mode.hpp"
#include "mods/mod_identity.h"
#include "options.h"
#include "utils/file_util.h"
#include "utils/format.hpp"
#include "utils/log.hpp"
#include "utils/md5.hpp"
#include "utils/paths.h"
#include "utils/sdl_compat.h"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"
#include "utils/str_split.hpp"

#if defined(_WIN32) && !defined(__UWP__) && !defined(DEVILUTIONX_WINDOWS_NO_WCHAR)
#include <find_steam_game.h>
#endif

#ifndef UNPACKED_MPQS
#include "mpq/mpq_sdl_rwops.hpp"
#endif

namespace devilution {

std::vector<std::string> OverridePaths;
std::map<int, MpqArchiveT, std::greater<>> MpqArchives;
bool HasHellfireMpq;
bool IsAssetIntegrityViolated = false;

namespace {

#ifdef UNPACKED_MPQS
char *FindUnpackedMpqFile(char *relativePath)
{
	char *path = nullptr;
	for (const auto &[_, unpackedDir] : MpqArchives) {
		path = relativePath - unpackedDir.size();
		std::memcpy(path, unpackedDir.data(), unpackedDir.size());
		if (FileExists(path)) break;
		path = nullptr;
	}
	return path;
}
#else
bool IsDebugLogging()
{
	return IsLogLevel(LogCategory::Application, SDL_LOG_PRIORITY_DEBUG);
}

SDL_IOStream *OpenOptionalRWops(const std::string &path)
{
	// SDL always logs an error in Debug mode.
	// We check the file presence in Debug mode to avoid this.
	if (IsDebugLogging() && !FileExists(path.c_str()))
		return nullptr;
	return SDL_IOFromFile(path.c_str(), "rb");
};

bool FindMpqFile(std::string_view filename, MpqArchive **archive, uint32_t *hashIndex)
{
	for (auto &[_, mpqArchive] : MpqArchives) {
		uint32_t hash = mpqArchive.FindHash(filename);
		if (hash != UINT32_MAX) {
			*archive = &mpqArchive;
			*hashIndex = hash;
			return true;
		}
	}

	return false;
}

bool HasLogicAssetExtension(std::string_view filename)
{
	if (filename.size() < 4)
		return false;
	// Case-insensitive so that overrides on case-insensitive filesystems are caught.
	const std::string extension = AsciiStrToLower(filename.substr(filename.size() - 4));
	return extension == ".lua" || extension == ".tsv" || extension == ".sol";
}

/**
 * @brief Returns true if a logic-asset request could resolve to a loose file at `relativePath`.
 */
bool IsLoadableLogicAssetPath(std::string relativePath)
{
	// Asset requests use `\` as the separator.
	std::replace(relativePath.begin(), relativePath.end(), DirectorySeparator, '\\');
	AsciiStrToLower(relativePath);

	// Lua modules are requested by module name under `lua\` (see `LoadPackageData`), including
	// mod entry points such as `lua\mods\<name>\init.lua` that have no archive counterpart.
	if (relativePath.starts_with("lua\\") && relativePath.ends_with(".lua"))
		return true;

	// TSV and SOL requests use fixed canonical paths.
	MpqArchive *archive;
	uint32_t hashIndex;
	if (FindMpqFile(relativePath, &archive, &hashIndex))
		return true;

	std::string assetsDirPath = relativePath;
	std::replace(assetsDirPath.begin(), assetsDirPath.end(), '\\', DirectorySeparator);
	return FileExists((paths::AssetsPath() + assetsDirPath).c_str());
}

bool ContainsLogicAssets(const std::string &rootPath, const std::string &relativeDir, unsigned depth)
{
	constexpr unsigned MaxScanDepth = 16;
	if (depth > MaxScanDepth)
		return false;
	const std::string dirPath = rootPath + relativeDir;
	for (const std::string &filename : ListFiles(dirPath.c_str())) {
		if (HasLogicAssetExtension(filename) && IsLoadableLogicAssetPath(relativeDir + filename))
			return true;
	}
	for (const std::string &subdirName : ListDirectories(dirPath.c_str())) {
		if (ContainsLogicAssets(rootPath, StrCat(relativeDir, subdirName, DIRECTORY_SEPARATOR_STR), depth + 1))
			return true;
	}
	return false;
}

#endif

} // namespace

#ifdef UNPACKED_MPQS
AssetRef FindAsset(std::string_view filename)
{
	AssetRef result;
	if (filename.empty() || filename.back() == '\\')
		return result;
	result.path[0] = '\0';

	char pathBuf[AssetRef::PathBufSize];
	char *const pathEnd = pathBuf + AssetRef::PathBufSize;
	char *const relativePath = &pathBuf[AssetRef::PathBufSize - filename.size() - 1];
	*BufCopy(relativePath, filename) = '\0';

#if !defined(_WIN32) && !defined(__DJGPP__)
	std::replace(relativePath, pathEnd, '\\', '/');
#endif
	// Absolute path:
	if (relativePath[0] == '/') {
		if (FileExists(relativePath)) {
			*BufCopy(result.path, std::string_view(relativePath, filename.size())) = '\0';
		}
		return result;
	}

	// Unpacked MPQ file:
	char *const unpackedMpqPath = FindUnpackedMpqFile(relativePath);
	if (unpackedMpqPath != nullptr) {
		*BufCopy(result.path, std::string_view(unpackedMpqPath, pathEnd - unpackedMpqPath)) = '\0';
		return result;
	}

	// The `/assets` directory next to the devilutionx binary.
	const std::string &assetsPathPrefix = paths::AssetsPath();
	char *assetsPath = relativePath - assetsPathPrefix.size();
	std::memcpy(assetsPath, assetsPathPrefix.data(), assetsPathPrefix.size());
	if (FileExists(assetsPath)) {
		*BufCopy(result.path, std::string_view(assetsPath, pathEnd - assetsPath)) = '\0';
	}
	return result;
}
#else
AssetRef FindAsset(std::string_view filename)
{
	AssetRef result;
	if (filename.empty() || filename.back() == '\\')
		return result;

	std::string relativePath { filename };
#ifndef _WIN32
	std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
#endif

	if (relativePath[0] == '/') {
		result.directHandle = SDL_IOFromFile(relativePath.c_str(), "rb");
		if (result.directHandle != nullptr) {
			return result;
		}
	}

	// Files in the `PrefPath()` directory can override MPQ contents.
	{
		for (const auto &overridePath : OverridePaths) {
			const std::string path = overridePath + relativePath;
			result.directHandle = OpenOptionalRWops(path);
			if (result.directHandle != nullptr) {
				LogVerbose("Loaded MPQ file override: {}", path);
				result.isOverridden = true;
				return result;
			}
		}
	}

	// Look for the file in all the MPQ archives:
	if (FindMpqFile(filename, &result.archive, &result.hashIndex)) {
		result.filename = filename;
		return result;
	}

	// Load from the `/assets` directory next to the devilutionx binary.
	result.directHandle = OpenOptionalRWops(paths::AssetsPath() + relativePath);
	if (result.directHandle != nullptr)
		return result;

#if (defined(__ANDROID__) && !defined(TERMUX)) || defined(__APPLE__)
	// Fall back to the bundled assets on supported systems.
	// This is handled by SDL when we pass a relative path.
	if (!paths::AssetsPath().empty()) {
		std::string assetPath = "assets/" + relativePath;
		result.directHandle = SDL_IOFromFile(assetPath.c_str(), "rb");
		if (result.directHandle != nullptr)
			return result;
	}
#endif

	return result;
}
#endif

AssetHandle OpenAsset(AssetRef &&ref, bool threadsafe)
{
#ifdef UNPACKED_MPQS
	return AssetHandle { OpenFile(ref.path, "rb") };
#else
	if (ref.archive != nullptr)
		return AssetHandle { SDL_RWops_FromMpqFile(*ref.archive, ref.hashIndex, ref.filename, threadsafe) };
	if (ref.directHandle != nullptr) {
		// Transfer handle ownership:
		auto *handle = ref.directHandle;
		ref.directHandle = nullptr;
		return AssetHandle { handle };
	}
	return AssetHandle { nullptr };
#endif
}

AssetHandle OpenAsset(std::string_view filename, bool threadsafe)
{
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return AssetHandle {};
	return OpenAsset(std::move(ref), threadsafe);
}

AssetHandle OpenAsset(std::string_view filename, size_t &fileSize, bool threadsafe)
{
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return AssetHandle {};
	fileSize = ref.size();
	return OpenAsset(std::move(ref), threadsafe);
}

AssetHandle OpenIntegralAsset(AssetRef &&ref, bool threadsafe)
{
#ifndef UNPACKED_MPQS
	if (ref.isOverridden)
		IsAssetIntegrityViolated = true;
#endif
	return OpenAsset(std::move(ref), threadsafe);
}

AssetHandle OpenIntegralAsset(std::string_view filename, bool threadsafe)
{
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return AssetHandle {};
	return OpenIntegralAsset(std::move(ref), threadsafe);
}

AssetHandle OpenIntegralAsset(std::string_view filename, size_t &fileSize, bool threadsafe)
{
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return AssetHandle {};
	fileSize = ref.size();
	return OpenIntegralAsset(std::move(ref), threadsafe);
}

SDL_IOStream *OpenAssetAsSdlRwOps(std::string_view filename, bool threadsafe)
{
#ifdef UNPACKED_MPQS
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return nullptr;
	return SDL_IOFromFile(ref.path, "rb");
#else
	return OpenAsset(filename, threadsafe).release();
#endif
}

std::expected<AssetData, std::string> LoadAsset(std::string_view path)
{
	AssetRef ref = FindAsset(path);
	if (!ref.ok()) {
		return std::unexpected(StrCat("Asset not found: ", path));
	}

	const size_t size = ref.size();
	std::unique_ptr<char[]> data { new char[size] };

	AssetHandle handle = OpenAsset(std::move(ref));
	if (!handle.ok()) {
		return std::unexpected(StrCat("Failed to open asset: ", path, "\n", handle.error()));
	}

	if (size > 0 && !handle.read(data.get(), size)) {
		return std::unexpected(StrCat("Read failed: ", path, "\n", handle.error()));
	}

	return AssetData { std::move(data), size };
}

std::expected<AssetData, std::string> LoadIntegralAsset(std::string_view path)
{
	AssetRef ref = FindAsset(path);
	if (!ref.ok()) {
		return std::unexpected(StrCat("Asset not found: ", path));
	}

	const size_t size = ref.size();
	std::unique_ptr<char[]> data { new char[size] };

	AssetHandle handle = OpenIntegralAsset(std::move(ref));
	if (!handle.ok()) {
		return std::unexpected(StrCat("Failed to open asset: ", path, "\n", handle.error()));
	}

	if (size > 0 && !handle.read(data.get(), size)) {
		return std::unexpected(StrCat("Read failed: ", path, "\n", handle.error()));
	}

	return AssetData { std::move(data), size };
}

std::string FailedToOpenFileErrorMessage(std::string_view path, std::string_view error)
{
	return FormatRuntime(_("Failed to open file:\n{:s}\n\n{:s}\n\nThe MPQ file(s) might be damaged. Please check the file integrity."), path, error);
}

namespace {
#ifdef UNPACKED_MPQS
std::optional<std::string> FindUnpackedMpqData(std::span<const std::string> paths, std::string_view mpqName)
{
	std::string targetPath;
	for (const std::string &path : paths) {
		targetPath.clear();
		targetPath.reserve(path.size() + mpqName.size() + 1);
		targetPath.append(path).append(mpqName) += DirectorySeparator;
		if (FileExists(targetPath)) {
			LogVerbose("  Found unpacked MPQ directory: {}", targetPath);
			return targetPath;
		}
	}
	return std::nullopt;
}

bool FindMPQ(std::span<const std::string> paths, std::string_view mpqName)
{
	return FindUnpackedMpqData(paths, mpqName).has_value();
}

bool LoadMPQ(std::span<const std::string> paths, std::string_view mpqName, int priority)
{
	std::optional<std::string> mpqPath = FindUnpackedMpqData(paths, mpqName);
	if (!mpqPath.has_value()) {
		LogVerbose("Missing: {}", mpqName);
		return false;
	}
	MpqArchives[priority] = *std::move(mpqPath);
	return true;
}
#else
bool FindMPQ(std::span<const std::string> paths, std::string_view mpqName)
{
	std::string mpqAbsPath;
	for (const auto &path : paths) {
		mpqAbsPath = StrCat(path, mpqName, ".mpq");
		if (FileExists(mpqAbsPath)) {
			LogVerbose("  Found: {} in {}", mpqName, path);
			return true;
		}
	}

	return false;
}

bool LoadMPQ(std::span<const std::string> paths, std::string_view mpqName, int priority, std::string_view ext = ".mpq", std::string *loadedPath = nullptr)
{
	std::string mpqAbsPath;
	bool foundButFailed = false;
	for (const auto &path : paths) {
		bool openWithSDL = path.starts_with("sdl:/");
		mpqAbsPath = StrCat(path, mpqName, ext);
		if (!openWithSDL && !FileExists(mpqAbsPath))
			continue;
		std::expected<MpqArchive, std::string> archive = openWithSDL
		    ? MpqArchive::OpenWithSDL(mpqAbsPath.c_str() + 5)
		    : MpqArchive::Open(mpqAbsPath.c_str());
		if (!archive.has_value()) {
			if (!openWithSDL) {
				foundButFailed = true;
				LogError("Error {}: {}", archive.error(), mpqAbsPath);
			}
			continue;
		}
		LogVerbose("  Found: {} in {}", mpqName, path);
		auto [it, inserted] = MpqArchives.emplace(priority, *std::move(archive));
		if (!inserted) {
			LogError("MPQ with priority {} is already registered, skipping {}", priority, mpqName);
		}
		if (loadedPath != nullptr)
			*loadedPath = mpqAbsPath;
		return true;
	}
	if (!foundButFailed) {
		LogVerbose("Missing: {}", mpqName);
	}

	return false;
}
#endif


MpqReleaseProfileId DetectedMpqReleaseProfile = MpqReleaseProfileId::Unknown;

std::vector<std::string> GetMpqNameVariants(std::string_view name)
{
	std::vector<std::string> result;
	auto addUnique = [&result](std::string candidate) {
		if (std::find(result.begin(), result.end(), candidate) == result.end())
			result.push_back(std::move(candidate));
	};

	addUnique(std::string(name));
	addUnique(AsciiStrToLower(name));

	std::string upper(name);
	std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
		return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
	});
	addUnique(std::move(upper));

	std::string title = AsciiStrToLower(name);
	if (!title.empty() && title.front() >= 'a' && title.front() <= 'z')
		title.front() = static_cast<char>(title.front() - 'a' + 'A');
	addUnique(std::move(title));
	return result;
}

bool FindMPQCaseVariants(std::span<const std::string> paths, std::string_view name)
{
	const std::vector<std::string> names = GetMpqNameVariants(name);
#ifdef UNPACKED_MPQS
	for (const std::string &candidate : names) {
		if (FindMPQ(paths, candidate))
			return true;
	}
#else
	constexpr std::array<std::string_view, 4> Extensions { ".mpq", ".MPQ", ".Mpq", ".mPQ" };
	for (const std::string &candidate : names) {
		for (const std::string_view extension : Extensions) {
			for (const std::string &path : paths) {
				if (FileExists(StrCat(path, candidate, extension)))
					return true;
			}
		}
	}
#endif
	return false;
}

bool LoadMPQCaseVariants(
    std::span<const std::string> paths, std::string_view name, int priority
#ifndef UNPACKED_MPQS
    , std::string *loadedPath = nullptr
#endif
)
{
	const std::vector<std::string> names = GetMpqNameVariants(name);
#ifdef UNPACKED_MPQS
	for (const std::string &candidate : names) {
		if (LoadMPQ(paths, candidate, priority))
			return true;
	}
#else
	constexpr std::array<std::string_view, 4> Extensions { ".mpq", ".MPQ", ".Mpq", ".mPQ" };
	for (const std::string &candidate : names) {
		for (const std::string_view extension : Extensions) {
			if (LoadMPQ(paths, candidate, priority, extension, loadedPath))
				return true;
		}
	}
#endif
	return false;
}

#ifndef UNPACKED_MPQS
std::optional<MpqReleaseProfileId> IdentifyMpqReleaseProfile(const std::string &mpqPath)
{
	std::uintmax_t fileSize = 0;
	if (!GetFileSize(mpqPath.c_str(), &fileSize)) {
		LogWarn("MPQ database: unable to read size for {}", mpqPath);
		return std::nullopt;
	}

	bool hasCandidate = false;
	for (const MpqArchiveFingerprint &fingerprint : MpqArchiveFingerprints) {
		if (fingerprint.fileName != "DIABDAT.MPQ")
			continue;
		if (fingerprint.sizeBytes != 0 && fingerprint.sizeBytes != fileSize)
			continue;
		hasCandidate = true;
		break;
	}
	if (!hasCandidate) {
		LogInfo("MPQ database: no size-compatible DIABDAT fingerprint for {} bytes", fileSize);
		return std::nullopt;
	}

	// Do not allocate a whole-archive buffer: hash the 500 MiB retail MPQ in
	// 64 KiB chunks so this remains viable with the PS3's limited main memory.
	LogInfo("MPQ database: calculating whole-file MD5 for {} ({} bytes)", mpqPath, fileSize);
	const std::optional<std::string> digest = md5::CalculateFile(mpqPath.c_str());
	if (!digest.has_value()) {
		LogWarn("MPQ database: could not read DIABDAT.MPQ completely for fingerprinting");
		return std::nullopt;
	}

	for (const MpqArchiveFingerprint &fingerprint : MpqArchiveFingerprints) {
		if (fingerprint.fileName != "DIABDAT.MPQ" || fingerprint.md5.empty())
			continue;
		if (fingerprint.sizeBytes != 0 && fingerprint.sizeBytes != fileSize)
			continue;
		if (*digest == fingerprint.md5) {
			LogInfo("MPQ database: matched {} ({})", fingerprint.fileName, fingerprint.provenance);
			return fingerprint.profile;
		}
	}

	LogInfo("MPQ database: unrecognized DIABDAT.MPQ size={} MD5={}", fileSize, *digest);
	return std::nullopt;
}

bool LoadRetailPatchArchive(std::span<const std::string> paths)
{
	constexpr int RetailPatchMpqPriority = 1050;
	const bool loaded = LoadMPQCaseVariants(paths, "Patch_rt", RetailPatchMpqPriority);
	if (loaded)
		LogInfo("MPQ database: mounted optional Patch_rt.mpq at priority {}", RetailPatchMpqPriority);
	return loaded;
}
#endif

std::vector<std::string> GetMPQSearchPaths()
{
	std::vector<std::string> paths;
	paths.push_back(paths::BasePath());
	paths.push_back(paths::PrefPath());
	if (paths[0] == paths[1])
		paths.pop_back();
	paths.push_back(paths::ConfigPath());
	if (paths[0] == paths[1] || (paths.size() == 3 && (paths[0] == paths[2] || paths[1] == paths[2])))
		paths.pop_back();

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__ANDROID__) && !defined(__DJGPP__)
	// `XDG_DATA_HOME` is usually the root path of `paths::PrefPath()`, so we only
	// add `XDG_DATA_DIRS`.
	const char *xdgDataDirs = std::getenv("XDG_DATA_DIRS");
	if (xdgDataDirs != nullptr) {
		for (const std::string_view path : SplitByChar(xdgDataDirs, ':')) {
			std::string fullPath(path);
			if (!path.empty() && path.back() != '/')
				fullPath += '/';
			fullPath.append("diasurgical/devilutionx/");
			paths.push_back(std::move(fullPath));
		}
	} else {
		paths.emplace_back("/usr/local/share/diasurgical/devilutionx/");
		paths.emplace_back("/usr/share/diasurgical/devilutionx/");
	}
#elif defined(TERMUX)
#ifdef CMAKE_INSTALL_PREFIX
	paths.emplace_back(CMAKE_INSTALL_PREFIX "/share/diasurgical/devilutionx/");
#else
	paths.emplace_back("/usr/share/diasurgical/devilutionx/");
#endif
#elif defined(__3DS__) || defined(__SWITCH__)
	paths.emplace_back("romfs:/");
#elif defined(NXDK)
	paths.emplace_back("D:\\");
#elif defined(_WIN32) && !defined(__UWP__) && !defined(DEVILUTIONX_WINDOWS_NO_WCHAR)
	char gogpath[_FSG_PATH_MAX];
	fsg_get_gog_game_path(gogpath, "1412601690");
	if (strlen(gogpath) > 0) {
		paths.emplace_back(std::string(gogpath) + "/");
		paths.emplace_back(std::string(gogpath) + "/hellfire/");
	}
#endif

	if (paths.empty() || !paths.back().empty()) {
		paths.emplace_back(); // PWD
	}

	if (IsLogLevel(LogCategory::Application, SDL_LOG_PRIORITY_VERBOSE)) {
		LogVerbose("Paths:\n    base: {}\n    pref: {}\n  config: {}\n  assets: {}",
		    paths::BasePath(), paths::PrefPath(), paths::ConfigPath(), paths::AssetsPath());

		std::string message;
		for (std::size_t i = 0; i < paths.size(); ++i) {
			message.append(StrCat("\n", LeftPad(i + 1, 6, ' '), ". '", paths[i], "'"));
		}
		LogVerbose("MPQ search paths:{}", message);
	}

	return paths;
}

#ifndef UNPACKED_MPQS
std::optional<std::string> NormalizeAdditionalMpqName(std::string_view configuredName)
{
	while (!configuredName.empty() && (configuredName.front() == ' ' || configuredName.front() == '\t'))
		configuredName.remove_prefix(1);
	while (!configuredName.empty() && (configuredName.back() == ' ' || configuredName.back() == '\t'))
		configuredName.remove_suffix(1);

	if (configuredName.size() >= 4 && AsciiStrToLower(configuredName.substr(configuredName.size() - 4)) == ".mpq")
		configuredName.remove_suffix(4);

	if (configuredName.empty() || configuredName.size() > 64)
		return std::nullopt;

	for (const unsigned char character : configuredName) {
		const bool isAlphaNumeric = (character >= 'a' && character <= 'z')
		    || (character >= 'A' && character <= 'Z')
		    || (character >= '0' && character <= '9');
		if (!isAlphaNumeric && character != '_' && character != '-')
			return std::nullopt;
	}

	return std::string(configuredName);
}

void LoadAdditionalMpqOverlays(const std::vector<std::string> &paths)
{
	constexpr int FirstAdditionalMpqPriority = 9150;
	constexpr int LastAdditionalMpqPriority = 9199;
	int priority = FirstAdditionalMpqPriority;
	std::vector<std::string> attemptedNames;

	auto loadOne = [&](std::string_view configuredName) {
		const std::optional<std::string> normalizedName = NormalizeAdditionalMpqName(configuredName);
		if (!normalizedName) {
			if (!configuredName.empty())
				LogWarn("MPQ overlay: ignoring unsafe archive name '{}'", configuredName);
			return;
		}

		const std::string dedupeKey = AsciiStrToLower(*normalizedName);
		if (std::find(attemptedNames.begin(), attemptedNames.end(), dedupeKey) != attemptedNames.end())
			return;
		attemptedNames.push_back(dedupeKey);

		if (priority > LastAdditionalMpqPriority) {
			LogWarn("MPQ overlay: reached the limit of {} configured archives", LastAdditionalMpqPriority - FirstAdditionalMpqPriority + 1);
			return;
		}

		LogInfo("MPQ overlay: searching for '{}'", *normalizedName);
		std::string loadedPath;
		bool loaded = LoadMPQ(paths, *normalizedName, priority, ".mpq", &loadedPath);
		if (!loaded)
			loaded = LoadMPQ(paths, *normalizedName, priority, ".MPQ", &loadedPath);

		// Original CD data commonly uses uppercase 8.3-style filenames. Try that
		// spelling too, since the PS3 filesystem may be case-sensitive.
		if (!loaded) {
			std::string upperName = *normalizedName;
			std::transform(upperName.begin(), upperName.end(), upperName.begin(), [](unsigned char character) {
				return character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : static_cast<char>(character);
			});
			if (upperName != *normalizedName) {
				loaded = LoadMPQ(paths, upperName, priority, ".MPQ", &loadedPath);
				if (!loaded)
					loaded = LoadMPQ(paths, upperName, priority, ".mpq", &loadedPath);
			}
		}

		if (!loaded) {
			LogWarn("MPQ overlay: configured archive '{}' was not found or could not be opened", *normalizedName);
			return;
		}

		LogInfo("MPQ overlay: loaded '{}' at priority {}", loadedPath, priority);
		++priority;
	};

	for (const std::string &configuredEntry : GetOptions().additionalMpqs) {
		std::string_view remaining = configuredEntry;
		while (true) {
			const std::size_t separator = remaining.find(',');
			const std::string_view item = separator == std::string_view::npos ? remaining : remaining.substr(0, separator);
			loadOne(item);
			if (separator == std::string_view::npos)
				break;
			remaining.remove_prefix(separator + 1);
		}
	}
}
#endif

} // namespace

const MpqReleaseProfileConfig &GetDetectedMpqReleaseProfile()
{
	for (const MpqReleaseProfileConfig &profile : MpqReleaseProfiles) {
		if (profile.id == DetectedMpqReleaseProfile)
			return profile;
	}
	return MpqReleaseProfiles.front();
}

void LoadCoreArchives()
{
	auto paths = GetMPQSearchPaths();

#if !(defined(__ANDROID__) && !defined(TERMUX)) && !defined(__APPLE__) && !defined(__3DS__) && !defined(__SWITCH__)
	// Load devilutionx.mpq first to get the font file for error messages
#ifdef __DJGPP__
	LoadMPQ(paths, "devx", DevilutionXMpqPriority);
#else
	LoadMPQ(paths, "devilutionx", DevilutionXMpqPriority);
#endif
#endif
	LoadMPQ(paths, "fonts", FontMpqPriority); // Extra fonts
	HasHellfireMpq = FindMPQCaseVariants(paths, "hellfire");
}

void LoadLanguageArchive()
{
	MpqArchives.erase(LangMpqPriority);
	const std::string_view code = GetLanguageCode();
	if (code != "en") {
		LoadMPQ(GetMPQSearchPaths(), code, LangMpqPriority);
	}
}

void LoadGameArchives()
{
	const std::vector<std::string> paths = GetMPQSearchPaths();
	bool haveDiabdat = false;
	bool haveSpawn = false;
	DetectedMpqReleaseProfile = MpqReleaseProfileId::Unknown;

#ifndef UNPACKED_MPQS
	std::string diabdatPath;
	haveDiabdat = LoadMPQCaseVariants(paths, "DIABDAT", MainMpqPriority, &diabdatPath);
	if (haveDiabdat) {
		const std::optional<MpqReleaseProfileId> profile = IdentifyMpqReleaseProfile(diabdatPath);
		if (profile.has_value()) {
			DetectedMpqReleaseProfile = *profile;
			LogInfo("MPQ database: detected boot profile '{}'", GetDetectedMpqReleaseProfile().name);
		} else {
			LogInfo("MPQ database: generic boot profile selected for this DIABDAT.MPQ");
		}
	}
#else
	haveDiabdat = LoadMPQCaseVariants(paths, "DIABDAT", MainMpqPriority);
#endif

	if (!haveDiabdat)
		gbIsSpawn = haveSpawn = LoadMPQCaseVariants(paths, "spawn", MainMpqPriority);

#ifndef UNPACKED_MPQS
	// Blizzard retail updates are separate overlays and do not rewrite DIABDAT.MPQ.
	LoadRetailPatchArchive(paths);
#endif

	// Optional high-priority archive containing browser-generated H.264/PCM
	// sidecars for PS3 Cell VDEC playback. Original game MPQs remain untouched.
	LoadMPQ(paths, "devilutionx-vdec", 1100);

	if (!HeadlessMode) {
		if (!haveDiabdat && !haveSpawn) {
			LogError("{}", SDL_GetError());
			InsertCDDlg(_("diabdat.mpq or spawn.mpq"));
		}
	}

	if (forceHellfire && !HasHellfireMpq) {
#ifdef UNPACKED_MPQS
		InsertCDDlg("hellfire");
#else
		InsertCDDlg("hellfire.mpq");
#endif
	}

#ifndef UNPACKED_MPQS
	// Optional character packs from some Hellfire releases; accept CD filename casing.
	LoadMPQCaseVariants(paths, "hfbard", 8110);
	LoadMPQCaseVariants(paths, "hfbarb", 8120);

	// Configured archives sit above base/Hellfire/language MPQs and below fonts/mods.
	LoadAdditionalMpqOverlays(paths);
#endif
}

void LoadHellfireArchives()
{
	const std::vector<std::string> paths = GetMPQSearchPaths();
	LoadMPQCaseVariants(paths, "hellfire", 8000);

#ifdef UNPACKED_MPQS
	const std::string &hellfireDataPath = MpqArchives.at(8000);
	const bool hasMonk = FileExists(hellfireDataPath + "plrgfx/monk/mha/mhaas.clx");
	const bool hasMusic = FileExists(hellfireDataPath + "music/dlvlf.wav")
	    || FileExists(hellfireDataPath + "music/dlvlf.mp3");
	const bool hasVoice = FileExists(hellfireDataPath + "sfx/hellfire/cowsut1.wav")
	    || FileExists(hellfireDataPath + "sfx/hellfire/cowsut1.mp3");
#else
	const bool hasMonk = LoadMPQCaseVariants(paths, "hfmonk", 8100);
	const bool hasMusic = LoadMPQCaseVariants(paths, "hfmusic", 8200);
	const bool hasVoice = LoadMPQCaseVariants(paths, "hfvoice", 8500);
#endif

	if (!hasMonk || !hasMusic || !hasVoice)
		DisplayFatalErrorAndExit(_("Some Hellfire MPQs are missing"), _("Not all Hellfire MPQs were found.\nPlease copy all the hf*.mpq files."));
}

void UnloadModArchives()
{
	OverridePaths.clear();

#ifndef UNPACKED_MPQS
	for (auto it = MpqArchives.begin(); it != MpqArchives.end();) {
		if ((it->first >= 8000 && it->first < 9000) || it->first >= 10000) {
			it = MpqArchives.erase(it); // erase returns the next valid iterator
		} else {
			++it;
		}
	}
#endif
}

#ifndef UNPACKED_MPQS
namespace {

std::expected<ModManifest, int32_t> ReadPackedModManifestFrom(MpqArchive &archive)
{
	constexpr std::string_view ManifestName = "manifest.ini";
	if (!archive.HasFile(ManifestName))
		return std::unexpected(0);

	size_t fileSize = 0;
	int32_t error = 0;
	std::unique_ptr<std::byte[]> data = archive.ReadFile(ManifestName, fileSize, error);
	if (data == nullptr)
		return std::unexpected(error);

	return ParseModManifest(std::string_view(reinterpret_cast<const char *>(data.get()), fileSize));
}

std::expected<ModManifest, int32_t> ReadPackedModManifest(std::span<const std::string> paths, std::string_view modname)
{
	const std::string mpqName = StrCat("mods" DIRECTORY_SEPARATOR_STR, modname);
	std::string mpqAbsPath;
	for (const std::string &path : paths) {
		bool openWithSDL = path.starts_with("sdl:/");
		mpqAbsPath = StrCat(path, mpqName, ".mpq");
		if (!openWithSDL && !FileExists(mpqAbsPath))
			continue;
		std::expected<MpqArchive, std::string> archive = openWithSDL
		    ? MpqArchive::OpenWithSDL(mpqAbsPath.c_str() + 5)
		    : MpqArchive::Open(mpqAbsPath.c_str());
		if (!archive.has_value())
			return std::unexpected(0);
		return ReadPackedModManifestFrom(*archive);
	}
	return std::unexpected(0);
}

// Reads the mod's `manifest.ini` from its own archive (registered at `priority`) and
// attaches the parsed metadata. Deliberately bypasses the override-capable `FindAsset`
// pipeline so the manifest is exactly the one covered by the mod's identifying hash.
void ReadLoadedModManifest(ModIdentifier &mod, int priority)
{
	auto it = MpqArchives.find(priority);
	if (it == MpqArchives.end())
		return;
	MpqArchive &archive = it->second;
	std::expected<ModManifest, int32_t> manifest = ReadPackedModManifestFrom(archive);
	if (manifest.has_value())
		mod.manifest = std::move(*manifest);
	else if (manifest.error() != 0)
		LogError("Failed to read manifest from mod {}: error {}", mod.name, manifest.error());
}

// Reads the `requires` list from a packed mod's manifest without registering the archive, so
// it can inform load ordering before the real load pass assigns priorities. Returns empty if
// the mod has no packed archive, no manifest, or an unreadable one.
std::vector<std::string> ReadPackedModRequiredMods(std::span<const std::string> paths, std::string_view modname)
{
	std::expected<ModManifest, int32_t> manifest = ReadPackedModManifest(paths, modname);
	if (!manifest.has_value())
		return {};
	return std::move(manifest->requiredMods);
}

} // namespace
#endif

ModManifest ReadModManifestByName(std::string_view name)
{
	constexpr std::string_view ManifestName = "manifest.ini";

	// Matches the OverridePaths registered per mod by LoadModArchives()
	const std::array<std::string, 2> unpackedSearchPaths {
		paths::PrefPath(),
		paths::BasePath(),
	};

	// Loose mod directory (also how UNPACKED_MPQS builds ship their mods).
	for (const std::string &path : unpackedSearchPaths) {
		const std::string manifestPath = StrCat(path, "mods" DIRECTORY_SEPARATOR_STR, name, DIRECTORY_SEPARATOR_STR, ManifestName);
		FILE *file = OpenFile(manifestPath.c_str(), "rb");
		if (file == nullptr)
			continue;
		uintmax_t size = 0;
		std::string contents;
		if (GetFileSize(manifestPath.c_str(), &size) && size > 0) {
			contents.resize(static_cast<size_t>(size));
			if (std::fread(contents.data(), 1, contents.size(), file) != contents.size())
				contents.clear();
		}
		std::fclose(file);
		if (!contents.empty())
			return ParseModManifest(contents);
	}

#ifndef UNPACKED_MPQS
	// Packed mod archive.
	std::vector<std::string> packedSearchPaths = GetMPQSearchPaths();
#if (defined(__ANDROID__) && !defined(TERMUX)) || defined(__APPLE__)
	packedSearchPaths.emplace_back("sdl:/");
#endif
	std::expected<ModManifest, int32_t> manifest = ReadPackedModManifest(packedSearchPaths, name);
	if (manifest.has_value())
		return std::move(*manifest);
#endif

	return {};
}

void LoadModArchives(std::span<const std::string_view> modnames)
{
	ClearModIdentifiers();

#ifdef UNPACKED_MPQS
	const std::vector<std::string_view> activeMods(modnames.begin(), modnames.end());
#else
	// Order the active mods so every mod's declared requiredMods load first: a dependency gets a
	// lower priority (loaded earlier, forms the base) and its dependents get higher priorities
	// (loaded later, override it). `requires` is read from each packed mod's manifest in a
	// pre-pass, because the real load below fixes priorities in iteration order.
	const std::vector<std::string> searchPaths = GetMPQSearchPaths();
	std::vector<ModDependency> deps;
	deps.reserve(modnames.size());
	for (const std::string_view modname : modnames)
		deps.push_back(ModDependency { std::string(modname), ReadPackedModRequiredMods(searchPaths, modname) });
	const std::vector<std::string> orderedNames = OrderModsByDependencies(deps);
	const std::vector<std::string_view> activeMods(orderedNames.begin(), orderedNames.end());

	// Tracks, per active mod, whether a loose override directory was found. Such mods carry no
	// identifier (they are gated by the loose-asset integrity check) and are not built-in.
	std::vector<bool> hasLooseOverride(activeMods.size(), false);
#endif

	std::string targetPath;
	for (size_t i = 0; i < activeMods.size(); ++i) {
		const std::string_view modname = activeMods[i];
		targetPath = StrCat(paths::PrefPath(), "mods" DIRECTORY_SEPARATOR_STR, modname, DIRECTORY_SEPARATOR_STR);
		if (DirectoryExists(targetPath)) {
			OverridePaths.emplace_back(targetPath);
#ifndef UNPACKED_MPQS
			hasLooseOverride[i] = true;
#endif
		}
		targetPath = StrCat(paths::BasePath(), "mods" DIRECTORY_SEPARATOR_STR, modname, DIRECTORY_SEPARATOR_STR);
		if (DirectoryExists(targetPath)) {
			OverridePaths.emplace_back(targetPath);
#ifndef UNPACKED_MPQS
			hasLooseOverride[i] = true;
#endif
		}
	}
	OverridePaths.emplace_back(paths::PrefPath());

	int priority = 10000;
	auto paths = GetMPQSearchPaths();
#if (defined(__ANDROID__) && !defined(TERMUX)) || defined(__APPLE__)
	paths.emplace_back("sdl:/");
#endif
	for (size_t i = 0; i < activeMods.size(); ++i) {
		const std::string_view modname = activeMods[i];
		const std::string mpqName = StrCat("mods" DIRECTORY_SEPARATOR_STR, modname);
#ifdef UNPACKED_MPQS
		LoadMPQ(paths, mpqName, priority);
#else
		std::string loadedPath;
		if (LoadMPQ(paths, mpqName, priority, ".mpq", &loadedPath)) {
			// A packed mod: identify it by the hash of its MPQ bytes. A local packed mod
			// deliberately shadows any built-in of the same name, so it is treated as external.
			size_t offset = loadedPath.starts_with("sdl:/") ? 5 : 0;
			ModIdentifier &mod = RegisterPackedModIdentifier(modname, loadedPath.c_str() + offset);
			ReadLoadedModManifest(mod, priority);
		} else if (!hasLooseOverride[i]) {
			// Neither a packed MPQ nor a loose override directory: this mod resolves purely
			// from core archives (a built-in), so it is provenance-whitelisted.
			RegisterBuiltinModIdentifier(modname);
		}
#endif
		priority++;
	}
}

bool HasLooseLogicAssets()
{
#ifdef UNPACKED_MPQS
	// Unpacked builds do not consult `OverridePaths` and have no override integrity tracking.
	return false;
#else
	for (const std::string &overridePath : OverridePaths) {
		if (ContainsLogicAssets(overridePath, /*relativeDir=*/ {}, /*depth=*/0))
			return true;
	}
	return false;
#endif
}

} // namespace devilution
