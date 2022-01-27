#ifndef VIDEO_PLAYER_PLUGIN_DRM_LICENCE_H
#define VIDEO_PLAYER_PLUGIN_DRM_LICENCE_H
typedef long DRM_RESULT;

#define DRM_SUCCESS ((DRM_RESULT)0x00000000L)
#define DRM_E_POINTER ((DRM_RESULT)0x80004003L)
#define DRM_E_INVALIDARG ((DRM_RESULT)0x80070057L)
#define DRM_E_NETWORK ((DRM_RESULT)0x91000000L)
#define DRM_E_NETWORK_CURL ((DRM_RESULT)0x91000001L)
#define DRM_E_NETWORK_HOST ((DRM_RESULT)0x91000002L)
#define DRM_E_NETWORK_CLIENT ((DRM_RESULT)0x91000003L)
#define DRM_E_NETWORK_SERVER ((DRM_RESULT)0x91000004L)
#define DRM_E_NETWORK_HEADER ((DRM_RESULT)0x91000005L)
#define DRM_E_NETWORK_REQUEST ((DRM_RESULT)0x91000006L)
#define DRM_E_NETWORK_RESPONSE ((DRM_RESULT)0x91000007L)
#define DRM_E_NETWORK_CANCELED ((DRM_RESULT)0x91000008L)

class VideoPlayerDrmLicenseHelper {
 public:
  enum DrmTypeHelper {
    DRM_TYPE_NONE = 0,
    DRM_TYPE_PLAYREADY,
    DRM_TYPE_WIDEVINE,
  };

  struct ExtensionCtxTZ {
    char* soap_header;
    char* http_header;
    char* user_agent;
    bool cancel_request;

    ExtensionCtxTZ() {
      soap_header = nullptr;
      http_header = nullptr;
      user_agent = nullptr;
      cancel_request = false;
    }
  };

 public:
  static DRM_RESULT DoTransactionTZ(const char* server_url,
                                    const void* challenge_data,
                                    unsigned long challenge_data_length,
                                    unsigned char** response_data,
                                    unsigned long* response_data_length,
                                    DrmTypeHelper drm_type, const char* cookie,
                                    ExtensionCtxTZ* extentsion_context);
};

#endif  // VIDEO_PLAYER_PLUGIN_DRM_LICENCE_H
