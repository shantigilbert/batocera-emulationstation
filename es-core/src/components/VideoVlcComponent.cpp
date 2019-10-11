#include "components/VideoVlcComponent.h"

#include "renderers/Renderer.h"
#include "resources/TextureResource.h"
#include "utils/StringUtil.h"
#include "PowerSaver.h"
#include "Settings.h"
#include <vlc/vlc.h>
#include <SDL_mutex.h>
#include <cmath>
#include "SystemConf.h"
#include "ThemeData.h"

#ifdef WIN32
#include <codecvt>
#endif

#include "ImageIO.h"

#define MATHPI          3.141592653589793238462643383279502884L

libvlc_instance_t* VideoVlcComponent::mVLC = NULL;

// VLC prepares to render a video frame.
static void *lock(void *data, void **p_pixels) 
{
	struct VideoContext *c = (struct VideoContext *)data;
	
	int frame = (c->surfaceId ^ 1);
	
	c->mutexes[frame].lock();
	c->hasFrame[frame] = false;
	*p_pixels = c->surfaces[frame];
	return NULL; // Picture identifier, not needed here.
}

// VLC just rendered a video frame.
static void unlock(void *data, void* /*id*/, void *const* /*p_pixels*/) 
{
	struct VideoContext *c = (struct VideoContext *)data;

	int frame = (c->surfaceId ^ 1);	

	c->surfaceId = frame;
	c->hasFrame[frame] = true;
	c->mutexes[frame].unlock();
}

// VLC wants to display a video frame.
static void display(void* data, void* id)
{
	if (data == NULL)
		return;

	struct VideoContext *c = (struct VideoContext *)data;
	if (c->valid && c->component != NULL && !c->component->isPlaying() && c->component->isWaitingForVideoToStart())
		c->component->onVideoStarted();
}

VideoVlcComponent::VideoVlcComponent(Window* window, std::string subtitles) :
	VideoComponent(window),
	mMediaPlayer(nullptr), 
	mMedia(nullptr)
{
	mElapsed = 0;

	// Get an empty texture for rendering the video
	mTexture = nullptr;// TextureResource::get("");
	mEffect = VideoVlcFlags::VideoVlcEffect::BUMP;

	// Make sure VLC has been initialised
	setupVLC(subtitles);
}

VideoVlcComponent::~VideoVlcComponent()
{
	stopVideo();
}

void VideoVlcComponent::setResize(float width, float height)
{
	if (mSize.x() != 0 && mSize.y() != 0 && !mTargetIsMax && !mTargetIsMin && mTargetSize.x() == width && mTargetSize.y() == height)
		return;

	mTargetSize = Vector2f(width, height);
	mTargetIsMax = false;
	mTargetIsMin = false;
	mStaticImage.setResize(width, height);
	resize();
}

void VideoVlcComponent::setMaxSize(float width, float height)
{
	if (mSize.x() != 0 && mSize.y() != 0 && mTargetIsMax && !mTargetIsMin && mTargetSize.x() == width && mTargetSize.y() == height)
		return;

	mTargetSize = Vector2f(width, height);
	mTargetIsMax = true;
	mTargetIsMin = false;
	mStaticImage.setMaxSize(width, height);
	resize();
}

void VideoVlcComponent::setMinSize(float width, float height)
{
	if (mSize.x() != 0 && mSize.y() != 0 && mTargetIsMin && !mTargetIsMax && mTargetSize.x() == width && mTargetSize.y() == height)
		return;

	mTargetSize = Vector2f(width, height);
	mTargetIsMax = false;
	mTargetIsMin = true;
	mStaticImage.setMinSize(width, height);
	resize();
}

void VideoVlcComponent::onVideoStarted()
{
	VideoComponent::onVideoStarted();
	resize();
}

