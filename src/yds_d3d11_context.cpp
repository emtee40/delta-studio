#include "../include/yds_d3d11_context.h"
#include "../include/yds_d3d11_render_target.h"
#include "../include/yds_d3d11_device.h"

ysD3D11Context::ysD3D11Context() : ysRenderingContext(ysDevice::DIRECTX11, ysWindowSystemObject::Platform::WINDOWS) {
    /* void */
}

ysD3D11Context::~ysD3D11Context() {
    /* void */
}
