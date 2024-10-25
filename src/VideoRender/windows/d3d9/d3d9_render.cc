#include "VideoRender/windows/d3d9/d3d9_render.h"
#include "d3d9_render.h"
#include "Base/ErrorCode.h"
#include <libyuv/convert.h>
using namespace hrtc;

hrtc::D3D9Renderer::D3D9Renderer(size_t width, size_t height):
    width_(width),
    height_(height),
    hwnd_(NULL),
    d3d_device_(NULL),
    mirror_(false),
    surfaceWidth_(0),
    surfaceHeight_(0),
    backbufferWidth_(0),
    backbufferHeight_(0),
    initialized_(false)
{

}

hrtc::D3D9Renderer::~D3D9Renderer()
{
    d3d_device_ = nullptr;
}

bool hrtc::D3D9Renderer::Init(void * window)
{
    if(!window || IsWindow((HWND)window)){
        return false;
    }
    hwnd_ = (HWND)window;
    HRESULT hr = S_OK;
    GetCreationTaskQueue()->sync([this,&hr]{
        if(!d3dGloba_){
            IDirect3D9Ex* d3d = nullptr;
            hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d);
            d3dGloba_ = d3d;
            if (d3d) {
                d3d->Release();
            }
            if(FAILED(hr)){
                return false;
            }
            if(!d3dGloba_){
                return false;
            }
        }
        D3DPRESENT_PARAMETERS d3dParams;
        d3dParams.Windowed = true;
        d3dParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dParams.BackBufferWidth = backbufferWidth_;
        d3dParams.BackBufferHeight = backbufferHeight_;
        d3dParams.BackBufferFormat = D3DFMT_UNKNOWN;
        d3dParams.hDeviceWindow = hwnd_;

        IDirect3DDevice9Ex* d3dDevice;
        int retryCount = 0;
        do {
            hr = d3dGloba_ ->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd_,
                D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
                &d3dParams, NULL, &d3dDevice);
            if(SUCCEEDED(hr)){
                d3d_device_ = d3dDevice;
                d3dDevice->Release();
                return true;
            }

            if(retryCount == 2){
                break;
            }
            retryCount++;
        }while(true);
        return false;
    });

    return true;
}