void VideoVlcComponent::resize()
{
	if(!mTexture)
		return;

	const Vector2f textureSize((float)mVideoWidth, (float)mVideoHeight);

	if(textureSize == Vector2f::Zero())
		return;

		// SVG rasterization is determined by height (see SVGResource.cpp), and rasterization is done in terms of pixels
		// if rounding is off enough in the rasterization step (for images with extreme aspect ratios), it can cause cutoff when the aspect ratio breaks
		// so, we always make sure the resultant height is an integer to make sure cutoff doesn't happen, and scale width from that
		// (you'll see this scattered throughout the function)
		// this is probably not the best way, so if you're familiar with this problem and have a better solution, please make a pull request!

		if(mTargetIsMax)
		{

			mSize = textureSize;

			Vector2f resizeScale((mTargetSize.x() / mSize.x()), (mTargetSize.y() / mSize.y()));

			if(resizeScale.x() < resizeScale.y())
			{
				mSize[0] *= resizeScale.x();
				mSize[1] *= resizeScale.x();
			}else{
				mSize[0] *= resizeScale.y();
				mSize[1] *= resizeScale.y();
			}

			// for SVG rasterization, always calculate width from rounded height (see comment above)
			mSize[1] = Math::round(mSize[1]);
			mSize[0] = (mSize[1] / textureSize.y()) * textureSize.x();

		}
		else if (mTargetIsMin)
		{
			mSize = ImageIO::getPictureMinSize(textureSize, mTargetSize);
		}
		else {
			// if both components are set, we just stretch
			// if no components are set, we don't resize at all
			mSize = mTargetSize == Vector2f::Zero() ? textureSize : mTargetSize;

			// if only one component is set, we resize in a way that maintains aspect ratio
			// for SVG rasterization, we always calculate width from rounded height (see comment above)
			if(!mTargetSize.x() && mTargetSize.y())
			{
				mSize[1] = Math::round(mTargetSize.y());
				mSize[0] = (mSize.y() / textureSize.y()) * textureSize.x();
			}else if(mTargetSize.x() && !mTargetSize.y())
			{
				mSize[1] = Math::round((mTargetSize.x() / textureSize.x()) * textureSize.y());
				mSize[0] = (mSize.y() / textureSize.y()) * textureSize.x();
			}
		}

	// mSize.y() should already be rounded
	mTexture->rasterizeAt((size_t)Math::round(mSize.x()), (size_t)Math::round(mSize.y()));

	onSizeChanged();
}

void VideoVlcComponent::render(const Transform4x4f& parentTrans)
{
	if (!isVisible())
		return;

	VideoComponent::render(parentTrans);

	bool initFromPixels = true;

	if (!mIsPlaying || !mContext.valid)
	{
		// If video is still attached to the path & texture is initialized, we suppose it had just been stopped (onhide, ondisable, screensaver...)
		// still render the last frame
		if (mTexture != nullptr && !mVideoPath.empty() && mPlayingVideoPath == mVideoPath && mTexture->isLoaded())
			initFromPixels = false;
		else
			return;
	}

	float t = mFadeIn;
	if (mFadeIn < 1.0)
	{
		t = 1.0 - mFadeIn;
		t -= 1; // cubic ease in
		t = Math::lerp(0, 1, t*t*t + 1);
		t = 1.0 - t;
	}

	if (t == 0.0)
		return;

	Transform4x4f trans = parentTrans * getTransform();

	if (!Renderer::isVisibleOnScreen(trans.translation().x(), trans.translation().y(), mSize.x(), mSize.y()))
		return;

	GuiComponent::renderChildren(trans);
	Renderer::setMatrix(trans);

	// Build a texture for the video frame
	if (initFromPixels)
	{		
		int frame = mContext.surfaceId;
		if (mContext.hasFrame[frame])
		{
			if (mTexture == nullptr)
			{
				mTexture = TextureResource::get("");
				resize();
			}

#ifdef _RPI_
			// Rpi : A lot of videos are encoded in 60fps on screenscraper
			// Try to limit transfert to opengl textures to 30fps to save CPU
			if (!Settings::getInstance()->getBool("OptimizeVideo") || mElapsed >= 40) // 40ms = 25fps, 33.33 = 30 fps
#endif
			{
				mContext.mutexes[frame].lock();
				mTexture->initFromExternalPixels(mContext.surfaces[frame], mVideoWidth, mVideoHeight);
				mContext.hasFrame[frame] = false;
				mContext.mutexes[frame].unlock();

				mElapsed = 0;
			}
		}
	}

	if (mTexture == nullptr)
		return;

	const unsigned int fadeIn = t * 255.0f;
	const unsigned int color = Renderer::convertColor(0xFFFFFF00 | fadeIn);
	Renderer::Vertex   vertices[4];

	if (mEffect == VideoVlcFlags::VideoVlcEffect::BUMP && mFadeIn > 0.0 && mFadeIn < 1.0 && mConfig.startDelay > 0)
	{
		// Bump Effect
		float bump = sin((MATHPI / 2.0) * mFadeIn) + sin(MATHPI * mFadeIn) / 2.0;

		float w = mSize.x() * bump;
		float h = mSize.y() * bump;
		float centerX = mSize.x() / 2.0f;
		float centerY = mSize.y() / 2.0f;

		Vector2f topLeft(Math::round(centerX - w / 2.0f), Math::round(centerY - h / 2.0f));
		Vector2f bottomRight(Math::round(centerX + w / 2.0f), Math::round(centerY + h / 2.0f));

		vertices[0] = { { topLeft.x()		, topLeft.y()	  }, { 0.0f, 0.0f }, color };
		vertices[1] = { { topLeft.x()		, bottomRight.y() }, { 0.0f, 1.0f }, color };
		vertices[2] = { { bottomRight.x()	, topLeft.y()     }, { 1.0f, 0.0f }, color };
		vertices[3] = { { bottomRight.x()	, bottomRight.y() }, { 1.0f, 1.0f }, color };
	}
	else
	{
		vertices[0] = { { 0.0f     , 0.0f      }, { 0.0f, 0.0f }, color };
		vertices[1] = { { 0.0f     , mSize.y() }, { 0.0f, 1.0f }, color };
		vertices[2] = { { mSize.x(), 0.0f      }, { 1.0f, 0.0f }, color };
		vertices[3] = { { mSize.x(), mSize.y() }, { 1.0f, 1.0f }, color };
	}

	// round vertices
	for(int i = 0; i < 4; ++i)
		vertices[i].pos.round();
	
	if (mTexture->bind())
	{
		if (mTargetIsMin)
		{
			Vector2f targetPos = (mTargetSize - mSize) * mOrigin * -1;

			Vector2i pos(trans.translation().x() + (int)targetPos.x(), trans.translation().y() + (int)targetPos.y());
			Vector2i size((int)mTargetSize.round().x(), (int)mTargetSize.round().y());
			Renderer::pushClipRect(pos, size);
		}

		// Render it
		Renderer::drawTriangleStrips(&vertices[0], 4);

		if (mTargetIsMin)
			Renderer::popClipRect();

		Renderer::bindTexture(0);
	}
}

