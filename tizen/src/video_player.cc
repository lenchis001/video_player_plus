#include "video_player.h"

#include <app_manager.h>
#include <dlfcn.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/standard_method_codec.h>
#include <system_info.h>
//#include <Ecore_Wl2.h>

#include <unistd.h>

#include <cstdarg>
#include <functional>

#include "drm_licence.h"
#include "log.h"
#include "video_player_error.h"

#define FMS_KEY_OSD_W "com.samsung/featureconf/product.osd_resolution_width"
#define FMS_KEY_OSD_H "com.samsung/featureconf/product.osd_resolution_height"

static int gPlayerIndex = 1;

// DRM Function
static std::string GetDRMStringInfoByDrmType(int dm_type) {
  switch (dm_type) {
    case 1:
      return "com.microsoft.playready";
    case 2:
      return "com.widevine.alpha";
    default:
      return "com.widevine.alpha";
  }
}

void VideoPlayer::m_CbDrmManagerError(long errCode, char *errMsg,
                                      void *userData) {
  LOG_INFO("m_CbDrmManagerError:[%ld][%s]", errCode, errMsg);
}

gboolean VideoPlayer::m_InstallEMEKey(void *pData) {
  LOG_INFO("m_InstallEMEKey idler callback...");
  VideoPlayer *pThis = static_cast<VideoPlayer *>(pData);
  int ret = 0;
  if (pThis == nullptr) {
    LOG_INFO("pThis == nullptr");
    return true;
  }

  LOG_INFO("Start Install license key!");
  // Make sure there is data in licenseParam.
  if (pThis->m_pbResponse == nullptr) {
    LOG_ERROR("m_pbResponse nullptr!");
    return false;
  }
  FuncDMGRSetData DMGRSetData = nullptr;
  DMGRSetData =
      (FuncDMGRSetData)dlsym(pThis->m_pLibDrmManagerHandle, "DMGRSetData");
  if (nullptr == DMGRSetData) {
    LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
    return false;
  }

  LOG_INFO("DMGRSetData for install_eme_key");
  ret = DMGRSetData((DRMSessionHandle_t)pThis->m_DRMSession, "install_eme_key",
                    (void *)&pThis->m_licenseparam);
  if (ret != DM_ERROR_NONE) {
    LOG_INFO("SetData for install_tvkey failed");
  }

  free(pThis->m_pbResponse);
  pThis->m_pbResponse = nullptr;

  return false;
}

int VideoPlayer::m_CbChallengeData(void *session_id, int msgType, void *msg,
                                   int msgLen, void *userData) {
  LOG_INFO("m_CbChallengeData, MsgType: %d", msgType);
  VideoPlayer *pThis = static_cast<VideoPlayer *>(userData);

  char license_url[128] = {0};
  strcpy(license_url, pThis->m_LicenseUrl.c_str());

  LOG_INFO("[VideoPlayer] license_url %s", license_url);
  pThis->m_pbResponse = nullptr;
  unsigned long responseLen = 0;
  LOG_INFO("The challenge data length is %d", msgLen);

  std::string challengeData(msgLen, 0);
  memcpy(&challengeData[0], (char *)msg, msgLen);
  // Get the license from the DRM Server
  DRM_RESULT drm_result = VideoPlayerDrmLicenseHelper::DoTransactionTZ(
      license_url, (const void *)&challengeData[0],
      (unsigned long)challengeData.length(), &pThis->m_pbResponse, &responseLen,
      (VideoPlayerDrmLicenseHelper::DrmTypeHelper)pThis->m_DrmType, nullptr,
      nullptr);

  LOG_INFO("DRM Result:0x%lx", drm_result);
  LOG_INFO("Response:%s", pThis->m_pbResponse);
  LOG_INFO("ResponseSize:%ld", responseLen);
  if (DRM_SUCCESS != drm_result || nullptr == pThis->m_pbResponse ||
      0 == responseLen) {
    LOG_ERROR("License Acquisition Failed.");
    return DM_ERROR_MANIFEST_PARSE_ERROR;
  }

  pThis->m_licenseparam.param1 = session_id;
  pThis->m_licenseparam.param2 = pThis->m_pbResponse;
  pThis->m_licenseparam.param3 = (void *)responseLen;

  pThis->m_sourceId = g_idle_add(m_InstallEMEKey, pThis);
  LOG_INFO("m_sourceId: %d", pThis->m_sourceId);

  return DM_ERROR_NONE;
}

