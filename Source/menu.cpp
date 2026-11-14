/**
 * @file mainmenu.cpp
 *
 * Implementation of functions for interacting with the main menu.
 */

#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/selok.h"
#include "DiabloUI/settingsmenu.h"
#include "engine/assets.hpp"
#include "engine/demomode.h"
#include "game_mode.hpp"
#include "init.hpp"
#include "movie.h"
#include "options.h"
#include "pfile.h"
#include "storm/storm_net.hpp"
#include "utils/language.h"
#ifdef __PS3__
#include "utils/log.hpp"
#endif

namespace devilution {

uint32_t gSaveNumber;

namespace {

_music_id NextTrack()
{
	if (gbIsSpawn) {
		return TMUSIC_INTRO;
	}

	switch (sgnMusicTrack) {
	case TMUSIC_INTRO:
		return TMUSIC_CATACOMBS;
	case TMUSIC_CATACOMBS:
		return TMUSIC_CAVES;
	case TMUSIC_CAVES:
		return TMUSIC_HELL;
	case TMUSIC_HELL:
		return gbIsHellfire ? TMUSIC_NEST : TMUSIC_INTRO;
	case TMUSIC_NEST:
		return gbIsHellfire ? TMUSIC_CRYPT : TMUSIC_INTRO;
	default:
		return TMUSIC_INTRO;
	}
}

void RefreshMusic()
{
	music_start(NextTrack());
}

bool InitMenu(_selhero_selections type)
{
	bool success;

	if (type == SELHERO_PREVIOUS)
		return true;

	success = StartGame(type != SELHERO_CONTINUE, type != SELHERO_CONNECT);
#ifdef __PS3__
	LogInfo("PS3 menu: StartGame hero-selection-type={} returned success={}", static_cast<int>(type), success);
#endif
	if (success)
		RefreshMusic();

	return success;
}

bool InitSinglePlayerMenu()
{
	gbIsMultiplayer = false;
	return InitMenu(SELHERO_NEW_DUNGEON);
}

bool InitMultiPlayerMenu()
{
#ifndef _DEBUG
	if (IsAssetIntegrityViolated || HasLooseLogicAssets()) {
		UiSelOkDialog(_("Multi Player Game").data(), _("Cannot play Multiplayer with overridden *.lua, *.tsv, or *.sol assets.").data(), false);
		return true;
	}
#endif
	gbIsMultiplayer = true;
	return InitMenu(SELHERO_CONNECT);
}

void PlayIntro()
{
	music_stop();
	if (gbIsHellfire)
		play_movie("gendata\\Hellfire.smk", true);
	else
		play_movie("gendata\\diablo1.smk", true);
	RefreshMusic();
}

bool DummyGetHeroInfo(_uiheroinfo * /*pInfo*/)
{
	return true;
}

} // namespace

bool mainmenu_select_hero_dialog(GameData *gameData)
{
	OptionEntryInt<uint32_t> *pSaveNumberFromOptions = nullptr;
	_selhero_selections dlgresult = SELHERO_NEW_DUNGEON;
	if (demo::IsRunning()) {
		pfile_ui_set_hero_infos(DummyGetHeroInfo);
		gbLoadGame = true;
	} else if (!gbIsMultiplayer) {
		pSaveNumberFromOptions = gbIsHellfire ? &GetOptions().Hellfire.lastSinglePlayerHero : &GetOptions().Diablo.lastSinglePlayerHero;
		gSaveNumber = **pSaveNumberFromOptions;
		UiSelHeroSingDialog(
		    pfile_ui_set_hero_infos,
		    pfile_ui_save_create,
		    pfile_delete_save,
		    pfile_ui_set_class_stats,
		    &dlgresult,
		    &gSaveNumber,
		    &gameData->nDifficulty);

		gbLoadGame = (dlgresult == SELHERO_CONTINUE);
	} else {
		pSaveNumberFromOptions = gbIsHellfire ? &GetOptions().Hellfire.lastMultiplayerHero : &GetOptions().Diablo.lastMultiplayerHero;
		gSaveNumber = **pSaveNumberFromOptions;
		UiSelHeroMultDialog(
		    pfile_ui_set_hero_infos,
		    pfile_ui_save_create,
		    pfile_delete_save,
		    pfile_ui_set_class_stats,
		    &dlgresult,
		    &gSaveNumber);
	}
#ifdef __PS3__
	LogInfo("PS3 menu: hero dialog returned selection={} load_game={}", static_cast<int>(dlgresult), gbLoadGame);
#endif
	if (dlgresult == SELHERO_PREVIOUS) {
#ifdef __PS3__
		LogInfo("PS3 menu: hero dialog backed out to the previous menu");
#endif
		return false;
	}

	if (pSaveNumberFromOptions != nullptr)
		pSaveNumberFromOptions->SetValue(gSaveNumber);

	return true;
}

void mainmenu_wait_for_button_sound()
{
	SDL_FillSurfaceRect(DiabloUiSurface(), nullptr, 0);
	UiFadeIn();
	SDL_Delay(350); // delay to let button pressed sound finish playing
}

void mainmenu_loop()
{
	bool done;

	RefreshMusic();
	done = false;
#ifdef __PS3__
	LogInfo("PS3 menu: mainmenu_loop entered spawn={} hellfire={} active={}", gbIsSpawn, gbIsHellfire, gbActive);
#endif

	do {
		_mainmenu_selections menu = MAINMENU_NONE;
		if (demo::IsRunning())
			menu = MAINMENU_SINGLE_PLAYER;
		else if (!UiMainMenuDialog(gszProductName, &menu, 30))
			app_fatal(_("Unable to display mainmenu"));

#ifdef __PS3__
		LogInfo("PS3 menu: mainmenu_loop received result={}", static_cast<int>(menu));
#endif
		switch (menu) {
		case MAINMENU_NONE:
			break;
		case MAINMENU_SINGLE_PLAYER: {
			const bool initialized = InitSinglePlayerMenu();
#ifdef __PS3__
			LogInfo("PS3 menu: single-player menu returned initialized={}", initialized);
#endif
			if (!initialized)
				done = true;
			break;
		}
		case MAINMENU_MULTIPLAYER: {
			const bool initialized = InitMultiPlayerMenu();
#ifdef __PS3__
			LogInfo("PS3 menu: multiplayer menu returned initialized={}", initialized);
#endif
			if (!initialized)
				done = true;
			break;
		}
		case MAINMENU_ATTRACT_MODE:
#ifdef __PS3__
			LogInfo("PS3 menu: attract mode selected; intro_available={} active={}", HaveIntro(), gbActive);
#endif
			if (gbIsSpawn && !HaveIntro())
				done = false;
			else if (gbActive)
				PlayIntro();
			break;
		case MAINMENU_SHOW_CREDITS:
			UiCreditsDialog();
			break;
		case MAINMENU_SHOW_SUPPORT:
			UiSupportDialog();
			break;
		case MAINMENU_EXIT_DIABLO:
#ifdef __PS3__
			LogInfo("PS3 menu: explicit Exit Diablo selection");
#endif
			mainmenu_wait_for_button_sound();
			done = true;
			break;
		case MAINMENU_SETTINGS:
			UiSettingsMenu();
			break;
		}
	} while (!done);

	music_stop();
#ifdef __PS3__
	LogInfo("PS3 menu: mainmenu_loop leaving");
#endif
}

} // namespace devilution
