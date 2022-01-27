#ifndef VIDEO_PLAYER_H_
#define VIDEO_PLAYER_H_

#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/plugin_registrar.h>
#include <flutter_tizen.h>
#include <glib.h>

#include <mutex>
#include <string>

#include "plus_player_wrapper_proxy.h"
#include "video_player_options.h"

typedef enum {
  PLUS_PLAYER_STATE_NONE,    /**< Player is not created */
  PLUS_PLAYER_STATE_IDLE,    /**< Player is created, but not prepared */
  PLUS_PLAYER_STATE_READY,   /**< Player is ready to play media */
  PLUS_PLAYER_STATE_PLAYING, /**< Player is playing media */
  PLUS_PLAYER_STATE_PAUSED,  /**< Player is paused while playing media */
} plusplayer_state_e;

typedef enum {
  DRM_TYPE_NONE,
  DRM_TYPE_PLAYREADAY,
  DRM_TYPE_WIDEVINECDM,
} DRMTYPE;

typedef enum {
  DM_ERROR_NONE = 0,                /**< Success */
  DM_ERROR_INVALID_PARAM,           /**< Invalid parameter */
  DM_ERROR_INVALID_OPERATE,         /**< Invalid operation */
  DM_ERROR_INVALID_HANDLE,          /**< Invalid handle */
  DM_ERROR_INTERNAL_ERROR,          /**< Internal error */
  DM_ERROR_TIMEOUT,                 /**< Timeout */
  DM_ERROR_MANIFEST_DOWNLOAD_ERROR, /**< Manifest download error */
  DM_ERROR_MANIFEST_PARSE_ERROR,    /**< Manifest parse error */
  DM_ERROR_FIND_NOPSSHDATA,         /**< No pssh data */

  DM_ERROR_MALLOC = 10, /**< Malloc error */
  DM_ERROR_DL,          /**< Load so error */

  DM_ERROR_INVALID_URL = 20,       /**< Invalid url */
  DM_ERROR_INVALID_SESSION,        /**< Invalid session */
  DM_ERROR_UNSUPPORTED_URL_SUFFIX, /**< Unsupported url suffix */
  DM_ERROR_INITIALIZE_FAILED,      /**< Failed to initialize DRM */

  DM_ERROR_DASH_INIT = 30, /**< DASH init failed */
  DM_ERROR_DASH_CLOSE,     /**< DASH close failed */
  DM_ERROR_DASH_OPEN,      /**< DASH open failed */

  DM_ERROR_DRM_WEB_SET = 40, /**< DRM web set failed */

  DM_ERROR_PR_HANDLE_CREATE = 50, /**< Playready handle create failed */
  DM_ERROR_PR_OPEN,               /**< Playready open failed */
  DM_ERROR_PR_DESTROY,            /**< Playready destroy failed */
  DM_ERROR_PR_GENCHALLENGE,       /**< Playready genchallenge failed */
  DM_ERROR_PR_INSTALL_LICENSE,    /**< Playready install license failed */
  DM_ERROR_PR_GETRIGHTS,          /**< Playready get rights failed */
  DM_ERROR_PR_STATUS,             /**< Playready get status failed */

  DM_ERROR_VMX_HANDLE_CREATE = 60, /**< Verimatrix handle create failed */
  DM_ERROR_VMX_FINALIZE,           /**< Verimatrix finalize failed */
  DM_ERROR_VMX_GET_UNIQUE_ID,      /**< Verimatrix get unique ID failed */

  DM_ERROR_MARLIN_OPEN = 70,   /**< Marlin open failed */
  DM_ERROR_MARLIN_CLOSE,       /**< Marlin close failed */
  DM_ERROR_MARLIN_GET_RIGHTS,  /**< Marlin get rights failed */
  DM_ERROR_MARLIN_GET_LICENSE, /**< Marlin get license failed */

  DM_ERROR_WVCDM_HANDLE_CREATE = 80,  /**< Widevinecdm handle create failed */
  DM_ERROR_WVCDM_DESTROY,             /**< Widevinecdm destroy failed */
  DM_ERROR_WVCDM_OPEN_SESSION,        /**< Widevinecdm open failed */
  DM_ERROR_WVCDM_CLOSE_SESSION,       /**< Widevinecdm close failed */
  DM_ERROR_WVCDM_GET_PROVISION,       /**< Widevinecdm get provision failed */
  DM_ERROR_WVCDM_GENERATE_KEYREQUEST, /**< Widevinecdm generate key request
                                         failed */
  DM_ERROR_WVCDM_ADD_KEY,             /**< Widevinecdm add key failed */
  DM_ERROR_WVCDM_REGISTER_EVENT,      /**< Widevinecdm register event failed */

  DM_ERROR_EME_SESSION_HANDLE_CREATE = 90, /**< EME handle create failed */
  DM_ERROR_EME_SESSION_CREATE,             /**< EME session create failed */
  DM_ERROR_EME_SESSION_DESTROY,            /**< EME session destroy failed */
  DM_ERROR_EME_SESSION_UPDATE,             /**< EME session update failed */
  DM_ERROR_EME_SESSION_REQUEST,            /**< EME session request failed */
  DM_ERROR_EME_WEB_OPERATION,              /**< EME web operation failed */
  DM_ERROR_EME_TYPE_NOT_SUPPORTED,         /**< EME type not supported */
  //...
  DM_ERROR_UNKOWN,
} dm_error_e;