static plusplayer::drm::Type TransferDrmType(int drmtype) {
  LOG_ERROR("drmType:%d", drmtype);

  if (drmtype == DRM_TYPE_PLAYREADAY) {
    return plusplayer::drm::Type::kPlayready;
  } else if (drmtype == DRM_TYPE_WIDEVINECDM) {
    return plusplayer::drm::Type::kWidevineCdm;
  } else {
    LOG_ERROR("Unknown PrepareCondition");
    return plusplayer::drm::Type::kNone;
  }
}

void VideoPlayer::m_InitializeDrmSession(const std::string &uri, int nDrmType) {
  int ret = 0;
  m_sourceId = 0;
  m_pLibDrmManagerHandle = dlopen("libdrmmanager.so.0", RTLD_LAZY);
  if (nullptr == m_pLibDrmManagerHandle) {
    LOG_ERROR("Failed to dlopen libdrmmanager.so error info: %s", dlerror());
    return;
  }
  LOG_INFO("dlopen libdrmmanager sucessfully!");
  // must use this API before DMGRCreateDRMSession, or it will be crash during
  // calling DRM APIs.

  FuncDMGRSetDRMLocalMode DMGRSetDRMLocalMode = nullptr;
  DMGRSetDRMLocalMode = (FuncDMGRSetDRMLocalMode)dlsym(m_pLibDrmManagerHandle,
                                                       "DMGRSetDRMLocalMode");
  if (DMGRSetDRMLocalMode) {
    DMGRSetDRMLocalMode();
  } else {
    LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
    return;
  }

  std::string drm_string = GetDRMStringInfoByDrmType(nDrmType);
  FuncDMGRCreateDRMSession DMGRCreateDRMSession = nullptr;
  DMGRCreateDRMSession = (FuncDMGRCreateDRMSession)dlsym(
      m_pLibDrmManagerHandle, "DMGRCreateDRMSession");
  if (DMGRCreateDRMSession) {
    m_DRMSession = DMGRCreateDRMSession(DM_TYPE_EME, drm_string.c_str());
    /*
        DRM Type is DM_TYPE_EME,
        "com.microsoft.playready" => PLAYREADY
        "com.widevine.alpha" => Wideveine CDM
        "org.w3.clearkey" => Clear Key
        "org.w3.cdrmv1"  => ChinaDRM
    */
  } else {
    LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
    return;
  }

  FuncDMGRSetData DMGRSetData = nullptr;
  DMGRSetData = (FuncDMGRSetData)dlsym(m_pLibDrmManagerHandle, "DMGRSetData");
  if (nullptr == DMGRSetData) {
    LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
    return;
  }

  FuncDMGRGetData DMGRGetData = nullptr;
  DMGRGetData = (FuncDMGRGetData)dlsym(m_pLibDrmManagerHandle, "DMGRGetData");
  if (nullptr == DMGRGetData) {
    LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
    return;
  }

  FuncDMGRSecurityInitCompleteCB DMGRSecurityInitCompleteCB = nullptr;
  DMGRSecurityInitCompleteCB = (FuncDMGRSecurityInitCompleteCB)dlsym(
      m_pLibDrmManagerHandle, "DMGRSecurityInitCompleteCB");
  if (nullptr == DMGRSecurityInitCompleteCB) {
    LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
    return;
  }

  ret = DMGRSetData(m_DRMSession, (const char *)"set_playready_manifest",
                    (void *)uri.c_str());

  SetDataParam_t configure_param;
  configure_param.param1 = (void *)m_CbDrmManagerError;
  configure_param.param2 = m_DRMSession;
  ret = DMGRSetData(m_DRMSession, (char *)"error_event_callback",
                    (void *)&configure_param);
  if (ret != DM_ERROR_NONE) {
    LOG_ERROR("setdata failed for renew callback");
    return;
  }

  LOG_INFO("PlusPlayer SetDrm Start");
  PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
  plusplayer::drm::Property property;
  property.handle = 0;
  ret = DMGRGetData(m_DRMSession, "drm_handle", (void **)&property.handle);
  if (ret != DM_ERROR_NONE) {
    LOG_ERROR("DMGRGetData drm_handle failed");
    return;
  }
  LOG_INFO("DMGRGetData drm_handle succeed, drm_handle: %d", property.handle);

  property.type = TransferDrmType(m_DrmType);
  SetDataParam_t userData;
  userData.param1 = static_cast<void *>(plusplayer_);
  userData.param2 = m_DRMSession;
  property.license_acquired_cb =
      reinterpret_cast<plusplayer::drm::LicenseAcquiredCb>(
          DMGRSecurityInitCompleteCB);
  property.license_acquired_userdata =
      reinterpret_cast<plusplayer::drm::UserData>(&userData);
  property.external_decryption = false;
  instance.SetDrm(plusplayer_, property);
  LOG_INFO("PlusPlayer SetDrm Done");

  std::string josnString = "";
  ret = DMGRSetData(m_DRMSession, "json_string", (void *)josnString.c_str());
  if (ret != DM_ERROR_NONE) {
    LOG_ERROR("DMGRGetData json_string failed");
    return;
  }

  SetDataParam_t pSetDataParam;

  pSetDataParam.param1 = (void *)m_CbChallengeData;
  pSetDataParam.param2 = (void *)this;
  ret = DMGRSetData(m_DRMSession, "eme_request_key_callback",
                    (void *)&pSetDataParam);
  if (ret != DM_ERROR_NONE) {
    LOG_ERROR("SetData eme_request_key_callback failed\n");
    return;
  }
  ret = DMGRSetData(m_DRMSession, "Initialize", nullptr);
}

