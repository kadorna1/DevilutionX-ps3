#include <memory>
#include <optional>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "controls/input.h"
#include "controls/menu_controls.h"
#include "discord/discord.h"
#include "engine/clx_sprite.hpp"
#include "engine/load_clx.hpp"
#include "engine/load_pcx.hpp"
#include "engine/point.hpp"
#include "utils/algorithm/container.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/sdl_geometry.h"
#include "utils/ui_fwd.h"
#include "utils/log.hpp"

namespace devilution {
namespace {

OptionalOwnedClxSpriteList DiabloTitleLogo;

std::vector<std::unique_ptr<UiItemBase>> vecTitleScreen;

void TitleLoad()
{
#ifdef __PS3__
	LogInfo("PS3 title: TitleLoad begin");
#endif
	ArtBackgroundWidescreen = LoadOptionalClx("ui_art\\hf_titlew.clx");
#ifdef __PS3__
	LogInfo("PS3 title: widescreen title asset lookup complete found={}", ArtBackgroundWidescreen.has_value());
#endif
	if (ArtBackgroundWidescreen.has_value()) {
#ifdef __PS3__
		LogInfo("PS3 title: widescreen background art load begin");
#endif
		LoadBackgroundArt("ui_art\\hf_logo1", 16);
#ifdef __PS3__
		LogInfo("PS3 title: widescreen background art load complete");
#endif
	} else {
#ifdef __PS3__
		LogInfo("PS3 title: standard background art load begin");
#endif
		LoadBackgroundArt("ui_art\\title");
#ifdef __PS3__
		LogInfo("PS3 title: standard background art load complete; logo PCX load begin");
#endif
		DiabloTitleLogo = LoadPcxSpriteList("ui_art\\logo", /*numFrames=*/15, /*transparentColor=*/250);
#ifdef __PS3__
		LogInfo("PS3 title: logo PCX load complete available={}", DiabloTitleLogo.has_value());
#endif
	}
#ifdef __PS3__
	LogInfo("PS3 title: TitleLoad complete");
#endif
}

void TitleFree()
{
#ifdef __PS3__
	LogInfo("PS3 title: TitleFree begin");
#endif
	ArtBackground = std::nullopt;
	ArtBackgroundWidescreen = std::nullopt;
	DiabloTitleLogo = std::nullopt;

	vecTitleScreen.clear();
#ifdef __PS3__
	LogInfo("PS3 title: TitleFree complete");
#endif
}

} // namespace

void UiTitleDialog()
{
#ifdef __PS3__
	LogInfo("PS3 title: UiTitleDialog entered");
	LogInfo("PS3 title: TitleLoad begin");
#endif
	TitleLoad();
#ifdef __PS3__
	LogInfo("PS3 title: TitleLoad returned");
#endif
	const Point uiPosition = GetUIRectangle().position;
#ifdef __PS3__
	LogInfo("PS3 title: UI rectangle position x={} y={}", uiPosition.x, uiPosition.y);
#endif
	if (ArtBackgroundWidescreen.has_value()) {
#ifdef __PS3__
		LogInfo("PS3 title: building widescreen title elements");
#endif
		const SDL_Rect rect = MakeSdlRect(0, uiPosition.y, 0, 0);
		if (ArtBackgroundWidescreen)
			vecTitleScreen.push_back(std::make_unique<UiImageClx>((*ArtBackgroundWidescreen)[0], rect, UiFlags::AlignCenter));
		vecTitleScreen.push_back(std::make_unique<UiImageAnimatedClx>(*ArtBackground, rect, UiFlags::AlignCenter));
	} else {
#ifdef __PS3__
		LogInfo("PS3 title: adding standard background begin");
#endif
		UiAddBackground(&vecTitleScreen);
#ifdef __PS3__
		LogInfo("PS3 title: adding standard background complete; adding animated logo");
#endif

		vecTitleScreen.push_back(std::make_unique<UiImageAnimatedClx>(
		    *DiabloTitleLogo, MakeSdlRect(0, uiPosition.y + 182, 0, 0), UiFlags::AlignCenter));
#ifdef __PS3__
		LogInfo("PS3 title: animated logo element added; adding copyright text");
#endif

		const SDL_Rect rect = MakeSdlRect(uiPosition.x, uiPosition.y + 410, 640, 26);
		vecTitleScreen.push_back(std::make_unique<UiArtText>(_("Copyright © 1996-2001 Blizzard Entertainment").data(), rect, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver));
	}
#ifdef __PS3__
	LogInfo("PS3 title: UI elements ready count={}", vecTitleScreen.size());
#endif

	bool endMenu = false;
	const Uint32 timeOut = SDL_GetTicks() + 7000;
#ifdef __PS3__
	LogInfo("PS3 title: dialog loop begin timeout_ms=7000");
#endif

	SDL_Event event;
	bool firstFrame = true;
	while (!endMenu && SDL_GetTicks() < timeOut) {
#ifdef __PS3__
		if (firstFrame)
			LogInfo("PS3 title: first UiRenderItems begin");
#endif
		UiRenderItems(vecTitleScreen);
#ifdef __PS3__
		if (firstFrame)
			LogInfo("PS3 title: first UiRenderItems complete");
#endif
		UiFadeIn();
#ifdef __PS3__
		if (firstFrame)
			LogInfo("PS3 title: first UiFadeIn complete");
#endif

		discord_manager::UpdateMenu();
#ifdef __PS3__
		if (firstFrame)
			LogInfo("PS3 title: first discord menu update complete");
#endif
		firstFrame = false;

		while (PollEvent(&event)) {
			if (c_any_of(GetMenuActions(event), [](MenuAction menuAction) { return menuAction != MenuAction_NONE; })) {
#ifdef __PS3__
				LogInfo("PS3 title: menu action ended title dialog");
#endif
				endMenu = true;
				break;
			}
			switch (event.type) {
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
#ifdef __PS3__
				LogInfo("PS3 title: keyboard/mouse event ended title dialog type={}", static_cast<int>(event.type));
#endif
				endMenu = true;
				break;
			}
			UiHandleEvents(&event);
		}
	}
#ifdef __PS3__
	LogInfo("PS3 title: dialog loop complete endMenu={}", endMenu);
#endif

	TitleFree();
#ifdef __PS3__
	LogInfo("PS3 title: UiTitleDialog returning");
#endif
}

} // namespace devilution
