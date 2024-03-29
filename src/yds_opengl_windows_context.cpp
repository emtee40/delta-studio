#include "../include/yds_opengl_windows_context.h"

#include "../include/yds_opengl_device.h"
#include "../include/yds_windows_window.h"

ysOpenGLWindowsContext::ysOpenGLWindowsContext() : ysOpenGLVirtualContext(ysWindowSystem::Platform::WINDOWS) {
    m_contextHandle = NULL;
}

ysOpenGLWindowsContext::~ysOpenGLWindowsContext() {
    /* void */
}

ysError ysOpenGLWindowsContext::CreateRenderingContext(ysOpenGLDevice *device, ysWindow *window, int major, int minor) {
    YDS_ERROR_DECLARE("CreateRenderingContext");

    if (window->GetPlatform() != ysWindowSystemObject::Platform::WINDOWS) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);
    GLenum result = glGetError();
    BOOL wresult = 0;

    ysWindowsWindow *windowsWindow = static_cast<ysWindowsWindow *>(window);

    HDC deviceHandle = GetDC(windowsWindow->GetWindowHandle());

    // Default
    m_device = NULL;
    m_deviceHandle = NULL;
    m_targetWindow = NULL;
    m_contextHandle = NULL;
    m_isRealContext = false;

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 32;
    pfd.iLayerType = PFD_MAIN_PLANE;

    unsigned int pixelFormat = ChoosePixelFormat(deviceHandle, &pfd);
    SetPixelFormat(deviceHandle, pixelFormat, &pfd);

    // Create a context if one doesn't already exist
    if (device->UpdateContext() == NULL) {
        HGLRC tempContext = wglCreateContext(deviceHandle);
        if (tempContext == NULL) return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_TEMPORARY_CONTEXT);

        wresult = wglMakeCurrent(deviceHandle, tempContext);
        if (wresult == FALSE) return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_ACTIVATE_TEMPORARY_CONTEXT);

        int attribs[] =
        {
            WGL_CONTEXT_MAJOR_VERSION_ARB, major,
            WGL_CONTEXT_MINOR_VERSION_ARB, minor,
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
            0
        };

        LoadContextCreationExtension();

        HGLRC hRC;

        hRC = wglCreateContextAttribsARB(deviceHandle, NULL, attribs);
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(tempContext);

        if (hRC == NULL) return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_CONTEXT);

        wresult = wglMakeCurrent(deviceHandle, hRC);
        if (wresult == FALSE) {
            wglDeleteContext(hRC);
            return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_ACTIVATE_CONTEXT);
        }

        m_contextHandle = hRC;
        m_isRealContext = true;

        device->UpdateContext();
    }

    m_device = device;
    m_deviceHandle = deviceHandle;
    m_targetWindow = window;

    LoadAllExtensions();

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysOpenGLWindowsContext::DestroyContext() {
    YDS_ERROR_DECLARE("DestroyContext");

    BOOL result;

    wglMakeCurrent(NULL, NULL);
    if (m_contextHandle) {
        result = wglDeleteContext(m_contextHandle);
        if (result == FALSE) return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_DESTROY_CONTEXT);
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysOpenGLWindowsContext::TransferContext(ysOpenGLVirtualContext *context) {
    YDS_ERROR_DECLARE("TransferContext");

    if (context->GetPlatform() != ysWindowSystemObject::Platform::WINDOWS) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    ysOpenGLWindowsContext *windowsContext = static_cast<ysOpenGLWindowsContext *>(context);
    windowsContext->m_contextHandle = m_contextHandle;
    windowsContext->m_isRealContext = true;

    m_isRealContext = false;
    m_contextHandle = NULL;

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysOpenGLWindowsContext::SetContextMode(ContextMode mode) {
    YDS_ERROR_DECLARE("SetContextMode");

    ysWindow *window = GetWindow();
    ysMonitor *monitor = window->GetMonitor();
    int result;

    if (mode == ysRenderingContext::ContextMode::FULLSCREEN) {
        window->SetWindowStyle(ysWindow::WindowStyle::FULLSCREEN);

        DEVMODE dmScreenSettings;
        memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
        dmScreenSettings.dmSize = sizeof(dmScreenSettings);
        dmScreenSettings.dmPelsWidth = monitor->GetWidth(); //window->GetWidth();
        dmScreenSettings.dmPelsHeight = monitor->GetHeight(); //window->GetHeight();
        dmScreenSettings.dmBitsPerPel = 32;
        dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

        result = ChangeDisplaySettingsEx(monitor->GetDeviceName(), &dmScreenSettings, NULL, CDS_FULLSCREEN | CDS_UPDATEREGISTRY, NULL);
        if (result != DISP_CHANGE_SUCCESSFUL) {
            return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_ENTER_FULLSCREEN);
        }

        //ChangeDisplaySettingsEx (NULL, NULL, NULL, 0, NULL);

        // TEMP
        ShowCursor(TRUE);
    }
    else if (mode == ysRenderingContext::ContextMode::WINDOWED) {
        window->SetWindowStyle(ysWindow::WindowStyle::WINDOWED);

        result = ChangeDisplaySettingsEx(NULL, NULL, NULL, 0, NULL);
        if (result != DISP_CHANGE_SUCCESSFUL) {
            return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_EXIT_FULLSCREEN);
        }

        ShowCursor(TRUE);
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysOpenGLWindowsContext::SetContext(ysRenderingContext *realContext) {
    YDS_ERROR_DECLARE("SetContext");

    ysOpenGLWindowsContext *realOpenglContext = static_cast<ysOpenGLWindowsContext *>(realContext);
    BOOL result;

    if (realContext) {
        result = wglMakeCurrent(m_deviceHandle, realOpenglContext->m_contextHandle);
        if (result == FALSE) return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_ACTIVATE_CONTEXT);
    }
    else {
        wglMakeCurrent(NULL, NULL);
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysOpenGLWindowsContext::Present() {
    YDS_ERROR_DECLARE("Present");

    BOOL result;
    result = SwapBuffers(m_deviceHandle);

    if (result == FALSE) return YDS_ERROR_RETURN(ysError::YDS_BUFFER_SWAP_ERROR);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

void ysOpenGLWindowsContext::LoadContextCreationExtension() {
    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
}

void ysOpenGLWindowsContext::LoadAllExtensions() {
    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)wglGetProcAddress("glDeleteBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    glBindBufferRange = (PFNGLBINDBUFFERRANGEPROC)wglGetProcAddress("glBindBufferRange");
    glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)wglGetProcAddress("glDeleteVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
    glVertexAttribIPointer = (PFNGLVERTEXATTRIBIPOINTERPROC)wglGetProcAddress("glVertexAttribIPointer");
    glVertexAttrib3f = (PFNGLVERTEXATTRIB3FPROC)wglGetProcAddress("glVertexAttrib3f");
    glVertexAttrib4f = (PFNGLVERTEXATTRIB4FPROC)wglGetProcAddress("glVertexAttrib4f");

    // Shaders
    glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");

    glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    glDetachShader = (PFNGLDETACHSHADERPROC)wglGetProcAddress("glDetachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    glBindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)wglGetProcAddress("glBindAttribLocation");
    glBindFragDataLocation = (PFNGLBINDFRAGDATALOCATIONPROC)wglGetProcAddress("glBindFragDataLocation");
    glGetFragDataLocation = (PFNGLGETFRAGDATALOCATIONPROC)wglGetProcAddress("glGetFragDataLocation");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
    glDrawBuffers = (PFNGLDRAWBUFFERSPROC)wglGetProcAddress("glDrawBuffers");

    glUniform4fv = (PFNGLUNIFORM4FVPROC)wglGetProcAddress("glUniform4fv");
    glUniform3fv = (PFNGLUNIFORM3FVPROC)wglGetProcAddress("glUniform3fv");
    glUniform2fv = (PFNGLUNIFORM2FVPROC)wglGetProcAddress("glUniform2fv");
    glUniform4f = (PFNGLUNIFORM4FPROC)wglGetProcAddress("glUniform4f");
    glUniform3f = (PFNGLUNIFORM3FPROC)wglGetProcAddress("glUniform3f");
    glUniform2f = (PFNGLUNIFORM2FPROC)wglGetProcAddress("glUniform2f");
    glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
    glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");

    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
    glUniformMatrix3fv = (PFNGLUNIFORMMATRIX3FVPROC)wglGetProcAddress("glUniformMatrix3fv");

    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");

    glGetActiveUniformName = (PFNGLGETACTIVEUNIFORMNAMEPROC)wglGetProcAddress("glGetActiveUniformName");
    glGetActiveUniformsiv = (PFNGLGETACTIVEUNIFORMSIVPROC)wglGetProcAddress("glGetActiveUniformsiv");
    glGetActiveUniform = (PFNGLGETACTIVEUNIFORMPROC)wglGetProcAddress("glGetActiveUniform");

    glMapBuffer = (PFNGLMAPBUFFERPROC)wglGetProcAddress("glMapBuffer");
    glMapBufferRange = (PFNGLMAPBUFFERRANGEPROC)wglGetProcAddress("glMapBufferRange");
    glUnmapBuffer = (PFNGLUNMAPBUFFERPROC)wglGetProcAddress("glUnmapBuffer");

    glDrawElementsBaseVertex = (PFNGLDRAWELEMENTSBASEVERTEXPROC)wglGetProcAddress("glDrawElementsBaseVertex");

    // Textures
    glActiveTexture = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
    glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");

    // Buffers
    glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)wglGetProcAddress("glGenRenderbuffers");
    glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)wglGetProcAddress("glDeleteRenderbuffers");
    glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)wglGetProcAddress("glBindRenderbuffer");
    glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)wglGetProcAddress("glRenderbufferStorage");
    glCopyBufferSubData = (PFNGLCOPYBUFFERSUBDATAPROC)wglGetProcAddress("glCopyBufferSubData");
    glBufferSubData = (PFNGLBUFFERSUBDATAPROC)wglGetProcAddress("glBufferSubData");

    glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
    glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
    glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
    glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");

    glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)wglGetProcAddress("glBlitFramebuffer");

    // Blending
    glBlendEquation = (PFNGLBLENDEQUATIONPROC)wglGetProcAddress("glBlendEquation");

    wglMakeContextCurrent = (PFNWGLMAKECONTEXTCURRENTARBPROC)wglGetProcAddress("wglMakeContextCurrentARB");
}