void VideoPlayer::m_ReleaseDrmSession() {
  if (m_sourceId > 0) {
    g_source_remove(m_sourceId);
  }
  m_sourceId = 0;

  if (m_DRMSession != nullptr) {
    int ret = 0;
    FuncDMGRSetData DMGRSetData = nullptr;
    DMGRSetData = (FuncDMGRSetData)dlsym(m_pLibDrmManagerHandle, "DMGRSetData");
    if (nullptr == DMGRSetData) {
      LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
      return;
    }

    FuncDMGRReleaseDRMSession DMGRReleaseDRMSession = nullptr;
    DMGRReleaseDRMSession = (FuncDMGRReleaseDRMSession)dlsym(
        m_pLibDrmManagerHandle, "DMGRReleaseDRMSession");
    if (nullptr == DMGRReleaseDRMSession) {
      LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
      return;
    }

    ret = DMGRSetData(m_DRMSession, "Finalize", nullptr);
    if (ret != DM_ERROR_NONE) {
      LOG_INFO("SetData Finalize failed");
    }
    LOG_INFO("SetData Finalize succeed");

    ret = DMGRReleaseDRMSession(m_DRMSession);
    if (ret != DM_ERROR_NONE) {
      LOG_INFO("ReleaseDRMSession failed");
    }
    LOG_INFO("ReleaseDRMSession succeed");
    m_DRMSession = nullptr;
  }

  // close dlopen handle.
  dlclose(m_pLibDrmManagerHandle);
  m_pLibDrmManagerHandle = nullptr;
}

void get_screen_resolution(int *width, int *height) {
  if (system_info_get_custom_int(FMS_KEY_OSD_W, width) !=
      SYSTEM_INFO_ERROR_NONE) {
    LOG_ERROR("Failed to get the horizontal OSD resolution, use default 1920");
    *width = 1920;
  }

  if (system_info_get_custom_int(FMS_KEY_OSD_H, height) !=
      SYSTEM_INFO_ERROR_NONE) {
    LOG_ERROR("Failed to get the vertical OSD resolution, use default 1080");
    *height = 1080;
  }
  LOG_INFO("OSD Resolution is %d %d", *width, *height);
}

