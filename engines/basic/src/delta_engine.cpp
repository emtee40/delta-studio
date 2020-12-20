#include "../include/delta_engine.h"

#include "../include/skeleton.h"
#include "../include/render_skeleton.h"
#include "../include/material.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION

#include <stb/stb_rect_pack.h>
#include <stb/stb_truetype.h>

#include <assert.h>

const std::string dbasic::DeltaEngine::FrameBreakdownFull = "Frame Full";
const std::string dbasic::DeltaEngine::FrameBreakdownRenderUI = "UI";
const std::string dbasic::DeltaEngine::FrameBreakdownRenderScene = "Scene";
const dbasic::DeltaEngine::GameEngineSettings dbasic::DeltaEngine::DefaultSettings;

dbasic::DeltaEngine::DeltaEngine() {
    m_device = nullptr;

    m_windowSystem = nullptr;
    m_gameWindow = nullptr;

    m_renderingContext = nullptr;
    m_mainRenderTarget = nullptr;

    m_audioSystem = nullptr;

    m_mainVertexBuffer = nullptr;
    m_mainIndexBuffer = nullptr;

    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    m_shaderProgram = nullptr;

    m_consolePixelShader = nullptr;
    m_consoleVertexShader = nullptr;
    m_consoleProgram = nullptr;

    m_inputLayout = nullptr;

    m_initialized = false;

    m_clearColor[0] = 0.0F;
    m_clearColor[1] = 0.0F;
    m_clearColor[2] = 0.0F;
    m_clearColor[3] = 1.0F;

    // Input system
    m_mainKeyboard = nullptr;
    m_inputSystem = nullptr;
    m_mainMouse = nullptr;

    // Shader Controls
    m_shaderObjectVariables.BaseColor.Set(1.0f, 1.0f, 1.0f, 1.0f);
    m_shaderObjectVariables.ColorReplace = 0;
    m_shaderObjectVariables.Scale[0] = 1.0f;
    m_shaderObjectVariables.Scale[1] = 1.0f;
    m_shaderObjectVariables.Scale[2] = 1.0f;
    m_shaderObjectVariablesSync = false;

    m_shaderScreenVariablesSync = false;

    // Camera
    m_nearClip = 2.0f;
    m_farClip = 100.0f;

    m_cameraPosition = ysMath::LoadVector(0.0f, 0.0f, 10.0f);
    m_cameraTarget = ysMath::Constants::Zero3;
    m_cameraMode = CameraMode::Flat;
    m_cameraAngle = 0.0f;

    m_currentTarget = DrawTarget::Main;

    m_cameraFov = ysMath::Constants::PI / 3.0f;

    ResetLights();

    m_drawQueue = new ysExpandingArray<DrawCall, 256>[MaxLayers];
    m_drawQueueGui = new ysExpandingArray<DrawCall, 256>[MaxLayers];
}

dbasic::DeltaEngine::~DeltaEngine() {
    assert(m_windowSystem == nullptr);
    assert(m_gameWindow == nullptr);
    assert(m_audioSystem == nullptr);
    assert(m_audioDevice == nullptr);
    assert(m_mainVertexBuffer == nullptr);
    assert(m_mainIndexBuffer == nullptr);
    assert(m_vertexShader == nullptr);
    assert(m_vertexSkinnedShader == nullptr);
    assert(m_consoleVertexShader == nullptr);
    assert(m_pixelShader == nullptr);
    assert(m_consolePixelShader == nullptr);
    assert(m_shaderProgram == nullptr);
    assert(m_consoleProgram == nullptr);
    assert(m_skinnedShaderProgram == nullptr);
    assert(m_inputLayout == nullptr);
    assert(m_skinnedInputLayout == nullptr);
    assert(m_consoleInputLayout == nullptr);
    assert(m_mainKeyboard == nullptr);
    assert(m_inputSystem == nullptr);
    assert(m_mainMouse == nullptr);
    assert(m_shaderObjectVariablesBuffer == nullptr);
    assert(m_shaderScreenVariablesBuffer == nullptr);
    assert(m_shaderSkinningControlsBuffer == nullptr);
    assert(m_consoleShaderObjectVariablesBuffer == nullptr);
    assert(m_lightingControlBuffer == nullptr);
}

