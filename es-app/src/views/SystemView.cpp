#include "views/SystemView.h"
#include "SystemData.h"
#include "Renderer.h"
#include "Log.h"
#include "Window.h"
#include "views/ViewController.h"
#include "animations/LambdaAnimation.h"
#include "SystemData.h"
#include "Settings.h"
#include "Util.h"

#define SELECTED_SCALE 1.5f
#define LOGO_PADDING ((logoSize().x() * (SELECTED_SCALE - 1)/2) + (mSize.x() * 0.06f))
#define BAND_HEIGHT (logoSize().y() * SELECTED_SCALE)

SystemView::SystemView(Window* window) : IList<SystemViewData, SystemData*>(window, LIST_SCROLL_STYLE_SLOW, LIST_ALWAYS_LOOP),
	mSystemInfo(window, "SYSTEM INFO", Font::get(FONT_SIZE_SMALL), 0x33333300, ALIGN_CENTER)
{
	mCamOffset = 0;
	mExtrasCamOffset = 0;
	mExtrasFadeOpacity = 0.0f;

	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	mSystemInfo.setSize(mSize.x(), mSystemInfo.getSize().y() * 1.333f);
	mSystemInfo.setPosition(0, (mSize.y() + BAND_HEIGHT) / 2);
		
	populate();
}

void SystemView::populate()
{
	LOG(LogDebug) << "SystemView::populate()";
	bool filterHidden = false;
	bool filterKid = false;
	if (Settings::getInstance()->getString("UIMode") != "Full")
	{
		filterHidden = true;
	}
	if (Settings::getInstance()->getString("UIMode") == "Kid")
	{
		filterKid = true;
	}
	
	bool filterFav = Settings::getInstance()->getBool("FavoritesOnly");

	LOG(LogDebug) << "    Settings.UIMode  = " << Settings::getInstance()->getString("UIMode");
	LOG(LogDebug) << "    Settings.FavoritesOnly  = " << Settings::getInstance()->getBool("FavoritesOnly");
	LOG(LogDebug) << "    filterHidden = " << filterHidden;
	LOG(LogDebug) << "    filterFav = " << filterFav;
	LOG(LogDebug) << "    filterKid = " << filterKid;
	
	mEntries.clear();
	for(auto it = SystemData::sSystemVector.begin(); it != SystemData::sSystemVector.end(); it++)
	{
		LOG(LogDebug) << "    System = " << (*it)->getName();
		
		if ((*it)->getGameCount(filterHidden, filterFav, filterKid) > 0)
		{
			LOG(LogDebug) << (*it)->getGameCount(filterHidden, filterFav, filterKid) << " games found, populating.";
			
			const std::shared_ptr<ThemeData>& theme = (*it)->getTheme();

			Entry e;
			e.name = (*it)->getName();
			e.object = *it;

			// make logo
			if(theme->getElement("system", "logo", "image"))
			{
				ImageComponent* logo = new ImageComponent(mWindow);
				logo->setMaxSize(Eigen::Vector2f(logoSize().x(), logoSize().y()));
				logo->applyTheme((*it)->getTheme(), "system", "logo", ThemeFlags::PATH);
				logo->setPosition((logoSize().x() - logo->getSize().x()) / 2, (logoSize().y() - logo->getSize().y()) / 2); // center
				e.data.logo = std::shared_ptr<GuiComponent>(logo);

				ImageComponent* logoSelected = new ImageComponent(mWindow);
				logoSelected->setMaxSize(Eigen::Vector2f(logoSize().x() * SELECTED_SCALE, logoSize().y() * SELECTED_SCALE * 0.70f));
				logoSelected->applyTheme((*it)->getTheme(), "system", "logo", ThemeFlags::PATH);
				logoSelected->setPosition((logoSize().x() - logoSelected->getSize().x()) / 2, 
					(logoSize().y() - logoSelected->getSize().y()) / 2); // center
				e.data.logoSelected = std::shared_ptr<GuiComponent>(logoSelected);
			}else{
				// no logo in theme; use text
				TextComponent* text = new TextComponent(mWindow, 
					(*it)->getName(), 
					Font::get(FONT_SIZE_LARGE), 
					0x000000FF, 
					ALIGN_CENTER);
				text->setSize(logoSize());
				e.data.logo = std::shared_ptr<GuiComponent>(text);

				TextComponent* textSelected = new TextComponent(mWindow, 
					(*it)->getName(), 
					Font::get((int)(FONT_SIZE_LARGE * SELECTED_SCALE)), 
					0x000000FF, 
					ALIGN_CENTER);
				textSelected->setSize(logoSize());
				e.data.logoSelected = std::shared_ptr<GuiComponent>(textSelected);
			}

			// make background extras
			e.data.backgroundExtras = std::shared_ptr<ThemeExtras>(new ThemeExtras(mWindow));
			e.data.backgroundExtras->setExtras(ThemeData::makeExtras((*it)->getTheme(), "system", mWindow));

			this->add(e);
		}
	}
}