VideoPlayer::VideoPlayer(FlutterDesktopPluginRegistrarRef registrar_ref,
                         flutter::PluginRegistrar *plugin_registrar,
                         const std::string &uri, VideoPlayerOptions &options) {
  is_initialized_ = false;
  m_sourceId = 0;
  m_DrmType = options.getDrmType();
  LOG_INFO("[VideoPlayer] m_DrmType %d", m_DrmType);
  m_LicenseUrl = options.getLicenseServerUrl();
  LOG_INFO("[VideoPlayer] getLicenseServerUrl %s", m_LicenseUrl.c_str());
  LOG_INFO("uri: %s", uri.c_str());

  m_DRMSession = nullptr;
  PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
  plusplayer_ = instance.CreatePlayer();
  if (plusplayer_ != nullptr) {
    instance.SetBufferingCallback(plusplayer_, onBuffering, this);
    instance.SetCompletedCallback(plusplayer_, onPlayCompleted, this);
    instance.SetPreparedCallback(plusplayer_, onPrepared, this);
    instance.SetSeekCompletedCallback(plusplayer_, onSeekCompleted, this);
    instance.SetErrorCallback(plusplayer_, onError, this);
    instance.SetErrorMessageCallback(plusplayer_, onErrorMessage, this);
    instance.SetAdaptiveStreamingControlCallback(
        plusplayer_, onPlayerAdaptiveStreamingControl, this);
    instance.SetDrmInitDataCallback(plusplayer_, onDrmInitData, this);
    LOG_INFO("[PlusPlayer]call Open to set uri (%s)", uri.c_str());
    if (!instance.Open(plusplayer_, uri.c_str())) {
      LOG_ERROR("Open uri(%s) failed", uri.c_str());
      throw VideoPlayerError("PlusPlayer", "Open failed");
    }
    LOG_INFO("[PlusPlayer]call SetAppId");
    char *appId = nullptr;
    long pid = getpid();
    int ret = app_manager_get_app_id(pid, &appId);
    if (ret == APP_MANAGER_ERROR_NONE) {
      LOG_INFO("set app id: %s", appId);
      instance.SetAppId(plusplayer_, appId);
    }
    if (appId) {
      free(appId);
    }
    LOG_INFO("[PlusPlayer]call RegisterListener");

    // DRM Function
    if (m_DrmType != DRM_TYPE_NONE)  // DRM video
    {
      m_InitializeDrmSession(uri, m_DrmType);
    }
    int width = 0;
    int height = 0;
    get_screen_resolution(&width, &height);
    LOG_INFO("[PlusPlayer]call SetDisplay");
    if (!instance.SetDisplay(
            plusplayer_, plusplayer::DisplayType::kOverlay,
            instance.GetSurfaceId(plusplayer_,
                                  FlutterDesktopGetWindow(registrar_ref)),
            0, 0, width, height)) {
      LOG_ERROR("Set display failed");
      throw VideoPlayerError("PlusPlayer", "set display failed");
    }
    if (!instance.SetDisplayMode(plusplayer_,
                                 plusplayer::DisplayMode::kDstRoi)) {
      LOG_ERROR("set display mode failed");
      throw VideoPlayerError("PlusPlayer", "set display mode failed");
    }
    LOG_INFO("[PlusPlayer]call PrepareAsync");
    if (!instance.PrepareAsync(plusplayer_)) {
      LOG_ERROR("Parepare async failed");
      throw VideoPlayerError("PlusPlayer", "prepare async failed");
    }
  } else {
    LOG_ERROR("Create operation failed");
    throw VideoPlayerError("PlusPlayer", "Create operation failed");
  }
  texture_id_ = gPlayerIndex++;
  setupEventChannel(plugin_registrar->messenger());
}