typedef enum {
  DM_TYPE_NONE = 0,             /**< None */
  DM_TYPE_PLAYREADY = 1,        /**< Playready */
  DM_TYPE_MARLINMS3 = 2,        /**< Marlinms3 */
  DM_TYPE_VERIMATRIX = 3,       /**< Verimatrix */
  DM_TYPE_WIDEVINE_CLASSIC = 4, /**< Widevine classic */
  DM_TYPE_SECUREMEDIA = 5,      /**< Securemedia */
  DM_TYPE_SDRM = 6,             /**< SDRM */
  DM_TYPE_VUDU = 7,             /**< Vudu */
  DM_TYPE_WIDEVINE = 8,         /**< Widevine cdm */
  DM_TYPE_LYNK = 9,             /**< Lynk */
  DM_TYPE_CLEARKEY = 13,        /**< Clearkey */
  DM_TYPE_EME = 14,             /**< EME */
  //...
  DM_TYPE_MAX,
} dm_type_e;

typedef struct SetDataParam_s {
  void* param1; /**< Parameter 1 */
  void* param2; /**< Parameter 2 */
  void* param3; /**< Parameter 3 */
  void* param4; /**< Parameter 4 */
} SetDataParam_t;

typedef void* DRMSessionHandle_t;
// DrmManager function porinter typedef

typedef void (*FuncDMGRSetDRMLocalMode)();
typedef DRMSessionHandle_t (*FuncDMGRCreateDRMSession)(
    dm_type_e type, const char* drm_sub_type);
typedef int (*FuncDMGRSetData)(DRMSessionHandle_t drm_session,
                               const char* data_type, void* input_data);
typedef int (*FuncDMGRGetData)(DRMSessionHandle_t drm_session,
                               const char* data_type, void* output_data);
typedef bool (*FuncDMGRSecurityInitCompleteCB)(int* drm_handle,
                                               unsigned int len,
                                               unsigned char* pssh_data,
                                               void* user_data);
typedef int (*FuncDMGRReleaseDRMSession)(DRMSessionHandle_t drm_session);

using SeekCompletedCb = std::function<void()>;

class VideoPlayer {
 public:
  VideoPlayer(FlutterDesktopPluginRegistrarRef registrar_ref,
              flutter::PluginRegistrar* plugin_registrar,
              const std::string& uri, VideoPlayerOptions& options);
  ~VideoPlayer();

  long getTextureId();
  void play();
  void pause();
  void setLooping(bool is_looping);
  void setVolume(double volume);
  void setPlaybackSpeed(double speed);
  void seekTo(int position,
              const SeekCompletedCb& seek_completed_cb);  // milliseconds
  int getPosition();                                      // milliseconds
  void dispose();
  void setDisplayRoi(int x, int y, int w, int h);

 private:
  // DRM Function
  void m_InitializeDrmSession(const std::string& uri, int nDrmType);
  void m_ReleaseDrmSession();
  static void m_CbDrmManagerError(long errCode, char* errMsg, void* userData);
  static int m_CbChallengeData(void* session_id, int msgType, void* msg,
                               int msgLen, void* userData);
  static gboolean m_InstallEMEKey(void* pData);

  unsigned char* m_pbResponse;
  SetDataParam_t m_licenseparam;
  DRMSessionHandle_t m_DRMSession;
  int m_DrmType;
  unsigned int m_sourceId;
  std::string m_LicenseUrl;
  void* m_pLibDrmManagerHandle = NULL;
  // end DRM
  void initialize();
  void setupEventChannel(flutter::BinaryMessenger* messenger);
  void sendInitialized();
  void sendBufferingStart();
  void sendBufferingUpdate(int position);  // milliseconds
  void sendBufferingEnd();
  void sendSeeking(bool seeking);

  static void onPrepared(bool ret, void* data);
  static void onBuffering(int percent, void* data);
  static void onSeekCompleted(void* data);
  static void onPlayCompleted(void* data);
  static void onPlaying(void* data);
  static void onError(const plusplayer::ErrorType& error_code, void* user_data);
  static void onErrorMessage(const plusplayer::ErrorType& error_code,
                             const char* error_msg, void* user_data);
  static void onPlayerAdaptiveStreamingControl(
      const plusplayer::StreamingMessageType& type,
      const plusplayer::MessageParam& msg, void* user_data);
  static void onDrmInitData(int* drmhandle, unsigned int len,
                            unsigned char* psshdata, plusplayer::TrackType type,
                            void* user_data);

  bool is_initialized_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      event_channel_;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;
  long texture_id_;
  PlusPlayerRef plusplayer_{nullptr};
  SeekCompletedCb on_seek_completed_;
};

#endif  // VIDEO_PLAYER_H_