void VideoVlcComponent::setupContext()
{
	if (mContext.valid)
		return;
	
	// Create an RGBA surface to render the video into
	mContext.surfaces[0] = new unsigned char[mVideoWidth * mVideoHeight * 4];
	mContext.surfaces[1] = new unsigned char[mVideoWidth * mVideoHeight * 4];
	mContext.hasFrame[0] = false;	
	mContext.hasFrame[1] = false;
	mContext.component = this;
	mContext.valid = true;	
	resize();	
}

void VideoVlcComponent::freeContext()
{
	if (!mContext.valid)
		return;

	if (!mDisable)
	{
		// Release texture memory -> except if mDisable by topWindow ( ex: menu was poped )
		mTexture = nullptr;
	}

	delete[] mContext.surfaces[0];
	delete[] mContext.surfaces[1];
	mContext.surfaces[0] = nullptr;
	mContext.surfaces[1] = nullptr;	
	mContext.hasFrame[0] = false;
	mContext.hasFrame[1] = false;
	mContext.component = NULL;
	mContext.valid = false;			
}

void VideoVlcComponent::setupVLC(std::string subtitles)
{
	if (mVLC != nullptr)
		return;

	std::vector<std::string> cmdline;
	cmdline.push_back("--quiet");
	cmdline.push_back("--no-video-title-show");
	
	if (!subtitles.empty())
	{
		cmdline.push_back("--sub-file");
		cmdline.push_back(subtitles);
	}

	std::string commandLine = SystemConf::getInstance()->get("vlc.commandline");
	if (!commandLine.empty())
	{
		std::vector<std::string> tokens = Utils::String::split(commandLine, ' ');
		for (auto token : tokens)
			cmdline.push_back(token);
	}

	const char* *theArgs = new const char*[10];

	for (int i = 0 ; i < cmdline.size() ; i++)
		theArgs[i] = cmdline[i].c_str();

	/*
	// If VLC hasn't been initialised yet then do it now
	const char** args;
	const char* newargs[] = { "--quiet", "--sub-file", subtitles.c_str() };
	const char* singleargs[] = { "--quiet" };
	int argslen = 0;

	if (!subtitles.empty())
	{
		argslen = sizeof(newargs) / sizeof(newargs[0]);
		args = newargs;
	}
	else
	{
		argslen = sizeof(singleargs) / sizeof(singleargs[0]);
		args = singleargs;
	}*/
	mVLC = libvlc_new(cmdline.size(), theArgs);

	delete[] theArgs;
}

void VideoVlcComponent::handleLooping()
{
	if (mIsPlaying && mMediaPlayer)
	{
		libvlc_state_t state = libvlc_media_player_get_state(mMediaPlayer);
		if (state == libvlc_Ended)
		{
			if (!Settings::getInstance()->getBool("VideoAudio"))
			{
				libvlc_audio_set_mute(mMediaPlayer, 1);
			}
			//libvlc_media_player_set_position(mMediaPlayer, 0.0f);
			if (mMedia)
				libvlc_media_player_set_media(mMediaPlayer, mMedia);

			libvlc_media_player_play(mMediaPlayer);
		}
	}
}