void VideoPlayer::setDisplayRoi(int x, int y, int w, int h) {
  LOG_INFO("setDisplayRoi PlusPlayer x = %d, y = %d, w = %d, h = %d", x, y, w,
           h);
  if (plusplayer_ == nullptr) {
    LOG_ERROR("Plusplayer isn't created");
    throw VideoPlayerError("PlusPlayer", "Not created");
  }
  plusplayer::Geometry roi;
  roi.x = x;
  roi.y = y;
  roi.w = w;
  roi.h = h;
  bool ret =
      PlusPlayerWrapperProxy::GetInstance().SetDisplayRoi(plusplayer_, roi);

  if (!ret) {
    LOG_ERROR("Plusplayer SetDisplayRoi failed");
    throw VideoPlayerError("PlusPlayer", "SetDisplayRoi failed");
  }
}

VideoPlayer::~VideoPlayer() {
  LOG_INFO("[VideoPlayer] destructor");
  // DRM Function
  if (m_DrmType != DRM_TYPE_NONE) {
    m_ReleaseDrmSession();
  }
  dispose();
}

long VideoPlayer::getTextureId() { return texture_id_; }

void VideoPlayer::play() {
  LOG_INFO("start PlusPlayer");
  if (plusplayer_ == nullptr) {
    LOG_ERROR("Plusplayer isn't created");
    throw VideoPlayerError("PlusPlayer", "Not created");
  }
  PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
  if (instance.GetState(plusplayer_) < plusplayer::State::kReady) {
    LOG_ERROR("Invalid state for play operation");
    throw VideoPlayerError("PlusPlayer", "Invalid state for play operation");
  }

  if (instance.GetState(plusplayer_) == plusplayer::State::kReady) {
    if (!instance.Start(plusplayer_)) {
      LOG_ERROR("Start operation failed");
      throw VideoPlayerError("PlusPlayer", "Start operation failed");
    }
  } else if (instance.GetState(plusplayer_) == plusplayer::State::kPaused) {
    if (!instance.Resume(plusplayer_)) {
      LOG_ERROR("Resume operation failed");
      throw VideoPlayerError("PlusPlayer", "Resume operation failed");
    }
  }
}

void VideoPlayer::pause() {
  LOG_INFO("pause PlusPlayer");
  if (plusplayer_ == nullptr) {
    LOG_ERROR("Plusplayer isn't created");
    throw VideoPlayerError("PlusPlayer", "Not created");
  }

  PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
  if (instance.GetState(plusplayer_) < plusplayer::State::kReady) {
    LOG_ERROR("Invalid state for pause operation");
    throw VideoPlayerError("PlusPlayer", "Invalid state for pause operation");
  }

  if (instance.GetState(plusplayer_) == plusplayer::State::kPlaying) {
    if (!instance.Pause(plusplayer_)) {
      LOG_ERROR("Pause operation failed");
      throw VideoPlayerError("PlusPlayer", "Pause operation failed");
    }
  }
}

void VideoPlayer::setLooping(bool is_looping) {
  LOG_ERROR("loop operation not supported");
}

void VideoPlayer::setVolume(double volume) {
  LOG_ERROR("PlusPlayer doesn't support to set volume");
}

void VideoPlayer::setPlaybackSpeed(double speed) {
  LOG_INFO("set playback speed: %f", speed);
  if (plusplayer_ == nullptr) {
    LOG_ERROR("Plusplayer isn't created");
    throw VideoPlayerError("PlusPlayer", "Not created");
  }
  PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
  if (!instance.SetPlaybackRate(plusplayer_, speed)) {
    LOG_ERROR("SetPlaybackRate failed");
    throw VideoPlayerError("PlusPlayer", "SetPlaybackRate operation failed");
  }
}