void SystemView::goToSystem(SystemData* system, bool animate)
{
	setCursor(system);

	if(!animate)
		finishAnimation(0);
}

bool SystemView::input(InputConfig* config, Input input)
{
	if(input.value != 0)
	{
		if(config->getDeviceId() == DEVICE_KEYBOARD && input.value && input.id == SDLK_r && SDL_GetModState() & KMOD_LCTRL && Settings::getInstance()->getBool("Debug"))
		{
			LOG(LogInfo) << "    Reloading SystemList view";

			// reload themes
			for(auto it = mEntries.begin(); it != mEntries.end(); it++)
				it->object->loadTheme();

			populate();
			updateHelpPrompts();
			return true;
		}
		if(config->isMappedTo("left", input))
		{
			listInput(-1);
			return true;
		}
		if(config->isMappedTo("right", input))
		{
			listInput(1);
			return true;
		}
		if(config->isMappedTo("a", input))
		{
			stopScrolling();
			ViewController::get()->goToGameList(getSelected());
			return true;
		}
	}else{
		if(config->isMappedTo("left", input) || config->isMappedTo("right", input))
			listInput(0);
	}

	return GuiComponent::input(config, input);
}

void SystemView::update(int deltaTime)
{
	listUpdate(deltaTime);
	GuiComponent::update(deltaTime);
}