void VideoVlcComponent::startVideo()
{
	if (mIsPlaying)
		return;

	mVideoWidth = 0;
	mVideoHeight = 0;

#ifdef WIN32
	std::string path(Utils::String::replace(mVideoPath, "/", "\\"));
#else
	std::string path(mVideoPath);
#endif
	// Make sure we have a video path
	if (mVLC && (path.size() > 0))
	{
		// Set the video that we are going to be playing so we don't attempt to restart it
		mPlayingVideoPath = mVideoPath;

		// Open the media
		mMedia = libvlc_media_new_path(mVLC, path.c_str());
		if (mMedia)
		{			
			// use : vlc �long-help
			// WIN32 ? libvlc_media_add_option(mMedia, ":avcodec-hw=dxva2");
			// RPI/OMX ? libvlc_media_add_option(mMedia, ":codec=mediacodec,iomx,all"); .

			std::string options = SystemConf::getInstance()->get("vlc.options");
			if (!options.empty())
			{
				std::vector<std::string> tokens = Utils::String::split(options, ' ');
				for (auto token : tokens)
					libvlc_media_add_option(mMedia, token.c_str());
			}

			unsigned track_count;
			// Get the media metadata so we can find the aspect ratio
			libvlc_media_parse(mMedia);
			libvlc_media_track_t** tracks;
			track_count = libvlc_media_tracks_get(mMedia, &tracks);
			for (unsigned track = 0; track < track_count; ++track)
			{
				if (tracks[track]->i_type == libvlc_track_video)
				{
					mVideoWidth = tracks[track]->video->i_width;
					mVideoHeight = tracks[track]->video->i_height;
					break;
				}
			}
			libvlc_media_tracks_release(tracks, track_count);

			// Make sure we found a valid video track
			if ((mVideoWidth > 0) && (mVideoHeight > 0))
			{			
				if (Settings::getInstance()->getBool("OptimizeVideo"))
				{
					// Avoid videos bigger than resolution
					Vector2f maxSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());
										
#ifdef _RPI_
					// Temporary -> RPI -> Try to limit videos to 400x300 for performance benchmark
					if (!Renderer::isSmallScreen())
						maxSize = Vector2f(400, 300);
#endif

					if (!mTargetSize.empty() && (mTargetSize.x() < maxSize.x() || mTargetSize.y() < maxSize.y()))
						maxSize = mTargetSize;

					

					// If video is bigger than display, ask VLC for a smaller image
					auto sz = ImageIO::adjustPictureSize(Vector2i(mVideoWidth, mVideoHeight), Vector2i(maxSize.x(), maxSize.y()), mTargetIsMin);
					if (sz.x() < mVideoWidth || sz.y() < mVideoHeight)
					{
						mVideoWidth = sz.x();
						mVideoHeight = sz.y();
					}
				}

				PowerSaver::pause();
				setupContext();

				// Setup the media player
				mMediaPlayer = libvlc_media_player_new_from_media(mMedia);

				if (!Settings::getInstance()->getBool("VideoAudio"))
					libvlc_audio_set_mute(mMediaPlayer, 1);


				libvlc_media_player_play(mMediaPlayer);
				libvlc_video_set_callbacks(mMediaPlayer, lock, unlock, display, (void*)&mContext);
				libvlc_video_set_format(mMediaPlayer, "RGBA", (int)mVideoWidth, (int)mVideoHeight, (int)mVideoWidth * 4);

				// Update the playing state -> Useless now set by display() & onVideoStarted
				//mIsPlaying = true;
				//mFadeIn = 0.0f;
			}
		}
	}
}

void VideoVlcComponent::stopVideo()
{
	mIsPlaying = false;
	mStartDelayed = false;

	// Release the media player so it stops calling back to us
	if (mMediaPlayer)
	{
		libvlc_media_player_stop(mMediaPlayer);
		libvlc_media_player_release(mMediaPlayer);
		mMediaPlayer = NULL;
	}

	// Release the media
	if (mMedia)
	{
		libvlc_media_release(mMedia); 
		mMedia = NULL;
	}		
		
	freeContext();
	PowerSaver::resume();	
}

void VideoVlcComponent::applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties)
{
	VideoComponent::applyTheme(theme, view, element, properties);

	using namespace ThemeFlags;

	const ThemeData::ThemeElement* elem = theme->getElement(view, element, "video");
	if (elem && elem->has("effect"))
	{
		if (!(elem->get<std::string>("effect").compare("bump")))
			mEffect = VideoVlcFlags::VideoVlcEffect::BUMP;
		else
			mEffect = VideoVlcFlags::VideoVlcEffect::NONE;
	}
}

void VideoVlcComponent::update(int deltaTime)
{
	mElapsed += deltaTime;
	VideoComponent::update(deltaTime);
}