void hrtc::D3D9Renderer::GetActualSize(size_t & width, size_t & height)
{
    RECT rect;
    GetClientRect(hwnd_, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

int hrtc::D3D9Renderer::RenderFrame(const IMediaInfo & frame)
{
    if(!frame.IsVideo())
        return HRTC_CODE_ERROR_INVALID_ARG;
    auto frameWidth = frame.Format().Video.width;
    auto frameHeight = frame.Format().Video.height;

    if(backbufferWidth_ != frameWidth || backbufferHeight_ != frameHeight){
        backbufferWidth_ = frameWidth;
        backbufferHeight_ = frameWidth;
        GetCreationTaskQueue()->sync([this]{
            D3DPRESENT_PARAMETERS d3dParams;
            d3dParams.Windowed = true;
            d3dParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
            d3dParams.BackBufferWidth = backbufferWidth_;
            d3dParams.BackBufferHeight = backbufferHeight_;
            d3dParams.BackBufferFormat = D3DFMT_UNKNOWN;
            d3dParams.hDeviceWindow = hwnd_;

            auto hr = d3d_device_->Reset(&d3dParams);
            if(FAILED(hr)){
                //todo log
            }
        });

        size_t surfaceW = surfaceWidth_;
        size_t surfaceH = surfaceHeight_;
        if(surfaceW != frameWidth || surfaceH != frameHeight){
            IDirect3DSurface9* surface = nullptr;
            auto hr = d3d_device_->CreateOffscreenPlainSurface(frameWidth,
                frameHeight,
                (D3DFORMAT)MAKEFOURCC('I', '4', '2', '0'), //todo support rgb
                D3DPOOL_DEFAULT,
                &surface,
                NULL);
            if(FAILED(hr)){
                //todo log
                return HRTC_CODE_ERROR_FAILURE;
            }
            surface_ = surface;
            if(surface)
                surface->Release();
            surfaceHeight_ = frameHeight;
            surfaceWidth_ = frameWidth;
        }
    }

    auto tmpSurface = surface_;
    if(tmpSurface){
        if(!RenderYUVSurface(frame,tmpSurface.get(),frameWidth,frameHeight)){
            return HRTC_CODE_ERROR_FAILURE;//todo log.
        }
    }
    IDirect3DSurface9* backBuffer;
    d3d_device_->BeginScene();
    if(FAILED(d3d_device_->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer))){
        return HRTC_CODE_ERROR_FAILURE;
    }
    RECT viewRect; 
    RECT outRect;
    HRESULT hr = S_OK;
    if(GetClientRect(hwnd_,&viewRect)){
        float w1 = viewRect.right - viewRect.left;
        float h1 = viewRect.bottom - viewRect.right;
        float w2 = frameWidth;
        float h2 = frameHeight;
        if(w1*h2/h1/w2 > 1){
            outRect.bottom = h1;
            outRect.top = 0;
            outRect.left = (w1 - w2*h1/h2 + 1)/2;
            outRect.right = w1 - outRect.left;
        }
        else{
            outRect.left = 0;
            outRect.right = w1;
            outRect.top = (h1 - h2*w1/w2 +1)/2;
            outRect.right = h1-outRect.top;
        }
        hr = d3d_device_->StretchRect(tmpSurface.get(),&outRect,backBuffer,NULL,D3DTEXF_LINEAR);
    }
    else{
        hr = d3d_device_->StretchRect(tmpSurface.get(),0,backBuffer,NULL,D3DTEXF_LINEAR);
    }
        
    if(FAILED(hr)){
        //todo log.
    }
    d3d_device_->EndScene();

    d3d_device_->Present(NULL,NULL,NULL,NULL);
    backBuffer->Release();
    return true;
}

bool hrtc::D3D9Renderer::RenderYUVSurface(const IMediaInfo & frame, IDirect3DSurface9 * surface, int renderWidth, int renderHeight)
{
    D3DLOCKED_RECT lockRect;
    surface->AddRef();
    auto hr = surface->LockRect(&lockRect,NULL,D3DLOCK_DONOTWAIT);
    if(FAILED(hr)){
        return false;
    }
    uint8_t* pDest = reinterpret_cast<uint8_t*>(lockRect.pBits);

    const int deststride = lockRect.Pitch;
    uint8_t* dstU =
        reinterpret_cast<uint8_t*>(lockRect.pBits) + deststride * surfaceHeight_;
    uint8_t* dstV = dstU + deststride * surfaceHeight_ / 2 / 2;

    libyuv::ConvertToI420(frame.GetData(0),frame.GetSize()
                            ,pDest,renderWidth
                            ,dstU,renderWidth/2
                            ,dstV,renderWidth/2
                            ,0,0
                            ,renderWidth,renderHeight
                            ,renderWidth,renderWidth,
                            libyuv::kRotate0,frame.Format().Video.format);
    surface->UnlockRect();
    surface->Release();
    return true;
}

bool hrtc::D3D9Renderer::RenderRGBSurface(const IMediaInfo & frame, IDirect3DSurface9 * surface, int renderRidth, int renderHeight)
{
    return false;
}



std::shared_ptr<dispatch_task_queue::task_queue> hrtc::D3D9Renderer::GetCreationTaskQueue()
{
    if(!d3dCreationQueue_){
        d3dCreationQueue_ = dispatch_task_queue::create(1);
    }
    return d3dCreationQueue_;
}

std::shared_ptr<dispatch_task_queue::task_queue> D3D9Renderer::d3dCreationQueue_;
rtc_refptr<IDirect3D9Ex> D3D9Renderer::d3dGloba_;