void SystemView::onCursorChanged(const CursorState& state)
{
	// update help style
	updateHelpPrompts();

	float startPos = mCamOffset;

	float posMax = (float)mEntries.size();
	float target = (float)mCursor;

	// what's the shortest way to get to our target?
	// it's one of these...

	float endPos = target; // directly
	float dist = abs(endPos - startPos);
	
	if(abs(target + posMax - startPos) < dist)
		endPos = target + posMax; // loop around the end (0 -> max)
	if(abs(target - posMax - startPos) < dist)
		endPos = target - posMax; // loop around the start (max - 1 -> -1)

	
	// animate mSystemInfo's opacity (fade out, wait, fade back in)

	cancelAnimation(1);
	cancelAnimation(2);

	const float infoStartOpacity = mSystemInfo.getOpacity() / 255.f;

	Animation* infoFadeOut = new LambdaAnimation(
		[infoStartOpacity, this] (float t)
	{
		mSystemInfo.setOpacity((unsigned char)(lerp<float>(infoStartOpacity, 0.f, t) * 255));
	}, (int)(infoStartOpacity * 150));


	/*unsigned int favoritesCount = getSelected()->getGameCount(false, true, false);
	unsigned int kidgamesCount = getSelected()->getKidGamesCount();
	unsigned int visibleCount = getSelected()->getVisibleCount();*/
	
	// also change the text after we've fully faded out
	setAnimation(infoFadeOut, 0, [this] {
		std::stringstream ss;

		// only display a game count if there are at least 2 games - Full / Kiosk UI modes
		LOG(LogDebug) << "System selected = " << getSelected()->getName() << ", UIMode = "<< Settings::getInstance()->getString("UIMode");
		//LOG(LogDebug) << "getSelected()->getGameCount(false, false, false) = " << getSelected()->getGameCount(false, false, false);
		//LOG(LogDebug) << "getSelected()->getGameCount(true, false, false) = " << getSelected()->getGameCount(true, false, false);
		//LOG(LogDebug) << "getSelected()->getGameCount(false, true, false) = " << getSelected()->getGameCount(false, true, false);
		//LOG(LogDebug) << "getSelected()->getGameCount(false, false, true) = " << getSelected()->getGameCount(false, false, true);
		//LOG(LogDebug) << "getSelected()->getGameCount(true, true, false) = " << getSelected()->getGameCount(true, true, false);
		//LOG(LogDebug) << "getSelected()->getGameCount(false, true, true) = " << getSelected()->getGameCount(false, true, true);

		if(Settings::getInstance()->getString("UIMode") == "Full")
		{
			unsigned int gameCount = getSelected()->getGameCount(false, false, false);
			if (gameCount == 1)
			{
				ss << gameCount << " GAME";
			}
			else if (gameCount > 1)
			{
				ss << gameCount << " GAMES";
			}
			unsigned int favoritesCount = getSelected()->getGameCount(false, true, false);
			if (favoritesCount == 1)
			{
				ss << ", " << favoritesCount << " FAVORITE";
			}
			else if (favoritesCount > 1)
			{
				ss << ", " << favoritesCount << " FAVORITES";
			}
			unsigned int kidgamesCount = getSelected()->getGameCount(false, false, true);
			if (kidgamesCount == 1)
			{
				ss << ", " << kidgamesCount << " KID-FRIENDLY GAME";
			}
			else if (kidgamesCount > 1)
			{
				ss << ", " << kidgamesCount << " KID-FRIENDLY GAMES";
			}
			ss << " AVAILABLE.";
		}else if(Settings::getInstance()->getString("UIMode") == "Kiosk")
		{
			unsigned int gameCount = getSelected()->getGameCount(true, false, false);
			if (gameCount == 1)
			{
				ss << gameCount << " GAME";
			}
			else if (gameCount > 1)
			{
				ss << gameCount << " GAMES";
			}
			unsigned int favoritesCount = getSelected()->getGameCount(true, true, false);
			if (favoritesCount == 1)
			{
				ss << ", " << favoritesCount << " FAVORITE";
			}
			else if (favoritesCount > 1)
			{
				ss << ", " << favoritesCount << " FAVORITES";
			}
			unsigned int kidgamesCount = getSelected()->getGameCount(true, false, true);
			if (kidgamesCount == 1)
			{
				ss << ", " << kidgamesCount << " KID-FRIENDLY GAME";
			}
			else if (kidgamesCount > 1)
			{
				ss << ", " << kidgamesCount << " KID-FRIENDLY GAMES";
			}
			ss << " AVAILABLE.";
		}else if(Settings::getInstance()->getString("UIMode") == "Kid")
		{
			unsigned int kidgamesCount = getSelected()->getGameCount(true, false, true);
			if (kidgamesCount == 1)
			{
				ss << kidgamesCount << " KID-FRIENDLY GAME";
			}
			else if (kidgamesCount > 1)
			{
				ss << kidgamesCount << " KID-FRIENDLY GAMES";
			}
			unsigned int favoritesCount = getSelected()->getGameCount(true, true, true);
			if (favoritesCount == 1)
			{
				ss << ", " << favoritesCount << " FAVORITE";
			}
			else if (favoritesCount > 1)
			{
				ss << ", " << favoritesCount << " FAVORITES";
			}
			ss << " AVAILABLE.";
		}

		mSystemInfo.setText(ss.str());
	}, false, 1);

	Animation* infoFadeIn = new LambdaAnimation(
		[this](float t)
	{
		mSystemInfo.setOpacity((unsigned char)(lerp<float>(0.f, 1.f, t) * 255));
	}, 300);

	// wait ms to fade in
	setAnimation(infoFadeIn, 800, nullptr, false, 2);

	// no need to animate transition, we're not going anywhere (probably mEntries.size() == 1)
	if(endPos == mCamOffset && endPos == mExtrasCamOffset)
		return;

	Animation* anim;
	if(Settings::getInstance()->getString("TransitionStyle") == "fade")
	{
		float startExtrasFade = mExtrasFadeOpacity;
		anim = new LambdaAnimation(
			[startExtrasFade, startPos, endPos, posMax, this](float t)
		{
			t -= 1;
			float f = lerp<float>(startPos, endPos, t*t*t + 1);
			if(f < 0)
				f += posMax;
			if(f >= posMax)
				f -= posMax;

			this->mCamOffset = f;

			t += 1;
			if(t < 0.3f)
				this->mExtrasFadeOpacity = lerp<float>(0.0f, 1.0f, t / 0.3f + startExtrasFade);
			else if(t < 0.7f)
				this->mExtrasFadeOpacity = 1.0f;
			else
				this->mExtrasFadeOpacity = lerp<float>(1.0f, 0.0f, (t - 0.7f) / 0.3f);

			if(t > 0.5f)
				this->mExtrasCamOffset = endPos;

		}, 500);
	}
	else{ // slide
		anim = new LambdaAnimation(
			[startPos, endPos, posMax, this](float t)
		{
			t -= 1;
			float f = lerp<float>(startPos, endPos, t*t*t + 1);
			if(f < 0)
				f += posMax;
			if(f >= posMax)
				f -= posMax;

			this->mCamOffset = f;
			this->mExtrasCamOffset = f;
		}, 500);
	}

	setAnimation(anim, 0, nullptr, false, 0);
}