void VideoPlayer::seekTo(int position,
                         const SeekCompletedCb &seek_completed_cb) {
  LOG_INFO("seekTo position: %d", position);
  if (plusplayer_ == nullptr) {
    LOG_ERROR("Plusplayer isn't created");
    throw VideoPlayerError("PlusPlayer", "Not created");
  }
  on_seek_completed_ = seek_completed_cb;
  PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
  if (!instance.Seek(plusplayer_, (unsigned long long)position)) {
    on_seek_completed_ = nullptr;
    LOG_ERROR("Seek to position %d failed", position);
    throw VideoPlayerError("PlusPlayer", "Seek operation failed");
  }
}

int VideoPlayer::getPosition() {
  LOG_INFO("get video player position");
  if (plusplayer_ == nullptr) {
    LOG_ERROR("Plusplayer isn't created");
    throw VideoPlayerError("PlusPlayer", "Not created");
  }

  PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
  plusplayer::State state = instance.GetState(plusplayer_);
  if (state == plusplayer::State::kPlaying ||
      state == plusplayer::State::kPaused) {
    uint64_t position;
    instance.GetPlayingTime(plusplayer_, &position);
    LOG_INFO("playing time: %lld", position);
    return (int)position;
  } else {
    LOG_ERROR("Invalid state for GetPlayingTime operation");
    throw VideoPlayerError("PlusPlayer",
                           "Invalid state for GetPlayingTime operation");
  }
}

void VideoPlayer::dispose() {
  LOG_INFO("[VideoPlayer.dispose] dispose video player start");
  is_initialized_ = false;
  event_sink_ = nullptr;
  event_channel_->SetStreamHandler(nullptr);

  if (plusplayer_) {
    PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
    instance.UnsetBufferingCallback(plusplayer_);
    instance.UnsetCompletedCallback(plusplayer_);
    instance.UnsetPreparedCallback(plusplayer_);
    instance.UnsetSeekCompletedCallback(plusplayer_);
    instance.UnsetErrorCallback(plusplayer_);
    instance.UnsetErrorMessageCallback(plusplayer_);
    instance.UnsetAdaptiveStreamingControlCallback(plusplayer_);
    instance.UnsetDrmInitDataCallback(plusplayer_);
    instance.Stop(plusplayer_);
    instance.Close(plusplayer_);
    instance.DestroyPlayer(plusplayer_);
    plusplayer_ = nullptr;
  }
  LOG_INFO("[VideoPlayer.dispose] dispose video player end");
}

void VideoPlayer::setupEventChannel(flutter::BinaryMessenger *messenger) {
  LOG_INFO("[VideoPlayer.setupEventChannel] setup event channel");
  std::string name =
      "flutter.io/videoPlayer/videoEvents" + std::to_string(texture_id_);
  auto channel =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          messenger, name, &flutter::StandardMethodCodec::GetInstance());
  // SetStreamHandler be called after player_prepare,
  // because initialized event will be send in listen function of event channel
  auto handler = std::make_unique<
      flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
      [&](const flutter::EncodableValue *arguments,
          std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> &&events)
          -> std::unique_ptr<
              flutter::StreamHandlerError<flutter::EncodableValue>> {
        LOG_INFO(
            "[VideoPlayer.setupEventChannel] call listen of StreamHandler");
        event_sink_ = std::move(events);
        initialize();
        return nullptr;
      },
      [&](const flutter::EncodableValue *arguments)
          -> std::unique_ptr<
              flutter::StreamHandlerError<flutter::EncodableValue>> {
        LOG_INFO(
            "[VideoPlayer.setupEventChannel] call cancel of StreamHandler");
        event_sink_ = nullptr;
        return nullptr;
      });
  channel->SetStreamHandler(std::move(handler));
  event_channel_ = std::move(channel);
}

void VideoPlayer::initialize() {
  PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
  plusplayer::State state = instance.GetState(plusplayer_);
  LOG_INFO("[VideoPlayer.initialize] player state: %d", state);
  if (state == plusplayer::State::kReady && !is_initialized_) {
    sendInitialized();
  }
}

