#include "platform/ps3/keyboard.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "engine/dx.h"
#include <sys/memory.h>
#include <sysutil/osk.h>
#include <sysutil/sysutil.h>

#include "utils/log.hpp"
#include "utils/utf8.hpp"

namespace devilution {
namespace {

constexpr std::size_t OskBufferCapacity = 256;
constexpr std::size_t OskContainerBytes = 4U * 1024U * 1024U;

std::array<uint16_t, OskBufferCapacity> OskGuideText {};
std::array<uint16_t, OskBufferCapacity> OskInitialText {};
std::array<uint16_t, OskBufferCapacity> OskOutputText {};
oskCallbackReturnParam OskOutput {};
bool OskUnloaded;
bool OskUnloadRequested;
s32 OskUnloadStatus;

std::size_t Utf8ToUtf16(std::string_view source, uint16_t *destination, std::size_t capacity)
{
	std::size_t written = 0;
	while (!source.empty() && written + 1 < capacity) {
		const char32_t codePoint = ConsumeFirstUtf8CodePoint(&source);
		if (codePoint <= 0xFFFF) {
			destination[written++] = static_cast<uint16_t>(codePoint);
		} else {
			if (written + 2 >= capacity)
				break;
			const char32_t value = codePoint - 0x10000;
			destination[written++] = static_cast<uint16_t>(0xD800 + (value >> 10));
			destination[written++] = static_cast<uint16_t>(0xDC00 + (value & 0x3FF));
		}
	}
	destination[written] = 0;
	return written;
}

std::string Utf16ToUtf8(const uint16_t *source)
{
	std::string result;
	for (std::size_t i = 0; source[i] != 0; ++i) {
		char32_t codePoint = source[i];
		if (codePoint >= 0xD800 && codePoint <= 0xDBFF && source[i + 1] >= 0xDC00 && source[i + 1] <= 0xDFFF) {
			codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (source[++i] - 0xDC00);
		}
		AppendUtf8(codePoint, result);
	}
	return result;
}

void RequestOskUnload()
{
	if (OskUnloadRequested)
		return;
	OskUnloadRequested = true;
	OskUnloadStatus = oskUnloadAsync(&OskOutput);
	if (OskUnloadStatus != 0)
		OskUnloaded = true;
}

void OskSystemCallback(u64 status, u64 /*param*/, void * /*userData*/)
{
	switch (status) {
	case SYSUTIL_OSK_LOADED:
		LogInfo("PS3 OSK: system keyboard loaded");
		break;
	case SYSUTIL_OSK_DONE:
		LogInfo("PS3 OSK: input completed");
		RequestOskUnload();
		break;
	case SYSUTIL_OSK_INPUT_CANCELED:
		LogInfo("PS3 OSK: input canceled");
		(void)oskAbort();
		RequestOskUnload();
		break;
	case SYSUTIL_OSK_UNLOADED:
		LogInfo("PS3 OSK: system keyboard unloaded");
		OskUnloaded = true;
		break;
	default:
		break;
	}
}

} // namespace

std::optional<std::string> PS3GetTextInput(
    std::string_view guideText,
    std::string_view initialText,
    std::size_t maxUtf8Bytes,
    bool numericOnly)
{
	if (maxUtf8Bytes == 0)
		return std::nullopt;

	OskGuideText.fill(0);
	OskInitialText.fill(0);
	OskOutputText.fill(0);
	Utf8ToUtf16(guideText, OskGuideText.data(), OskGuideText.size());
	Utf8ToUtf16(initialText, OskInitialText.data(), OskInitialText.size());

	oskInputFieldInfo inputInfo {};
	inputInfo.message = OskGuideText.data();
	inputInfo.startText = OskInitialText.data();
	inputInfo.maxLength = static_cast<s32>(std::min(maxUtf8Bytes, OskBufferCapacity - 1));

	oskParam parameters {};
	parameters.allowedPanels = numericOnly ? OSK_PANEL_TYPE_NUMERAL : OSK_PANEL_TYPE_DEFAULT;
	parameters.firstViewPanel = parameters.allowedPanels;
	parameters.controlPoint = { 0.0F, 0.0F };
	parameters.prohibitFlags = OSK_PROHIBIT_RETURN;
	if (numericOnly)
		parameters.prohibitFlags |= OSK_PROHIBIT_SPACE;

	OskOutput = {};
	OskOutput.res = OSK_OK;
	OskOutput.len = static_cast<s32>(OskBufferCapacity - 1);
	OskOutput.str = OskOutputText.data();
	OskUnloaded = false;
	OskUnloadRequested = false;
	OskUnloadStatus = 0;

	sys_mem_container_t container {};
	const s32 containerResult = sysMemContainerCreate(&container, OskContainerBytes);
	if (containerResult != 0) {
		LogError("PS3 OSK: sysMemContainerCreate failed: {:#x}", static_cast<unsigned>(containerResult));
		return std::nullopt;
	}

	const s32 callbackResult = sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, OskSystemCallback, nullptr);
	if (callbackResult != 0) {
		LogError("PS3 OSK: sysUtilRegisterCallback failed: {:#x}", static_cast<unsigned>(callbackResult));
		(void)sysMemContainerDestroy(container);
		return std::nullopt;
	}

	(void)oskSetInitialInputDevice(OSK_DEVICE_PAD);
	(void)oskSetDeviceMask(OSK_DEVICE_MASK_PAD);
	(void)oskSetKeyLayoutOption(numericOnly ? OSK_10KEY_PANEL : OSK_FULLKEY_PANEL);
	(void)oskSetLayoutMode(OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER | OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER);

	const s32 loadResult = oskLoadAsync(container, &parameters, &inputInfo);
	if (loadResult != 0) {
		LogError("PS3 OSK: oskLoadAsync failed: {:#x}", static_cast<unsigned>(loadResult));
		(void)sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
		(void)sysMemContainerDestroy(container);
		return std::nullopt;
	}

	LogInfo("PS3 OSK: waiting for system keyboard");
	while (!OskUnloaded) {
		// PSL1GHT's OSK is composited by the system utility layer during a
		// framebuffer flip. Servicing callbacks alone does not make it visible.
		(void)sysUtilCheckCallback();
		RenderPresent();
	}
	(void)sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
	(void)sysMemContainerDestroy(container);

	if (OskUnloadStatus != 0) {
		LogError("PS3 OSK: oskUnloadAsync failed: {:#x}", static_cast<unsigned>(OskUnloadStatus));
		return std::nullopt;
	}
	if (OskOutput.res != OSK_OK)
		return std::nullopt;

	const std::string text = Utf16ToUtf8(OskOutputText.data());
	const std::string_view bounded = TruncateUtf8(text, maxUtf8Bytes);
	LogInfo("PS3 OSK: accepted {} UTF-8 bytes", bounded.size());
	return std::string(bounded);
}

} // namespace devilution
