// Released under the MIT License. See LICENSE for details.

#ifndef BALLISTICA_BASE_BASE_H_
#define BALLISTICA_BASE_BASE_H_

#include <set>
#include <string>

#include "ballistica/core/support/base_soft.h"
#include "ballistica/shared/foundation/feature_set_native_component.h"

// Common header that most everything using our feature-set should include.
// It predeclares our feature-set's various types and globals and other
// bits.

// Predeclared types from other feature sets that we use.
namespace ballistica::core {
class CoreConfig;
class CoreFeatureSet;
}  // namespace ballistica::core

namespace ballistica::base {

// Predeclare types we use throughout our FeatureSet so most headers can get
// away with just including this header.
class AppAdapter;
class AppConfig;
class AppTimer;
class AppMode;
class PlusSoftInterface;
class AreaOfInterest;
class Assets;
class Audio;
class AudioServer;
class AudioStreamer;
class AudioSource;
class BaseFeatureSet;
class BasePlatform;
class BasePython;
class BGDynamics;
class BGDynamicsServer;
class BGDynamicsDrawSnapshot;
class BGDynamicsEmission;
class BGDynamicsFuse;
struct BGDynamicsFuseData;
class BGDynamicsHeightCache;
class BGDynamicsShadow;
struct BGDynamicsShadowData;
class BGDynamicsVolumeLight;
struct BGDynamicsVolumeLightData;
class Camera;
class ClassicSoftInterface;
class CollisionMeshAsset;
class CollisionCache;
class Console;
class Context;
class ContextRef;
class DataAsset;
class FrameDef;
class GLContext;
class Graphics;
class GraphicsServer;
class Huffman;
class ImageMesh;
class Input;
class InputDevice;
class InputDeviceDelegate;
class JoystickInput;
class KeyboardInput;
class Logic;
class Asset;
class AssetsServer;
class MeshBufferBase;
class MeshBufferVertexSprite;
class MeshBufferVertexSimpleFull;
class MeshBufferVertexSmokeFull;
class Mesh;
class MeshData;
class MeshDataClientHandle;
class MeshIndexBuffer16;
class MeshIndexedSimpleFull;
class MeshIndexedSmokeFull;
class MeshRendererData;
class MeshAsset;
class MeshAssetRendererData;
class NetClientThread;
class NetGraph;
class Networking;
class NetworkReader;
class NetworkWriter;
class ObjectComponent;
class PythonClassUISound;
class PythonContextCall;
class Renderer;
class RenderComponent;
class RenderCommandBuffer;
class RenderPass;
class RenderTarget;
class RemoteAppServer;
class RemoteControlInput;
class ScoreToBeat;
class AppAdapterSDL;
class SDLContext;
class SoundAsset;
class SpriteMesh;
class StdioConsole;
class StressTest;
class Module;
class TestInput;
class TextGroup;
class TextGraphics;
class TextMesh;
class TextPacker;
class TextureAsset;
class TextureAssetPreloadData;
class TextureAssetRendererData;
class TouchInput;
class UI;
class UIV1SoftInterface;
class AppAdapterVR;
class GraphicsVR;

enum class AssetType {
  kTexture,
  kCollisionMesh,
  kMesh,
  kSound,
  kData,
  kLast,
};

enum class DrawType {
  kTriangles,
  kPoints,
};

/// Hints to the renderer - stuff that is changed rarely should be static,
/// and stuff changed often should be dynamic.
enum class MeshDrawType {
  kStatic,
  kDynamic,
};

enum class ReflectionType {
  kNone,
  kChar,
  kPowerup,
  kSoft,
  kSharp,
  kSharper,
  kSharpest,
};

enum class GraphicsQuality {
  /// Quality has not yet been set.
  kUnset,
  /// Bare minimum graphics.
  kLow,
  /// Basic graphics; no post-processing.
  kMedium,
  /// Graphics with bare minimum post-processing.
  kHigh,
  /// Graphics with full post-processing.
  kHigher,
};

/// Requests for exact or auto graphics quality values.
enum class GraphicsQualityRequest {
  kUnset,
  kLow,
  kMedium,
  kHigh,
  kHigher,
  kAuto,
};

// Standard vertex structs used in rendering/fileIO/etc.
// Remember to make sure components are on 4 byte boundaries.
// (need to find out how strict we need to be on Metal, Vulkan, etc).

struct VertexSimpleSplitStatic {
  uint16_t uv[2];
};

struct VertexSimpleSplitDynamic {
  float position[3];
};

struct VertexSimpleFull {
  float position[3];
  uint16_t uv[2];
};

struct VertexDualTextureFull {
  float position[3];
  uint16_t uv[2];
  uint16_t uv2[2];
};

struct VertexObjectSplitStatic {
  uint16_t uv[2];
};

struct VertexObjectSplitDynamic {
  float position[3];
  int16_t normal[3];
  int8_t padding[2];
};

struct VertexObjectFull {
  float position[3];
  uint16_t uv[2];
  int16_t normal[3];
  uint8_t padding[2];
};

struct VertexSmokeFull {
  float position[3];
  float uv[2];
  uint8_t color[4];
  uint8_t diffuse;
  uint8_t padding1[3];
  uint8_t erode;
  uint8_t padding2[3];
};

struct VertexSprite {
  float position[3];
  uint16_t uv[2];
  float size;
  float color[4];
};

enum class MeshFormat {
  /// 16bit UV, 8bit normal, 8bit pt-index.
  kUV16N8Index8,
  /// 16bit UV, 8bit normal, 16bit pt-index.
  kUV16N8Index16,
  /// 16bit UV, 8bit normal, 32bit pt-index.
  kUV16N8Index32,
};

enum class TextureType {
  k2D,
  kCubeMap,
};

enum class TextureFormat {
  kNone,
  kRGBA_8888,
  kRGB_888,
  kRGBA_4444,
  kRGB_565,
  kDXT1,
  kDXT5,
  kETC1,
  kPVR2,
  kPVR4,
  kETC2_RGB,
  kETC2_RGBA,
};

enum class TextureCompressionType {
  kS3TC,
  kPVR,
  kETC1,
  kETC2,
};

enum class TextureMinQuality {
  kLow,
  kMedium,
  kHigh,
};

enum class CameraMode {
  kFollow,
  kOrbit,
};

enum class MeshDataType {
  kIndexedSimpleSplit,
  kIndexedObjectSplit,
  kIndexedSimpleFull,
  kIndexedDualTextureFull,
  kIndexedSmokeFull,
  kSprite
};

struct TouchEvent {
  enum class Type { kDown, kUp, kMoved, kCanceled };
  Type type{};
  void* touch{};
  bool overall{};  // For sanity-checks.
  float x{};
  float y{};
};

enum class TextMeshEntryType {
  kRegular,
  kExtras,
  kOSRendered,
};

enum MeshDrawFlags {
  kMeshDrawFlagNoReflection = 1,
};

enum class LightShadowType {
  kNone,
  kTerrain,
  kObject,
};

enum class TextureQualityRequest {
  kUnset,
  kAuto,
  kHigh,
  kMedium,
  kLow,
};
enum class TextureQuality {
  kUnset,
  kHigh,
  kMedium,
  kLow,
};

enum class BenchmarkType {
  kNone,
  kCPU,
  kGPU,
};

#if BA_VR_BUILD
enum class VRHandType {
  kNone,
  kDaydreamRemote,
  kOculusTouchL,
  kOculusTouchR,
};
struct VRHandState {
  VRHandType type = VRHandType::kNone;
  float tx = 0.0f;
  float ty = 0.0f;
  float tz = 0.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;
  float roll = 0.0f;
};
struct VRHandsState {
  VRHandState l;
  VRHandState r;
};
#endif  // BA_VR_BUILD

/// Types of shading.
/// These do not necessarily correspond to actual shader objects in the renderer
/// (a single shader may handle more than one of these, etc).
/// These are simply categories of looks.
enum class ShadingType {
  kSimpleColor,
  kSimpleColorTransparent,
  kSimpleColorTransparentDoubleSided,
  kSimpleTexture,
  kSimpleTextureModulated,
  kSimpleTextureModulatedColorized,
  kSimpleTextureModulatedColorized2,
  kSimpleTextureModulatedColorized2Masked,
  kSimpleTextureModulatedTransparent,
  kSimpleTextureModulatedTransFlatness,
  kSimpleTextureModulatedTransparentDoubleSided,
  kSimpleTextureModulatedTransparentColorized,
  kSimpleTextureModulatedTransparentColorized2,
  kSimpleTextureModulatedTransparentColorized2Masked,
  kSimpleTextureModulatedTransparentShadow,
  kSimpleTexModulatedTransShadowFlatness,
  kSimpleTextureModulatedTransparentGlow,
  kSimpleTextureModulatedTransparentGlowMaskUV2,
  kObject,
  kObjectTransparent,
  kObjectLightShadowTransparent,
  kSpecial,
  kShield,
  kObjectReflect,
  kObjectReflectTransparent,
  kObjectReflectAddTransparent,
  kObjectLightShadow,
  kObjectReflectLightShadow,
  kObjectReflectLightShadowDoubleSided,
  kObjectReflectLightShadowColorized,
  kObjectReflectLightShadowColorized2,
  kObjectReflectLightShadowAdd,
  kObjectReflectLightShadowAddColorized,
  kObjectReflectLightShadowAddColorized2,
  kSmoke,
  kSmokeOverlay,
  kPostProcess,
  kPostProcessEyes,
  kPostProcessNormalDistort,
  kSprite,
  kCount
};

enum class SysTextureID {
  kUIAtlas,
  kButtonSquare,
  kWhite,
  kFontSmall0,
  kFontBig,
  kCursor,
  kBoxingGlove,
  kShield,
  kExplosion,
  kTextClearButton,
  kWindowHSmallVMed,
  kWindowHSmallVSmall,
  kGlow,
  kScrollWidget,
  kScrollWidgetGlow,
  kFlagPole,
  kScorch,
  kScorchBig,
  kShadow,
  kLight,
  kShadowSharp,
  kLightSharp,
  kShadowSoft,
  kLightSoft,
  kSparks,
  kEye,
  kEyeTint,
  kFuse,
  kShrapnel1,
  kSmoke,
  kCircle,
  kCircleOutline,
  kCircleNoAlpha,
  kCircleOutlineNoAlpha,
  kCircleShadow,
  kSoftRect,
  kSoftRect2,
  kSoftRectVertical,
  kStartButton,
  kBombButton,
  kOuyaAButton,
  kBackIcon,
  kNub,
  kArrow,
  kMenuButton,
  kUsersButton,
  kActionButtons,
  kTouchArrows,
  kTouchArrowsActions,
  kRGBStripes,
  kUIAtlas2,
  kFontSmall1,
  kFontSmall2,
  kFontSmall3,
  kFontSmall4,
  kFontSmall5,
  kFontSmall6,
  kFontSmall7,
  kFontExtras,
  kFontExtras2,
  kFontExtras3,
  kFontExtras4,
  kCharacterIconMask,
  kBlack,
  kWings
};

enum class SysCubeMapTextureID {
  kReflectionChar,
  kReflectionPowerup,
  kReflectionSoft,
  kReflectionSharp,
  kReflectionSharper,
  kReflectionSharpest
};

enum class SysSoundID {
  kDeek = 0,
  kBlip,
  kBlank,
  kPunch,
  kClick,
  kErrorBeep,
  kSwish,
  kSwish2,
  kSwish3,
  kTap,
  kCorkPop,
  kGunCock,
  kTickingCrazy,
  kSparkle,
  kSparkle2,
  kSparkle3
};

enum class SystemDataID {};

enum class SysMeshID {
  kButtonSmallTransparent,
  kButtonSmallOpaque,
  kButtonMediumTransparent,
  kButtonMediumOpaque,
  kButtonBackTransparent,
  kButtonBackOpaque,
  kButtonBackSmallTransparent,
  kButtonBackSmallOpaque,
  kButtonTabTransparent,
  kButtonTabOpaque,
  kButtonLargeTransparent,
  kButtonLargeOpaque,
  kButtonLargerTransparent,
  kButtonLargerOpaque,
  kButtonSquareTransparent,
  kButtonSquareOpaque,
  kCheckTransparent,
  kScrollBarThumbTransparent,
  kScrollBarThumbOpaque,
  kScrollBarThumbSimple,
  kScrollBarThumbShortTransparent,
  kScrollBarThumbShortOpaque,
  kScrollBarThumbShortSimple,
  kScrollBarTroughTransparent,
  kTextBoxTransparent,
  kImage1x1,
  kImage1x1FullScreen,
  kImage2x1,
  kImage4x1,
  kImage16x1,
#if BA_VR_BUILD
  kImage1x1VRFullScreen,
  kVROverlay,
  kVRFade,
#endif
  kOverlayGuide,
  kWindowHSmallVMedTransparent,
  kWindowHSmallVMedOpaque,
  kWindowHSmallVSmallTransparent,
  kWindowHSmallVSmallOpaque,
  kSoftEdgeOutside,
  kSoftEdgeInside,
  kBoxingGlove,
  kShield,
  kFlagPole,
  kFlagStand,
  kScorch,
  kEyeBall,
  kEyeBallIris,
  kEyeLid,
  kHairTuft1,
  kHairTuft1b,
  kHairTuft2,
  kHairTuft3,
  kHairTuft4,
  kShrapnel1,
  kShrapnelSlime,
  kShrapnelBoard,
  kShockWave,
  kFlash,
  kCylinder,
  kArrowFront,
  kArrowBack,
  kActionButtonLeft,
  kActionButtonTop,
  kActionButtonRight,
  kActionButtonBottom,
  kBox,
  kLocator,
  kLocatorBox,
  kLocatorCircle,
  kLocatorCircleOutline,
  kCrossOut,
  kWing
};

// Our feature-set's globals.
// Feature-sets should NEVER directly access globals in another feature-set's
// namespace. All functionality we need from other feature-sets should be
// imported into globals in our own namespace. Generally we do this when we
// are initially imported (just as regular Python modules do).
extern core::CoreFeatureSet* g_core;
extern base::BaseFeatureSet* g_base;

/// Our C++ front-end to our feature set. This is what other C++
/// feature-sets can 'Import' from us.
class BaseFeatureSet : public FeatureSetNativeComponent,
                       public core::BaseSoftInterface {
 public:
  /// Instantiates our FeatureSet if needed and returns the single
  /// instance of it. Basically C++ analog to Python import.
  static auto Import() -> BaseFeatureSet*;

  /// Called when our associated Python module is instantiated.
  static void OnModuleExec(PyObject* module);

  /// Start app systems in motion.
  void StartApp() override;

  /// Called when app shutdown process completes. Sets app to exit.
  void OnAppShutdownComplete();

  auto AppManagesEventLoop() -> bool override;

  /// Run app event loop to completion (only applies to flavors which manage
  /// their own event loop).
  void RunAppToCompletion() override;

  void PrimeAppMainThreadEventPump() override;

  auto CurrentContext() -> const ContextRef& {
    assert(InLogicThread());  // Up to caller to ensure this.
    return *context_ref;
  }

  void SetCurrentContext(const ContextRef& context);

  /// Try to load the plus feature-set and return whether it is available.
  auto HavePlus() -> bool;

  /// Access the plus feature-set. Will throw an exception if not present.
  auto plus() -> PlusSoftInterface*;

  void set_plus(PlusSoftInterface* plus);

  /// Try to load the classic feature-set and return whether it is available.
  auto HaveClassic() -> bool;

  /// Access the classic feature-set. Will throw an exception if not present.
  auto classic() -> ClassicSoftInterface*;

  void set_classic(ClassicSoftInterface* classic);

  /// Try to load the ui_v1 feature-set and return whether it is available.
  auto HaveUIV1() -> bool;

  /// Access the ui_v1 feature-set. Will throw an exception if not present.
  auto ui_v1() -> UIV1SoftInterface*;

  void set_ui_v1(UIV1SoftInterface* ui_v1);

  /// Return a string that should be universally unique to this particular
  /// running instance of the app.
  auto GetAppInstanceUUID() -> const std::string&;

  /// Does it appear that we are a blessed build with no known
  /// user-modifications?
  /// Note that some corner cases (such as being called too early in the launch
  /// process) may result in false negatives (saying we're *not* unmodified when
  /// in reality we are unmodified).
  auto IsUnmodifiedBlessedBuild() -> bool override;

  /// Return true if both babase and _babase modules have completed their
  /// import execs. To keep our init order well defined, we want to avoid
  /// allowing certain functionality before this time.
  auto IsBaseCompletelyImported() -> bool;

  auto InAssetsThread() const -> bool override;
  auto InLogicThread() const -> bool override;
  auto InGraphicsThread() const -> bool override;
  auto InAudioThread() const -> bool override;
  auto InBGDynamicsThread() const -> bool override;
  auto InNetworkWriteThread() const -> bool override;

  /// High level screen-message call usable from any thread.
  void ScreenMessage(const std::string& s, const Vector3f& color) override;

  /// Has StartApp been called (and completely finished its work)?
  /// Code that sends calls/messages to other threads or otherwise uses
  /// app functionality may want to check this to avoid crashes.
  auto IsAppStarted() const -> bool override;

  void PlusDirectSendV1CloudLogs(const std::string& prefix,
                                 const std::string& suffix, bool instant,
                                 int* result) override;
  auto CreateFeatureSetData(FeatureSetNativeComponent* featureset)
      -> PyObject* override;
  auto FeatureSetFromData(PyObject* obj) -> FeatureSetNativeComponent* override;
  void DoV1CloudLog(const std::string& msg) override;
  void PushConsolePrintCall(const std::string& msg) override;
  auto GetPyExceptionType(PyExcType exctype) -> PyObject* override;
  auto PrintPythonStackTrace() -> bool override;
  auto GetPyLString(PyObject* obj) -> std::string override;
  auto DoGetContextBaseString() -> std::string override;
  void DoPrintContextAuto() override;
  void DoPushObjCall(const PythonObjectSetBase* objset, int id) override;
  void DoPushObjCall(const PythonObjectSetBase* objset, int id,
                     const std::string& arg) override;
  void OnReachedEndOfBaBaseImport();
  void ShutdownSuppressBegin();
  void ShutdownSuppressEnd();
  auto shutdown_suppress_count() const { return shutdown_suppress_count_; }

  /// Called in the logic thread once our screen is up and assets are
  /// loading.
  void OnAssetsAvailable();

  // Const subsystems.
  AppAdapter* const app_adapter;
  AppConfig* const app_config;
  Assets* const assets;
  AssetsServer* const assets_server;
  Audio* const audio;
  AudioServer* const audio_server;
  BasePlatform* const platform;
  BasePython* const python;
  BGDynamics* const bg_dynamics;
  BGDynamicsServer* const bg_dynamics_server;
  ContextRef* const context_ref;
  Graphics* const graphics;
  GraphicsServer* const graphics_server;
  Huffman* const huffman;
  Input* const input;
  Logic* const logic;
  Networking* const networking;
  NetworkReader* const network_reader;
  NetworkWriter* const network_writer;
  StdioConsole* const stdio_console;
  TextGraphics* const text_graphics;
  UI* const ui;
  Utils* const utils;

  // Variable subsystems.
  auto* console() const { return console_; }
  auto* app_mode() const { return app_mode_; }
  auto* stress_test() const { return stress_test_; }
  void set_app_mode(AppMode* mode);

  /// Whether we're running under ballisticakit_server.py
  /// (affects some app behavior).
  auto server_wrapper_managed() { return server_wrapper_managed_; }

  // Non-const bits (fixme: clean up access to these).
  TouchInput* touch_input{};

 private:
  BaseFeatureSet();
  void LogVersionInfo_();
  void PrintContextNonLogicThread_();
  void PrintContextForCallableLabel_(const char* label);
  void PrintContextUnavailable_();

  AppMode* app_mode_;
  Console* console_{};
  PlusSoftInterface* plus_soft_{};
  ClassicSoftInterface* classic_soft_{};
  UIV1SoftInterface* ui_v1_soft_{};
  StressTest* stress_test_;

  std::string console_startup_messages_;
  int shutdown_suppress_count_{};
  bool tried_importing_plus_{};
  bool tried_importing_classic_{};
  bool tried_importing_ui_v1_{};
  bool called_start_app_{};
  bool app_started_{};
  bool called_run_app_to_completion_{};
  bool base_import_completed_{};
  bool base_native_import_completed_{};
  bool basn_log_behavior_{};
  bool server_wrapper_managed_{};
};

}  // namespace ballistica::base

#endif  // BALLISTICA_BASE_BASE_H_