void VideoPlayer::sendInitialized() {
  if (!is_initialized_ && event_sink_ != nullptr) {
    PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
    int64_t duration;
    if (!instance.GetDuration(plusplayer_, &duration)) {
      LOG_ERROR("PlusPlayer - GetDuration operation failed");
      event_sink_->Error("PlusPlayer", "GetDuration operation failed");
      return;
    }
    LOG_INFO("[VideoPlayer.sendInitialized] video duration: %lld", duration);

    int width, height;
    if (!instance.GetVideoSize(plusplayer_, &width, &height)) {
      LOG_ERROR("PlusPlayer -> Get video size failed");
      event_sink_->Error("PlusPlayer - Get video size failed");
      return;
    }
    LOG_INFO("video widht: %d, video height: %d", width, height);

    plusplayer::DisplayRotation rotate;
    if (!instance.GetDisplayRotate(plusplayer_, &rotate)) {
      LOG_ERROR("PlusPlayer - GetDisplayRotate operation failed");
      event_sink_->Error("", "PlusPlayer - GetDisplayRotate operation failed");
    } else {
      if (rotate == plusplayer::DisplayRotation::kRotate90 ||
          rotate == plusplayer::DisplayRotation::kRotate270) {
        int tmp = width;
        width = height;
        height = tmp;
      }
    }
    is_initialized_ = true;
    flutter::EncodableMap encodables = {
        {flutter::EncodableValue("event"),
         flutter::EncodableValue("initialized")},
        {flutter::EncodableValue("duration"),
         flutter::EncodableValue(duration)},
        {flutter::EncodableValue("width"), flutter::EncodableValue(width)},
        {flutter::EncodableValue("height"), flutter::EncodableValue(height)}};
    flutter::EncodableValue eventValue(encodables);
    LOG_INFO("[VideoPlayer.sendInitialized] send initialized event");
    event_sink_->Success(eventValue);
  }
}

void VideoPlayer::sendBufferingStart() {
  if (event_sink_) {
    flutter::EncodableMap encodables = {
        {flutter::EncodableValue("event"),
         flutter::EncodableValue("bufferingStart")}};
    flutter::EncodableValue eventValue(encodables);
    LOG_INFO("[VideoPlayer.onBuffering] send bufferingStart event");
    event_sink_->Success(eventValue);
  }
}

void VideoPlayer::sendBufferingUpdate(int position) {
  if (event_sink_) {
    flutter::EncodableList range = {flutter::EncodableValue(0),
                                    flutter::EncodableValue(position)};
    flutter::EncodableList rangeList = {flutter::EncodableValue(range)};
    flutter::EncodableMap encodables = {
        {flutter::EncodableValue("event"),
         flutter::EncodableValue("bufferingUpdate")},
        {flutter::EncodableValue("values"),
         flutter::EncodableValue(rangeList)}};
    flutter::EncodableValue eventValue(encodables);
    LOG_INFO("[VideoPlayer.onBuffering] send bufferingUpdate event");
    event_sink_->Success(eventValue);
  }
}

void VideoPlayer::sendBufferingEnd() {
  if (event_sink_) {
    flutter::EncodableMap encodables = {
        {flutter::EncodableValue("event"),
         flutter::EncodableValue("bufferingEnd")}};
    flutter::EncodableValue eventValue(encodables);
    LOG_INFO("[VideoPlayer.onBuffering] send bufferingEnd event");
    event_sink_->Success(eventValue);
  }
}

void VideoPlayer::onPrepared(bool ret, void *data) {
  VideoPlayer *player = (VideoPlayer *)data;
  LOG_INFO("[VideoPlayer.onPrepared] video player is prepared");

  if (!player->is_initialized_) {
    player->sendInitialized();
  }
}

void VideoPlayer::onBuffering(int percent, void *data) {
  // percent isn't used for video size, it's the used storage of buffer
  LOG_INFO("[VideoPlayer.onBuffering] percent: %d", percent);
}

void VideoPlayer::onSeekCompleted(void *data) {
  VideoPlayer *player = (VideoPlayer *)data;
  LOG_INFO("[VideoPlayer.onSeekCompleted] completed to seek");

  if (player->on_seek_completed_) {
    player->on_seek_completed_();
    player->on_seek_completed_ = nullptr;
  }
}