ysError dbasic::DeltaEngine::CreateGameWindow(const GameEngineSettings &settings) {
    YDS_ERROR_DECLARE("CreateGameWindow");

    // Create the window system
    YDS_NESTED_ERROR_CALL(ysWindowSystem::CreateWindowSystem(&m_windowSystem, ysWindowSystemObject::Platform::Windows));
    m_windowSystem->ConnectInstance(settings.Instance);

    // Find the monitor setup
    m_windowSystem->SurveyMonitors();
    ysMonitor *mainMonitor = m_windowSystem->GetMonitor(0);

    YDS_NESTED_ERROR_CALL(ysInputSystem::CreateInputSystem(&m_inputSystem, ysWindowSystemObject::Platform::Windows));
    m_windowSystem->AssignInputSystem(m_inputSystem);
    m_inputSystem->Initialize();

    m_mainKeyboard = m_inputSystem->GetKeyboardAggregator();
    m_mainMouse = m_inputSystem->GetMouseAggregator();

    // Create the game window
    YDS_NESTED_ERROR_CALL(m_windowSystem->NewWindow(&m_gameWindow));
    YDS_NESTED_ERROR_CALL(m_gameWindow->InitializeWindow(nullptr, settings.WindowTitle, ysWindow::WindowStyle::Windowed, 0, 0, 1920, 1080, mainMonitor));
    m_gameWindow->AttachEventHandler(&m_windowHandler);

    // Create the graphics device
    YDS_NESTED_ERROR_CALL(ysDevice::CreateDevice(&m_device, settings.API));
    YDS_NESTED_ERROR_CALL(m_device->InitializeDevice());

    // Create the audio device
    YDS_NESTED_ERROR_CALL(ysAudioSystem::CreateAudioSystem(&m_audioSystem, ysAudioSystem::API::DirectSound8));
    m_audioSystem->EnumerateDevices();
    m_audioDevice = m_audioSystem->GetPrimaryDevice();
    m_audioSystem->ConnectDevice(m_audioDevice, m_gameWindow);

    // Create the rendering context
    YDS_NESTED_ERROR_CALL(m_device->CreateRenderingContext(&m_renderingContext, m_gameWindow));

    m_windowHandler.Initialize(m_device, m_renderingContext, this);

    // Main render target
    YDS_NESTED_ERROR_CALL(m_device->CreateOnScreenRenderTarget(&m_mainRenderTarget, m_renderingContext, settings.DepthBuffer));

    m_mainRenderTarget->SetDebugName("MAIN_RENDER_TARGET");

    // Initialize Geometry
    YDS_NESTED_ERROR_CALL(InitializeGeometry());

    // Initialize UI renderer
    m_uiRenderer.SetEngine(this);
    YDS_NESTED_ERROR_CALL(m_uiRenderer.Initialize(32768));

    // Initialize the console
    m_console.SetEngine(this);
    m_console.SetRenderer(&m_uiRenderer);
    YDS_NESTED_ERROR_CALL(m_console.Initialize());

    // Initialize Shaders
    YDS_NESTED_ERROR_CALL(InitializeShaders(settings.ShaderDirectory));

    // Timing System
    m_timingSystem = ysTimingSystem::Get();
    m_timingSystem->Initialize();

    InitializeBreakdownTimer(settings.LoggingDirectory);

    m_initialized = true;

    m_windowHandler.OnResizeWindow(m_gameWindow->GetWidth(), m_gameWindow->GetHeight());

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::StartFrame() {
    YDS_ERROR_DECLARE("StartFrame");

    m_uiRenderer.Reset();

    m_breakdownTimer.WriteLastFrameToLogFile();
    m_breakdownTimer.StartFrame();
    m_breakdownTimer.StartMeasurement(FrameBreakdownFull);

    m_windowSystem->ProcessMessages();
    m_timingSystem->Update();

    // TEMP
    if (IsKeyDown(ysKey::Code::B)) m_device->SetDebugFlag(0, true);
    else m_device->SetDebugFlag(0, false);

    if (m_gameWindow->IsActive()) {
        m_windowSystem->SetCursorVisible(!m_cursorHidden);
        if (m_cursorPositionLocked) {
            m_windowSystem->ReleaseCursor();
            m_windowSystem->ConfineCursor(m_gameWindow);
        }
    }
    else {
        m_windowSystem->SetCursorVisible(true);
        m_windowSystem->ReleaseCursor();
    }

    if (IsOpen()) {
        m_device->SetRenderTarget(m_mainRenderTarget);
        m_device->ClearBuffers(m_clearColor);
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::EndFrame() {
    YDS_ERROR_DECLARE("EndFrame");

    if (IsOpen()) {
        m_breakdownTimer.StartMeasurement(FrameBreakdownRenderScene);
        {
            ExecuteDrawQueue(DrawTarget::Main);
        }
        m_breakdownTimer.EndMeasurement(FrameBreakdownRenderScene);

        m_breakdownTimer.StartMeasurement(FrameBreakdownRenderUI);
        {
            ExecuteDrawQueue(DrawTarget::Gui);
        }
        m_breakdownTimer.EndMeasurement(FrameBreakdownRenderUI);

        m_device->Present();

        ResetLights();
    }
    else {
        m_breakdownTimer.SkipMeasurement(FrameBreakdownRenderScene);
        m_breakdownTimer.SkipMeasurement(FrameBreakdownRenderUI);
    }

    m_breakdownTimer.EndMeasurement(FrameBreakdownFull);
    m_breakdownTimer.EndFrame();

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::Destroy() {
    YDS_ERROR_DECLARE("Destroy");

    YDS_NESTED_ERROR_CALL(GetConsole()->Destroy());
    YDS_NESTED_ERROR_CALL(GetUiRenderer()->Destroy());

    YDS_NESTED_ERROR_CALL(m_device->DestroyGPUBuffer(m_mainIndexBuffer));
    YDS_NESTED_ERROR_CALL(m_device->DestroyGPUBuffer(m_mainVertexBuffer));
    YDS_NESTED_ERROR_CALL(m_device->DestroyGPUBuffer(m_shaderObjectVariablesBuffer));
    YDS_NESTED_ERROR_CALL(m_device->DestroyGPUBuffer(m_shaderScreenVariablesBuffer));
    YDS_NESTED_ERROR_CALL(m_device->DestroyGPUBuffer(m_shaderSkinningControlsBuffer));
    YDS_NESTED_ERROR_CALL(m_device->DestroyGPUBuffer(m_consoleShaderObjectVariablesBuffer)); 
    YDS_NESTED_ERROR_CALL(m_device->DestroyGPUBuffer(m_lightingControlBuffer));

    YDS_NESTED_ERROR_CALL(m_device->DestroyShader(m_vertexShader));
    YDS_NESTED_ERROR_CALL(m_device->DestroyShader(m_vertexSkinnedShader));
    YDS_NESTED_ERROR_CALL(m_device->DestroyShader(m_consoleVertexShader));
    YDS_NESTED_ERROR_CALL(m_device->DestroyShader(m_pixelShader));
    YDS_NESTED_ERROR_CALL(m_device->DestroyShader(m_consolePixelShader));
    YDS_NESTED_ERROR_CALL(m_device->DestroyShaderProgram(m_shaderProgram));
    YDS_NESTED_ERROR_CALL(m_device->DestroyShaderProgram(m_consoleProgram));
    YDS_NESTED_ERROR_CALL(m_device->DestroyShaderProgram(m_skinnedShaderProgram));

    YDS_NESTED_ERROR_CALL(m_device->DestroyInputLayout(m_inputLayout));
    YDS_NESTED_ERROR_CALL(m_device->DestroyInputLayout(m_skinnedInputLayout));
    YDS_NESTED_ERROR_CALL(m_device->DestroyInputLayout(m_consoleInputLayout));

    YDS_NESTED_ERROR_CALL(m_device->DestroyRenderTarget(m_mainRenderTarget));
    YDS_NESTED_ERROR_CALL(m_device->DestroyRenderingContext(m_renderingContext));

    YDS_NESTED_ERROR_CALL(m_device->DestroyDevice());
   
    m_audioSystem->DisconnectDevice(m_audioDevice);
    ysAudioSystem::DestroyAudioSystem(&m_audioSystem);

    m_audioDevice = nullptr;

    m_mainKeyboard = nullptr;
    m_mainMouse = nullptr;

    YDS_NESTED_ERROR_CALL(ysInputSystem::DestroyInputSystem(m_inputSystem));

    m_windowSystem->DeleteAllWindows();
    m_gameWindow = nullptr;

    ysWindowSystem::DestroyWindowSystem(m_windowSystem);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::UseMaterial(Material *material) {
    YDS_ERROR_DECLARE("UseMaterial");

    if (material == nullptr) {
        SetBaseColor(ysMath::LoadVector(1.0f, 0.0f, 1.0f, 1.0f));
    }
    else {
        SetBaseColor(material->GetDiffuseColor());
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

void dbasic::DeltaEngine::ResetLights() {
    m_lightCount = 0;
    for (int i = 0; i < LightingControls::MaxLights; ++i) {
        m_lightingControls.Lights[i].Active = 0;
    }

    m_lightingControls.AmbientLighting = ysVector4(0.0f, 0.0f, 0.0f, 0.0f);
}

ysError dbasic::DeltaEngine::InitializeGeometry() {
    YDS_ERROR_DECLARE("InitializeGeometry");

    Vertex vertexData[] = {
        { { -1.0f, 1.0f, 0.0f, 1.0f },		{0.0f, 1.0f},	{0.0f, 0.0f, 1.0f, 0.0f} },
        { { 1.0f, 1.0f, 0.0f, 1.0f },		{1.0f, 1.0f},	{0.0f, 0.0f, 1.0f, 0.0f} },
        { { 1.0f, -1.0f, 0.0f, 1.0f },		{1.0f, 0.0f},	{0.0f, 0.0f, 1.0f, 0.0f} },
        { { -1.0f, -1.0f, 0.0f, 1.0f },     {0.0f, 0.0f},	{0.0f, 0.0f, 1.0f, 0.0f} } };

    unsigned short indices[] = {
        2, 1, 0,
        3, 2, 0 };

    YDS_NESTED_ERROR_CALL(m_device->CreateVertexBuffer(&m_mainVertexBuffer, sizeof(vertexData), (char *)vertexData));
    YDS_NESTED_ERROR_CALL(m_device->CreateIndexBuffer(&m_mainIndexBuffer, sizeof(indices), (char *)indices));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::InitializeShaders(const char *shaderDirectory) {
    YDS_ERROR_DECLARE("InitializeShaders");

    char buffer[256];

    if (m_device->GetAPI() == ysContextObject::DeviceAPI::DirectX11 || 
        m_device->GetAPI() == ysContextObject::DeviceAPI::DirectX10) 
    {
        sprintf_s(buffer, "%s%s", shaderDirectory, "delta_engine_shader.fx");
        YDS_NESTED_ERROR_CALL(m_device->CreateVertexShader(&m_vertexShader, buffer, "VS_STANDARD"));
        YDS_NESTED_ERROR_CALL(m_device->CreateVertexShader(&m_vertexSkinnedShader, buffer, "VS_SKINNED"));
        YDS_NESTED_ERROR_CALL(m_device->CreatePixelShader(&m_pixelShader, buffer, "PS"));

        sprintf_s(buffer, "%s%s", shaderDirectory, "delta_console_shader.fx");
        YDS_NESTED_ERROR_CALL(m_device->CreateVertexShader(&m_consoleVertexShader, buffer, "VS_CONSOLE"));
        YDS_NESTED_ERROR_CALL(m_device->CreatePixelShader(&m_consolePixelShader, buffer, "PS_CONSOLE"));
    }
    else if (m_device->GetAPI() == ysContextObject::DeviceAPI::OpenGL4_0) {
        sprintf_s(buffer, "%s%s", shaderDirectory, "delta_engine_shader.glvs");
        YDS_NESTED_ERROR_CALL(m_device->CreateVertexShader(&m_vertexShader, buffer, "VS"));
        YDS_NESTED_ERROR_CALL(m_device->CreateVertexShader(&m_vertexSkinnedShader, buffer, "VS"));

        sprintf_s(buffer, "%s%s", shaderDirectory, "delta_engine_shader.glfs");
        YDS_NESTED_ERROR_CALL(m_device->CreatePixelShader(&m_pixelShader, buffer, "PS"));

        sprintf_s(buffer, "%s%s", shaderDirectory, "delta_console_shader.glvs");
        YDS_NESTED_ERROR_CALL(m_device->CreateVertexShader(&m_consoleVertexShader, buffer, "VS"));

        sprintf_s(buffer, "%s%s", shaderDirectory, "delta_console_shader.glfs");
        YDS_NESTED_ERROR_CALL(m_device->CreatePixelShader(&m_consolePixelShader, buffer, "PS"));
    }

    m_skinnedFormat.AddChannel("POSITION", 0, ysRenderGeometryChannel::ChannelFormat::R32G32B32A32_FLOAT);
    m_skinnedFormat.AddChannel("TEXCOORD", sizeof(float) * 4, ysRenderGeometryChannel::ChannelFormat::R32G32_FLOAT);
    m_skinnedFormat.AddChannel("NORMAL", sizeof(float) * (4 + 2), ysRenderGeometryChannel::ChannelFormat::R32G32B32A32_FLOAT);
    m_skinnedFormat.AddChannel("BONE_INDICES", sizeof(float) * (4 + 2 + 4), ysRenderGeometryChannel::ChannelFormat::R32G32B32A32_UINT);
    m_skinnedFormat.AddChannel("BONE_WEIGHTS", sizeof(float) * (4 + 2 + 4) + sizeof(int) * 4, ysRenderGeometryChannel::ChannelFormat::R32G32B32A32_FLOAT);

    m_standardFormat.AddChannel("POSITION", 0, ysRenderGeometryChannel::ChannelFormat::R32G32B32A32_FLOAT);
    m_standardFormat.AddChannel("TEXCOORD", sizeof(float) * 4, ysRenderGeometryChannel::ChannelFormat::R32G32_FLOAT);
    m_standardFormat.AddChannel("NORMAL", sizeof(float) * (4 + 2), ysRenderGeometryChannel::ChannelFormat::R32G32B32A32_FLOAT);

    m_consoleVertexFormat.AddChannel("POSITION", 0, ysRenderGeometryChannel::ChannelFormat::R32G32_FLOAT);
    m_consoleVertexFormat.AddChannel("TEXCOORD", sizeof(float) * 2, ysRenderGeometryChannel::ChannelFormat::R32G32_FLOAT);

    YDS_NESTED_ERROR_CALL(m_device->CreateInputLayout(&m_inputLayout, m_vertexShader, &m_standardFormat));
    YDS_NESTED_ERROR_CALL(m_device->CreateInputLayout(&m_skinnedInputLayout, m_vertexSkinnedShader, &m_skinnedFormat));
    YDS_NESTED_ERROR_CALL(m_device->CreateInputLayout(&m_consoleInputLayout, m_consoleVertexShader, &m_consoleVertexFormat));

    YDS_NESTED_ERROR_CALL(m_device->CreateShaderProgram(&m_shaderProgram));
    YDS_NESTED_ERROR_CALL(m_device->AttachShader(m_shaderProgram, m_vertexShader));
    YDS_NESTED_ERROR_CALL(m_device->AttachShader(m_shaderProgram, m_pixelShader));
    YDS_NESTED_ERROR_CALL(m_device->LinkProgram(m_shaderProgram));

    YDS_NESTED_ERROR_CALL(m_device->CreateShaderProgram(&m_skinnedShaderProgram));
    YDS_NESTED_ERROR_CALL(m_device->AttachShader(m_skinnedShaderProgram, m_vertexSkinnedShader));
    YDS_NESTED_ERROR_CALL(m_device->AttachShader(m_skinnedShaderProgram, m_pixelShader));
    YDS_NESTED_ERROR_CALL(m_device->LinkProgram(m_skinnedShaderProgram));

    YDS_NESTED_ERROR_CALL(m_device->CreateShaderProgram(&m_consoleProgram));
    YDS_NESTED_ERROR_CALL(m_device->AttachShader(m_consoleProgram, m_consoleVertexShader));
    YDS_NESTED_ERROR_CALL(m_device->AttachShader(m_consoleProgram, m_consolePixelShader));
    YDS_NESTED_ERROR_CALL(m_device->LinkProgram(m_consoleProgram));

    // Create shader controls
    YDS_NESTED_ERROR_CALL(m_device->CreateConstantBuffer(&m_shaderObjectVariablesBuffer, sizeof(ShaderObjectVariables), nullptr));
    YDS_NESTED_ERROR_CALL(m_device->CreateConstantBuffer(&m_shaderScreenVariablesBuffer, sizeof(ShaderScreenVariables), nullptr));
    YDS_NESTED_ERROR_CALL(m_device->CreateConstantBuffer(&m_shaderSkinningControlsBuffer, sizeof(ShaderSkinningControls), nullptr));
    YDS_NESTED_ERROR_CALL(m_device->CreateConstantBuffer(&m_lightingControlBuffer, sizeof(LightingControls), nullptr));
    YDS_NESTED_ERROR_CALL(m_device->CreateConstantBuffer(&m_consoleShaderObjectVariablesBuffer, 
        sizeof(ConsoleShaderObjectVariables), nullptr));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::InitializeBreakdownTimer(const char *loggingDirectory) {
    YDS_ERROR_DECLARE("InitializeBreakdownTimer");

    ysBreakdownTimerChannel *full = m_breakdownTimer.CreateChannel(FrameBreakdownFull);
    ysBreakdownTimerChannel *scene = m_breakdownTimer.CreateChannel(FrameBreakdownRenderScene);
    ysBreakdownTimerChannel *ui = m_breakdownTimer.CreateChannel(FrameBreakdownRenderUI);

    std::string logFile = loggingDirectory;
    logFile += "/frame_breakdown_log.csv";

    m_breakdownTimer.OpenLogFile(logFile);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::LoadTexture(ysTexture **image, const char *fname) {
    YDS_ERROR_DECLARE("LoadTexture");

    YDS_NESTED_ERROR_CALL(m_device->CreateTexture(image, fname));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::LoadAnimation(Animation **animation, const char *path, int start, int end) {
    YDS_ERROR_DECLARE("LoadAnimation");

    ysTexture **list = new ysTexture * [end - start + 1];
    char buffer[256];
    for (int i = start; i <= end; ++i) {
        sprintf_s(buffer, 256, "%s/%.4i.png", path, i);

        YDS_NESTED_ERROR_CALL(m_device->CreateTexture(&list[i - start], buffer));
    }

    Animation *newAnimation;
    newAnimation = new Animation;
    newAnimation->m_nFrames = end - start + 1;
    newAnimation->m_textures = list;

    *animation = newAnimation;

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::LoadFont(Font **font, const char *path, int size) {
    YDS_ERROR_DECLARE("LoadFont");

    unsigned char *ttfBuffer = new unsigned char[1 << 20];
    unsigned char *bitmapData = new unsigned char[size * size];

    FILE *f = nullptr;
    fopen_s(&f, path, "rb");

    if (f == nullptr) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_OPEN_FILE);
    }

    fread(ttfBuffer, 1, 1 << 20, f);

    stbtt_packedchar *cdata = new stbtt_packedchar[96];

    int result = 0;
    stbtt_pack_context context;
    result = stbtt_PackBegin(&context, bitmapData, size, size, 0, 16, nullptr);
    result = stbtt_PackFontRange(&context, ttfBuffer, 0,  64.0f, 32, 96, cdata);
    stbtt_PackEnd(&context);
    delete[] ttfBuffer;

    Font *newFont = new Font;
    *font = newFont;

    ysTexture *texture = nullptr;
    YDS_NESTED_ERROR_CALL(m_device->CreateAlphaTexture(&texture, size, size, bitmapData));

    newFont->Initialize(32, 96, cdata, 64.0f, texture);

    delete[] cdata;

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::PlayAudio(AudioAsset *audio) {
    YDS_ERROR_DECLARE("PlayAudio");

    ysAudioSource *newSource = m_audioDevice->CreateSource(audio->GetBuffer());
    newSource->SetMode(ysAudioSource::Mode::PlayOnce);
    newSource->SetPan(0.0f);
    newSource->SetVolume(1.0f);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

void dbasic::DeltaEngine::SubmitSkeleton(Skeleton *skeleton) {
    int nBones = skeleton->GetBoneCount();

    for (int i = 0; i < nBones; i++) {
        Bone *bone = skeleton->GetBone(i);
        m_shaderSkinningControls.BoneTransforms[i] = ysMath::Transpose(skeleton->GetBone(i)->GetSkinMatrix());
    }
}

void dbasic::DeltaEngine::SetCameraPosition(float x, float y) {
    ysVector3 p = ysMath::GetVector3(m_cameraPosition);
    m_cameraPosition = ysMath::LoadVector(x, y, p.z, 1.0f);

    m_shaderScreenVariablesSync = false;
}

void dbasic::DeltaEngine::SetCameraPosition(const ysVector &pos) {
    m_cameraPosition = pos;
    m_shaderObjectVariablesSync = false;
}

ysVector dbasic::DeltaEngine::GetCameraPosition() const {
    return m_cameraPosition;
}

void dbasic::DeltaEngine::GetCameraPosition(float *x, float *y) const {
    *x = ysMath::GetX(m_cameraPosition);
    *y = ysMath::GetY(m_cameraPosition);
}

void dbasic::DeltaEngine::SetCameraUp(const ysVector &up) {
    m_cameraUp = up;
}

ysVector dbasic::DeltaEngine::GetCameraUp() const {
    return m_cameraUp;
}

void dbasic::DeltaEngine::SetCameraTarget(const ysVector &target) {
    m_cameraTarget = target;
    m_shaderScreenVariablesSync = false;
}

void dbasic::DeltaEngine::SetCameraMode(CameraMode mode) {
    m_cameraMode = mode;
    m_shaderScreenVariablesSync = false;
}

void dbasic::DeltaEngine::SetCameraAngle(float angle) {
    m_cameraAngle = angle;
    m_shaderScreenVariablesSync = false;
}

float dbasic::DeltaEngine::GetCameraFov() const {
    return m_cameraFov;
}

void dbasic::DeltaEngine::SetCameraFov(float fov) {
    m_cameraFov = fov;
}

float dbasic::DeltaEngine::GetCameraAspect() const {
    return m_gameWindow->GetScreenWidth() / (float)m_gameWindow->GetScreenHeight();
}

void dbasic::DeltaEngine::SetCameraAltitude(float altitude) {
    ysVector3 p = ysMath::GetVector3(m_cameraPosition);
    m_cameraPosition = ysMath::LoadVector(p.x, p.y, altitude, 1.0f);

    m_shaderScreenVariablesSync = false;
}

float dbasic::DeltaEngine::GetCameraAltitude() const {
    return ysMath::GetZ(m_cameraPosition);
}

void dbasic::DeltaEngine::SetObjectTransform(const ysMatrix &mat) {
    m_shaderObjectVariablesSync = false;
    m_shaderObjectVariables.Transform = mat;
}

void dbasic::DeltaEngine::SetPositionOffset(const ysVector &position) {
    m_shaderObjectVariablesSync = false;
    m_shaderObjectVariables.Transform = 
        ysMath::MatMult(m_shaderObjectVariables.Transform, ysMath::TranslationTransform(position));
}

void dbasic::DeltaEngine::SetWindowSize(int width, int height) {
    m_shaderScreenVariablesSync = false;
}

void dbasic::DeltaEngine::SetConsoleColor(const ysVector &v) {
    m_consoleShaderObjectVariables.MulCol = ysMath::GetVector4(v);
}

void dbasic::DeltaEngine::SetClearColor(const ysVector &v) {
    ysVector4 v4 = ysMath::GetVector4(v);

    m_clearColor[0] = v4.x;
    m_clearColor[1] = v4.y;
    m_clearColor[2] = v4.z;
    m_clearColor[3] = v4.w;
}

bool dbasic::DeltaEngine::IsKeyDown(ysKey::Code key) {
    if (m_mainKeyboard != nullptr) {
        return m_mainKeyboard->IsKeyDown(key);
    }

    return false;
}

bool dbasic::DeltaEngine::ProcessKeyDown(ysKey::Code key) {
    if (m_mainKeyboard != nullptr) {
        return m_mainKeyboard->ProcessKeyTransition(key);
    }

    return false;
}

bool dbasic::DeltaEngine::ProcessKeyUp(ysKey::Code key) {
    if (m_mainKeyboard != nullptr) {
        return m_mainKeyboard->ProcessKeyTransition(key, ysKey::State::UpTransition);
    }

    return false;
}

bool dbasic::DeltaEngine::ProcessMouseButtonDown(ysMouse::Button key) {
    if (m_mainMouse != nullptr) {
        return m_mainMouse->ProcessMouseButton(key, ysMouse::ButtonState::DownTransition);
    }

    return false;
}

bool dbasic::DeltaEngine::ProcessMouseButtonUp(ysMouse::Button key) {
    if (m_mainMouse != nullptr) {
        return m_mainMouse->ProcessMouseButton(key, ysMouse::ButtonState::UpTransition);
    }

    return false;
}

bool dbasic::DeltaEngine::IsMouseButtonDown(ysMouse::Button button) {
    if (m_mainMouse != nullptr) {
        return m_mainMouse->IsDown(button);
    }

    return false;
}

int dbasic::DeltaEngine::GetMouseWheel() {
    if (m_mainMouse != nullptr) {
        return m_mainMouse->GetWheel();
    }

    return 0;
}

void dbasic::DeltaEngine::GetMousePos(int *x, int *y) {
    if (m_mainMouse != nullptr) {
        if (x != nullptr) {
            *x = m_mainMouse->GetX();
        }

        if (y != nullptr) {
            *y = m_mainMouse->GetY();
        }
    }
}

void dbasic::DeltaEngine::GetOsMousePos(int *x, int *y) {
    if (m_mainMouse != nullptr) {
        int os_x = m_mainMouse->GetOsPositionX();
        int os_y = m_mainMouse->GetOsPositionY();

        m_gameWindow->ScreenToLocal(os_x, os_y);

        if (x != nullptr) *x = os_x;
        if (y != nullptr) *y = os_y;
    }
}

float dbasic::DeltaEngine::GetFrameLength() {
    return (float)(m_timingSystem->GetFrameDuration());
}

float dbasic::DeltaEngine::GetAverageFramerate() {
    return m_timingSystem->GetFPS();
}

void dbasic::DeltaEngine::ResetBrdfParameters() {
    m_shaderObjectVariables = ShaderObjectVariables();
}

void dbasic::DeltaEngine::SetBaseColor(const ysVector &color) {
    m_shaderObjectVariables.BaseColor = ysMath::GetVector4(color);
}

void dbasic::DeltaEngine::ResetBaseColor() {
    m_shaderObjectVariables.BaseColor = ysVector4(1.0f, 1.0f, 1.0f, 1.0f);
}

void dbasic::DeltaEngine::SetLit(bool lit) {
    m_shaderObjectVariables.Lit = (lit) ? 1 : 0;
}

void dbasic::DeltaEngine::SetEmission(const ysVector &emission) {
    m_shaderObjectVariables.Emission = ysMath::GetVector4(emission);
}

void dbasic::DeltaEngine::SetSpecularMix(float specularMix) {
    m_shaderObjectVariables.SpecularMix = specularMix;
}

void dbasic::DeltaEngine::SetDiffuseMix(float diffuseMix) {
    m_shaderObjectVariables.DiffuseMix = diffuseMix;
}

void dbasic::DeltaEngine::SetMetallic(float metallic) {
    m_shaderObjectVariables.Metallic = metallic;
}

void dbasic::DeltaEngine::SetDiffuseRoughness(float diffuseRoughness) {
    m_shaderObjectVariables.DiffuseRoughness = diffuseRoughness;
}

void dbasic::DeltaEngine::SetSpecularRoughness(float specularRoughness) {
    m_shaderObjectVariables.SpecularPower = ::pow(2.0f, 12.0f * (1.0f - specularRoughness));
}

void dbasic::DeltaEngine::SetSpecularPower(float power) {
    m_shaderObjectVariables.SpecularPower = power;
}

void dbasic::DeltaEngine::SetIncidentSpecular(float incidentSpecular) {
    m_shaderObjectVariables.IncidentSpecular = incidentSpecular;
}

int dbasic::DeltaEngine::GetScreenWidth() const {
    return m_gameWindow->GetScreenWidth();
}

int dbasic::DeltaEngine::GetScreenHeight() const {
    return m_gameWindow->GetScreenHeight();
}

ysError dbasic::DeltaEngine::DrawImage(ysTexture *image, int layer, float scaleX, float scaleY, float texOffsetU, float texOffsetV, float texScaleU, float texScaleV) {
    YDS_ERROR_DECLARE("DrawImage");

    if (!m_shaderObjectVariablesSync) {
        m_shaderObjectVariables.Scale[0] = scaleX;
        m_shaderObjectVariables.Scale[1] = scaleY;
        m_shaderObjectVariables.Scale[2] = 1.0f;

        m_shaderObjectVariables.TexOffset[0] = texOffsetU;
        m_shaderObjectVariables.TexOffset[1] = texOffsetV;

        m_shaderObjectVariables.TexScale[0] = texScaleU;
        m_shaderObjectVariables.TexScale[1] = texScaleV;

        m_shaderObjectVariables.ColorReplace = 0;
        m_shaderObjectVariables.Lit = 1;
    }

    DrawCall *newCall = nullptr;
    if (m_currentTarget == DrawTarget::Main) {
        newCall = &m_drawQueue[layer].New();
    }
    else if (m_currentTarget == DrawTarget::Gui) {
        newCall = &m_drawQueueGui[layer].New();
    }

    if (newCall != nullptr) {
        newCall->ObjectVariables = m_shaderObjectVariables;
        newCall->Texture = image;
        newCall->Model = nullptr;
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::AddLight(const Light &light) {
    YDS_ERROR_DECLARE("AddLight");

    if (m_lightCount >= LightingControls::MaxLights) return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
    m_lightingControls.Lights[m_lightCount] = light;
    m_lightingControls.Lights[m_lightCount].Active = 1;
    ++m_lightCount;


    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::SetAmbientLight(const ysVector4 &ambient) {
    YDS_ERROR_DECLARE("SetAmbientLight");

    m_lightingControls.AmbientLighting = ambient;

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::DrawBox(float width, float height, int layer) {
    YDS_ERROR_DECLARE("DrawBox");

    if (!m_shaderObjectVariablesSync) {
        m_shaderObjectVariables.Scale[0] = (float)width / 2.0f;
        m_shaderObjectVariables.Scale[1] = (float)height / 2.0f;
        m_shaderObjectVariables.Scale[2] = 1.0f;

        m_shaderObjectVariables.TexOffset[0] = 0.0f;
        m_shaderObjectVariables.TexOffset[1] = 0.0f;

        m_shaderObjectVariables.TexScale[0] = 1.0f;
        m_shaderObjectVariables.TexScale[1] = 1.0f;

        m_shaderObjectVariables.ColorReplace = 1;
    }

    DrawCall *newCall = nullptr;
    if (m_currentTarget == DrawTarget::Main) {
        newCall = &m_drawQueue[layer].New();
    }
    else if (m_currentTarget == DrawTarget::Gui) {
        newCall = &m_drawQueueGui[layer].New();
    }

    if (newCall != nullptr) {
        newCall->ObjectVariables = m_shaderObjectVariables;
        newCall->Texture = nullptr;
        newCall->Model = nullptr;
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::DrawAxis(const ysVector &position, const ysVector &direction, float width, float length, int layer) {
    YDS_ERROR_DECLARE("DrawAxis");

    ysMatrix trans = ysMath::TranslationTransform(position);
    ysMatrix offset = ysMath::TranslationTransform(ysMath::LoadVector(0, length / 2.0f));

    ysVector orth = ysMath::Cross(direction, ysMath::Constants::ZAxis);
    ysMatrix dir = ysMath::LoadMatrix(orth, direction, ysMath::Constants::ZAxis, ysMath::Constants::IdentityRow4);
    dir = ysMath::Transpose(dir);

    ysMatrix transform = ysMath::MatMult(trans, dir);
    transform = ysMath::MatMult(transform, offset);
    SetObjectTransform(transform);

    YDS_NESTED_ERROR_CALL(DrawBox(width, length, layer));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::DrawModel(ModelAsset *model, float scale, ysTexture *texture, int layer) {
    YDS_ERROR_DECLARE("DrawModel");

    if (!m_shaderObjectVariablesSync) {
        m_shaderObjectVariables.Scale[0] = scale;
        m_shaderObjectVariables.Scale[1] = scale;
        m_shaderObjectVariables.Scale[2] = scale;

        m_shaderObjectVariables.TexOffset[0] = 0.0f;
        m_shaderObjectVariables.TexOffset[1] = 0.0f;

        m_shaderObjectVariables.TexScale[0] = 1.0f;
        m_shaderObjectVariables.TexScale[1] = 1.0f;

        m_shaderObjectVariables.ColorReplace = 1;
    }

    DrawCall *newCall = nullptr;
    if (m_currentTarget == DrawTarget::Main) {
        newCall = &m_drawQueue[layer].New();
    }
    else if (m_currentTarget == DrawTarget::Gui) {
        newCall = &m_drawQueueGui[layer].New();
    }

    if (newCall != nullptr) {
        newCall->ObjectVariables = m_shaderObjectVariables;
        newCall->Texture = texture;
        newCall->Model = model;
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::DrawRenderSkeleton(RenderSkeleton *skeleton, float scale, int layer) {
    YDS_ERROR_DECLARE("DrawRenderSkeleton");

    int nodeCount = skeleton->GetNodeCount();
    for (int i = 0; i < nodeCount; ++i) {
        RenderNode *node = skeleton->GetNode(i);
        if (node->GetModelAsset() != nullptr) {
            Material *material = node->GetModelAsset()->GetMaterial();
            UseMaterial(material);

            ysTexture *diffuseMap = material == nullptr
                ? nullptr
                : material->GetDiffuseMap();

            SetObjectTransform(node->Transform.GetWorldTransform());
            DrawModel(
                node->GetModelAsset(),
                scale,
                diffuseMap,
                layer);
        }
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError dbasic::DeltaEngine::ExecuteDrawQueue(DrawTarget target) {
    YDS_ERROR_DECLARE("ExecuteDrawQueue");

    YDS_NESTED_ERROR_CALL(m_device->EditBufferData(m_shaderSkinningControlsBuffer, (char *)(&m_shaderSkinningControls)));
    YDS_NESTED_ERROR_CALL(m_device->EditBufferData(m_lightingControlBuffer, (char *)(&m_lightingControls)));

    if (!m_shaderScreenVariablesSync) {
        const float aspect = m_gameWindow->GetScreenWidth() / (float)m_gameWindow->GetScreenHeight();

        if (target == DrawTarget::Main) {
            m_shaderScreenVariables.Projection = ysMath::Transpose(ysMath::FrustrumPerspective(m_cameraFov, aspect, m_nearClip, m_farClip));
        }
        else if (target == DrawTarget::Gui) {
            m_shaderScreenVariables.Projection = ysMath::Transpose(
                ysMath::OrthographicProjection(
                    (float)m_gameWindow->GetScreenWidth(), 
                    (float)m_gameWindow->GetScreenHeight(), 
                    0.001f, 
                    500.0f));
        }

        float sinRot = sin(m_cameraAngle * ysMath::Constants::PI / 180.0f);
        float cosRot = cos(m_cameraAngle * ysMath::Constants::PI / 180.0f);

        ysVector cameraEye;
        ysVector cameraTarget;
        ysVector up;

        if (target == DrawTarget::Main) {
            cameraEye = m_cameraPosition;
        }
        else if (target == DrawTarget::Gui) {
            cameraEye = ysMath::LoadVector(0.0f, 0.0f, 10.0f, 1.0f);
        }

        if (target == DrawTarget::Main) {
            if (m_cameraMode == CameraMode::Flat) {
                cameraTarget = ysMath::Mask(cameraEye, ysMath::Constants::MaskOffZ);
                up = ysMath::LoadVector(-sinRot, cosRot);
            }
            else {
                cameraTarget = m_cameraTarget;
                ysVector cameraDir = ysMath::Sub(cameraTarget, cameraEye);
                ysVector right = ysMath::Cross(cameraDir, m_cameraUp);
                up = ysMath::Normalize(ysMath::Cross(right, cameraDir));
            }
        }
        else if (target == DrawTarget::Gui) {
            cameraTarget = ysMath::LoadVector(0.0f, 0.0f, 0.0f, 1.0f);
            up = ysMath::LoadVector(-sinRot, cosRot);
        }

        m_shaderScreenVariables.CameraView = ysMath::Transpose(ysMath::CameraTarget(cameraEye, cameraTarget, up));
        m_shaderScreenVariables.Eye = ysMath::GetVector4(cameraEye);

        ysMatrix proj = m_shaderScreenVariables.Projection;
        ysMatrix cam = m_shaderScreenVariables.CameraView;

        ysVector v = ysMath::LoadVector(19.3632f, 9.74805f, 0.0f, 1.0f);
        v = ysMath::MatMult(cam, v);
        v = ysMath::MatMult(proj, v);
        v = ysMath::Div(v, ysMath::LoadScalar(ysMath::GetW(v)));

        YDS_NESTED_ERROR_CALL(m_device->EditBufferData(m_shaderScreenVariablesBuffer, (char *)(&m_shaderScreenVariables)));

        // NOTE - Update the screen variables each frame so sync is not set to true
        // after the update happens.
        m_shaderScreenVariablesSync = false;
    }

    for (int i = 0; i < MaxLayers; i++) {
        int objectsAtLayer = 0;

        if (target == DrawTarget::Main) objectsAtLayer = m_drawQueue[i].GetNumObjects();
        else if (target == DrawTarget::Gui) objectsAtLayer = m_drawQueueGui[i].GetNumObjects();

        for (int j = 0; j < objectsAtLayer; j++) {
            DrawCall *call = nullptr;
            if (target == DrawTarget::Main) call = &m_drawQueue[i][j];
            else if (target == DrawTarget::Gui) call = &m_drawQueueGui[i][j];

            if (call == nullptr) continue;

            m_device->EditBufferData(m_shaderObjectVariablesBuffer, (char *)(&call->ObjectVariables));

            if (call->Model != nullptr) {
                m_device->SetDepthTestEnabled(m_mainRenderTarget, true);

                if (call->Model->GetBoneCount() <= 0) {
                    m_device->UseShaderProgram(m_shaderProgram);
                    m_device->UseInputLayout(m_inputLayout);
                }
                else {
                    m_device->UseShaderProgram(m_skinnedShaderProgram);
                    m_device->UseInputLayout(m_skinnedInputLayout);
                }

                m_device->UseConstantBuffer(m_shaderScreenVariablesBuffer, 0);
                m_device->UseConstantBuffer(m_shaderObjectVariablesBuffer, 1);
                m_device->UseConstantBuffer(m_shaderSkinningControlsBuffer, 2);
                m_device->UseConstantBuffer(m_lightingControlBuffer, 3);

                m_device->UseTexture(call->Texture, 0);

                m_device->UseIndexBuffer(call->Model->GetIndexBuffer(), 0);
                m_device->UseVertexBuffer(call->Model->GetVertexBuffer(), call->Model->GetVertexSize(), 0);

                m_device->Draw(call->Model->GetFaceCount(), call->Model->GetBaseIndex(), call->Model->GetBaseVertex());
            }
            else {
                m_device->SetDepthTestEnabled(m_mainRenderTarget, false);

                m_device->UseInputLayout(m_inputLayout);
                m_device->UseShaderProgram(m_shaderProgram);

                m_device->UseConstantBuffer(m_shaderScreenVariablesBuffer, 0);
                m_device->UseConstantBuffer(m_shaderObjectVariablesBuffer, 1);
                m_device->UseConstantBuffer(m_shaderSkinningControlsBuffer, 2);
                m_device->UseConstantBuffer(m_lightingControlBuffer, 3);

                m_device->UseIndexBuffer(m_mainIndexBuffer, 0);
                m_device->UseVertexBuffer(m_mainVertexBuffer, sizeof(Vertex), 0);

                m_device->UseTexture(call->Texture, 0);

                m_device->Draw(2, 0, 0);
            }
        }

        if (target == DrawTarget::Gui) {
            m_drawQueueGui[i].Clear();
        }
        else if (target == DrawTarget::Main) {
            m_drawQueue[i].Clear();
        }
    }

    if (target == DrawTarget::Gui) {
        ConsoleShaderObjectVariables objectSettings;
        objectSettings.TexOffset[0] = 0.0f;
        objectSettings.TexOffset[1] = 0.0f;
        objectSettings.TexScale[0] = 1.0f;
        objectSettings.TexScale[1] = 1.0f;

        m_device->EditBufferData(m_consoleShaderObjectVariablesBuffer, (char *)(&objectSettings));

        m_device->UseInputLayout(m_consoleInputLayout);
        m_device->UseShaderProgram(m_consoleProgram);

        m_device->UseConstantBuffer(m_shaderScreenVariablesBuffer, 0);
        m_device->UseConstantBuffer(m_consoleShaderObjectVariablesBuffer, 1);

        YDS_NESTED_ERROR_CALL(m_console.UpdateGeometry());
        YDS_NESTED_ERROR_CALL(m_uiRenderer.Update());
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}