void SystemView::render(const Eigen::Affine3f& parentTrans)
{
	if(size() == 0)
		return;

	Eigen::Affine3f trans = getTransform() * parentTrans;
	
	// draw the list elements (titles, backgrounds, logos)
	const float logoSizeX = logoSize().x() + LOGO_PADDING;

	int logoCount = (int)(mSize.x() / logoSizeX) + 2; // how many logos we need to draw
	int center = (int)(mCamOffset);

	if(mEntries.size() == 1)
		logoCount = 1;

	// draw background extras
	Eigen::Affine3f extrasTrans = trans;
	int extrasCenter = (int)mExtrasCamOffset;
	for(int i = extrasCenter - 1; i < extrasCenter + 2; i++)
	{
		int index = i;
		while(index < 0)
			index += mEntries.size();
		while(index >= (int)mEntries.size())
			index -= mEntries.size();

		extrasTrans.translation() = trans.translation() + Eigen::Vector3f((i - mExtrasCamOffset) * mSize.x(), 0, 0);

		Eigen::Vector2i clipRect = Eigen::Vector2i((int)((i - mExtrasCamOffset) * mSize.x()), 0);
		Renderer::pushClipRect(clipRect, mSize.cast<int>());
		mEntries.at(index).data.backgroundExtras->render(extrasTrans);
		Renderer::popClipRect();
	}

	// fade extras if necessary
	if(mExtrasFadeOpacity)
	{
		Renderer::setMatrix(trans);
		Renderer::drawRect(0.0f, 0.0f, mSize.x(), mSize.y(), 0x00000000 | (unsigned char)(mExtrasFadeOpacity * 255));
	}

	// draw logos
	float xOff = (mSize.x() - logoSize().x())/2 - (mCamOffset * logoSizeX);
	float yOff = (mSize.y() - logoSize().y())/2;

	// background behind the logos
	Renderer::setMatrix(trans);
	Renderer::drawRect(0.f, (mSize.y() - BAND_HEIGHT) / 2, mSize.x(), BAND_HEIGHT, 0xFFFFFFD8);

	Eigen::Affine3f logoTrans = trans;
	for(int i = center - logoCount/2; i < center + logoCount/2 + 1; i++)
	{
		int index = i;
		while(index < 0)
			index += mEntries.size();
		while(index >= (int)mEntries.size())
			index -= mEntries.size();

		logoTrans.translation() = trans.translation() + Eigen::Vector3f(i * logoSizeX + xOff, yOff, 0);

		if(index == mCursor) //scale our selection up
		{
			// selected
			const std::shared_ptr<GuiComponent>& comp = mEntries.at(index).data.logoSelected;
			comp->setOpacity(0xFF);
			comp->render(logoTrans);
		}else{
			// not selected
			const std::shared_ptr<GuiComponent>& comp = mEntries.at(index).data.logo;
			comp->setOpacity(0x80);
			comp->render(logoTrans);
		}
	}

	Renderer::setMatrix(trans);
	Renderer::drawRect(mSystemInfo.getPosition().x(), mSystemInfo.getPosition().y() - 1, mSize.x(), mSystemInfo.getSize().y(), 0xDDDDDD00 | (unsigned char)(mSystemInfo.getOpacity() / 255.f * 0xD8));
	mSystemInfo.render(trans);
}

std::vector<HelpPrompt> SystemView::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("left/right", "choose"));
	prompts.push_back(HelpPrompt("a", "select"));
	return prompts;
}

HelpStyle SystemView::getHelpStyle()
{
	HelpStyle style;
	style.applyTheme(mEntries.at(mCursor).object->getTheme(), "system");
	return style;
}