void VideoPlayer::onPlayCompleted(void *data) {
  VideoPlayer *player = (VideoPlayer *)data;
  LOG_INFO("[VideoPlayer.onPlayCompleted] completed to play video");

  if (player->event_sink_) {
    flutter::EncodableMap encodables = {{flutter::EncodableValue("event"),
                                         flutter::EncodableValue("completed")}};
    flutter::EncodableValue eventValue(encodables);
    LOG_INFO("[VideoPlayer.onPlayCompleted] send completed event");
    player->event_sink_->Success(eventValue);

    LOG_INFO("[VideoPlayer.onPlayCompleted] change player state to pause");
    player->pause();
  }
}

void VideoPlayer::onPlaying(void *data) {}
void VideoPlayer::onError(const plusplayer::ErrorType &error_code,
                          void *user_data) {}
void VideoPlayer::onErrorMessage(const plusplayer::ErrorType &error_code,
                                 const char *error_msg, void *user_data) {}
void VideoPlayer::onPlayerAdaptiveStreamingControl(
    const plusplayer::StreamingMessageType &type,
    const plusplayer::MessageParam &msg, void *user_data) {
  LOG_INFO("OnAdaptiveStreamingControlEvent");
  VideoPlayer *player = (VideoPlayer *)user_data;
  LOG_INFO(
      "CALL: Type[%d]",
      static_cast<std::underlying_type<plusplayer::StreamingMessageType>::type>(
          type));

  if (type == plusplayer::StreamingMessageType::kDrmInitData) {
    LOG_INFO("msg size:%d", msg.size);
    char *pssh = new char[msg.size];
    if (nullptr == pssh) {
      LOG_ERROR("Memory Allocation Failed");
      return;
    }

    if (true == msg.data.empty() || 0 == msg.size) {
      LOG_ERROR("Empty data.");
      return;
    }

    FuncDMGRSetData DMGRSetData = nullptr;
    DMGRSetData =
        (FuncDMGRSetData)dlsym(player->m_pLibDrmManagerHandle, "DMGRSetData");
    if (nullptr == DMGRSetData) {
      LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
      return;
    }
    memcpy(pssh, msg.data.c_str(), msg.size);
    SetDataParam_t psshDataParam;
    psshDataParam.param1 = (void *)pssh;
    psshDataParam.param2 = (void *)msg.size;
    int ret = DMGRSetData(player->m_DRMSession, (char *)"update_pssh_data",
                          (void *)&psshDataParam);
    if (ret != DM_ERROR_NONE) {
      LOG_ERROR("setdata failed for renew callback");
      delete[] pssh;
      return;
    }
    delete[] pssh;
  }
}

void VideoPlayer::onDrmInitData(int *drmhandle, unsigned int len,
                                unsigned char *psshdata,
                                plusplayer::TrackType type, void *user_data) {
  LOG_INFO("OnDrmInitData");
  VideoPlayer *player = (VideoPlayer *)user_data;

  FuncDMGRSecurityInitCompleteCB DMGRSecurityInitCompleteCB = nullptr;
  DMGRSecurityInitCompleteCB = (FuncDMGRSecurityInitCompleteCB)dlsym(
      player->m_pLibDrmManagerHandle, "DMGRSecurityInitCompleteCB");
  if (nullptr == DMGRSecurityInitCompleteCB) {
    LOG_ERROR("[VideoPlayer] Symbol not found %s: ", dlerror());
    return;
  }

  SetDataParam_t setDataParam;
  setDataParam.param2 = player->m_DRMSession;
  if (DMGRSecurityInitCompleteCB(drmhandle, len, psshdata,
                                 (void *)&setDataParam)) {
    LOG_INFO("DMGRSecurityInitCompleteCB sucessfully!");
    PlusPlayerWrapperProxy &instance = PlusPlayerWrapperProxy::GetInstance();
    instance.DrmLicenseAcquiredDone(player->plusplayer_, type);
  }
}
