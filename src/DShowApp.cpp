//------------------------------------------------------------------------------
// File: DShowApp.cpp
//
// Desc: DirectShow to libCinder proof-of-concept
//
// Aldo Hoeben / fieldOfView; aldo@fieldofview.com
//------------------------------------------------------------------------------

// If defined, GRAPHDEBUG sets the DirectShow filter up to be remotely connected to with Graphedit
//#define GRAPHDEBUG

#include "cinder/app/AppBasic.h"
#include "cinder/gl/Texture.h"

// Fix stdint / intsafe macro conflict
#undef INT8_MIN
#undef INT16_MIN
#undef INT32_MIN
#undef INT64_MIN
#undef INT8_MAX
#undef UINT8_MAX
#undef INT16_MAX
#undef UINT16_MAX
#undef INT32_MAX
#undef UINT32_MAX
#undef INT64_MAX
#undef UINT64_MAX

#include "stdafx.h"
#include "util.h"
#include "Strsafe.h"

#include "Allocator.h"


using namespace ci;
using namespace ci::app;
using namespace std;

class DShowApp : public AppBasic {
  public:
	void setup();
	void mouseDown( MouseEvent event );	
	void keyDown( KeyEvent event );
	void update();
	void draw();
	void shutdown();

	void frameDrawn( CAllocator* allocatorInstance );
	
	Surface					mFrameSurface;
	gl::Texture				mFrameTexture;
	bool					mFrameReady;

	HWND mHwnd;	

	BOOL		VerifyVMR9();
	HRESULT     StartGraph( fs::path path );
	HRESULT     CloseGraph();
	HRESULT     SetAllocatorPresenter( IBaseFilter *filter, HWND window );

#ifdef GRAPHDEBUG
	HRESULT		AddToRot( IUnknown *pUnkGraph, DWORD *pdwRegister );
	void		RemoveFromRot( DWORD pdwRegister );
	DWORD		dwRegister;
#endif

	// DirectShow interfaces
	SmartPtr<IGraphBuilder>          g_graph;
	SmartPtr<IBaseFilter>            g_filter;
	SmartPtr<IMediaControl>          g_mediaControl;
	SmartPtr<IMediaEvent>            g_mediaEvent;
	SmartPtr<IMediaSeeking>          g_mediaSeeking;
	SmartPtr<IVMRSurfaceAllocator9>  g_allocator;
};

void DShowApp::setup()
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if(!VerifyVMR9()) {
		console() << "The VMR 9 renderer is not supported on your system." << endl;
		return;
	}

	if(!gl::isExtensionAvailable("GL_ARB_pixel_buffer_object")) {
		console() << "Pixel Buffer Objects are not supported by your video card." << endl;
		quit();
		return;
	}

	mFrameTexture = gl::Texture();
	mFrameReady = false;

    g_allocator    = NULL;        
	g_mediaEvent   = NULL;
    g_mediaControl = NULL;
	g_mediaSeeking = NULL;
    g_filter       = NULL;        
    g_graph        = NULL;
}

void DShowApp::shutdown()
{
	CloseGraph();
}

void DShowApp::mouseDown( MouseEvent event )
{
}

void DShowApp::keyDown( KeyEvent event )
{
	switch( event.getChar() ) {
	case 'f':
		setFullScreen( ! isFullScreen() );
		break;

	case 'o':
		{
			CloseGraph();
			fs::path moviePath = getOpenFilePath();
			if( ! moviePath.empty() ) {
				StartGraph( moviePath );
			}
		}
		break;

	case ' ': 
		if( g_mediaControl != NULL ) {
			LONG msTimeout = 0;
			OAFilterState pfs;

			if( SUCCEEDED( g_mediaControl->GetState( msTimeout, &pfs ) ) ) {
				if( pfs == State_Running )
					g_mediaControl->Pause();
				else 
					g_mediaControl->Run();
			}
		}
		break;

	}
}

void DShowApp::update()
{
	if( mFrameReady && mFrameSurface ) {
		if( !mFrameTexture )
			mFrameTexture = gl::Texture( mFrameSurface );
		else
			mFrameTexture.update( mFrameSurface );
	}
	mFrameReady = false;
	
	if( g_mediaEvent != NULL ) {
		long lEventCode;
		LONG lParam1;
		LONG lParam2;
		
		while ( SUCCEEDED( g_mediaEvent->GetEvent(&lEventCode, &lParam1, &lParam2, 0) ) ) {
			console() << lEventCode << endl;
			switch( lEventCode ) {
			case EC_COMPLETE:
				// Completed video playback

				LONGLONG Time = 0;
				g_mediaSeeking->SetPositions(&Time, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);

				break;
			}
			
			g_mediaEvent->FreeEventParams(lEventCode, lParam1, lParam2);
		}
	}
}

void DShowApp::draw()
{
	// clear out the window with black
	gl::clear( Color( 0, 0, 0 ) ); 
	if( mFrameTexture )
		gl::draw( mFrameTexture, Rectf( mFrameTexture.getBounds() ).getCenteredFit( getWindowBounds(), true ) );
}

//----------------------------------------------------------------------------
//  frameDrawn
//
//  Receives a boost::signals2 notification from the allocator when a new 
//  frame is drawn into the surface
//----------------------------------------------------------------------------
void DShowApp::frameDrawn( CAllocator* allocatorInstance )
{
	if(!mFrameSurface)
		mFrameSurface = allocatorInstance->GetCiSurface();

	mFrameReady = true;
}


//----------------------------------------------------------------------------
//  VerifyVMR9
//
//  Verifies that VMR9 COM objects exist on the system and that the VMR9
//  can be instantiated.
//
//  Returns: FALSE if the VMR9 can't be created
//----------------------------------------------------------------------------

BOOL DShowApp::VerifyVMR9()
{
    HRESULT hr;

    // Verify that the VMR exists on this system
    IBaseFilter* pBF = NULL;
    hr = CoCreateInstance(CLSID_VideoMixingRenderer9, NULL,
                          CLSCTX_INPROC,
                          IID_IBaseFilter,
                          (LPVOID *)&pBF);
    if(SUCCEEDED(hr))
    {
        pBF->Release();
        return TRUE;
    }
    else
        return FALSE;
}

HRESULT DShowApp::CloseGraph()
{
	mFrameTexture.reset();
	mFrameSurface.reset();

    if( g_mediaControl != NULL ) 
    {
        OAFilterState state;
        do {
            g_mediaControl->Stop();
            g_mediaControl->GetState(0, & state );
        } while( state != State_Stopped ) ;
    }

#ifdef GRAPHDEBUG
	RemoveFromRot(dwRegister);
#endif

    g_allocator    = NULL;        
	g_mediaEvent   = NULL;
    g_mediaControl = NULL; 
	g_mediaSeeking = NULL;
    g_filter       = NULL;        
    g_graph        = NULL;
    ::InvalidateRect( mHwnd, NULL, true );
    return S_OK;
}

HRESULT DShowApp::StartGraph( fs::path path )
{
	mHwnd = ((RendererGl*)getRenderer())->getHwnd();

	// Clear DirectShow interfaces (COM smart pointers)
    CloseGraph();

    SmartPtr<IVMRFilterConfig9> filterConfig;

    HRESULT hr;
    
    hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&g_graph);

#ifdef GRAPH_DEBUG
	if (SUCCEEDED(hr))
	{
		hr = AddToRot(g_graph, &dwRegister);
	}
#endif

    if (SUCCEEDED(hr))
    {
        hr = CoCreateInstance(CLSID_VideoMixingRenderer9, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&g_filter);
    }

    if (SUCCEEDED(hr))
    {
        hr = g_filter->QueryInterface(IID_IVMRFilterConfig9, reinterpret_cast<void**>(&filterConfig));
    }

    if (SUCCEEDED(hr))
    {
        hr = filterConfig->SetRenderingMode( VMR9Mode_Renderless );
    }

    if (SUCCEEDED(hr))
    {
        hr = filterConfig->SetNumberOfStreams(2);
    }

    if (SUCCEEDED(hr))
    {
        hr = SetAllocatorPresenter( g_filter, mHwnd );
    }

    if (SUCCEEDED(hr))
    {
        hr = g_graph->AddFilter(g_filter, L"Video Mixing Renderer 9");
    }

    if (SUCCEEDED(hr))
    {
        hr = g_graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&g_mediaControl));
    }

    if (SUCCEEDED(hr))
    {
        hr = g_graph->QueryInterface(IID_IMediaEvent, reinterpret_cast<void**>(&g_mediaEvent));
    }

    if (SUCCEEDED(hr))
    {
        hr = g_graph->QueryInterface(IID_IMediaSeeking, reinterpret_cast<void**>(&g_mediaSeeking));
    }

	if (SUCCEEDED(hr))
    {
        hr = g_graph->RenderFile( path.c_str(), NULL );
    }

    if (SUCCEEDED(hr))
    {
        hr = g_mediaControl->Run();
    }

    return hr;
}

HRESULT DShowApp::SetAllocatorPresenter( IBaseFilter *filter, HWND window )
{
    if( filter == NULL )
    {
        return E_FAIL;
    }

    HRESULT hr;

    SmartPtr<IVMRSurfaceAllocatorNotify9> lpIVMRSurfAllocNotify;
    FAIL_RET( filter->QueryInterface(IID_IVMRSurfaceAllocatorNotify9, reinterpret_cast<void**>(&lpIVMRSurfAllocNotify)) );

    // create our surface allocator
	CAllocator * allocator = new CAllocator( hr, window );
    g_allocator.Attach( allocator );

    if( FAILED( hr ) )
    {
        g_allocator = NULL;
        return hr;
    }

    // let the allocator and the notify know about each other
	DWORD_PTR g_userId = 0xACDCACDC;
    FAIL_RET( lpIVMRSurfAllocNotify->AdviseSurfaceAllocator( g_userId, g_allocator ) );
    FAIL_RET( g_allocator->AdviseNotify(lpIVMRSurfAllocNotify) );

	// connect signal to callback
	allocator->GetSignal()->connect(boost::bind(&DShowApp::frameDrawn, this, boost::arg<1>() ));

    return hr;
}

#ifdef GRAPHDEBUG
HRESULT DShowApp::AddToRot(IUnknown *pUnkGraph, DWORD *pdwRegister) 
{
    IMoniker * pMoniker = NULL;
    IRunningObjectTable *pROT = NULL;

    if (FAILED(GetRunningObjectTable(0, &pROT))) 
    {
        return E_FAIL;
    }
    
    const size_t STRING_LENGTH = 256;

    WCHAR wsz[STRING_LENGTH];
 
   StringCchPrintfW(
        wsz, STRING_LENGTH, 
        L"FilterGraph %08x pid %08x", 
        (DWORD_PTR)pUnkGraph, 
        GetCurrentProcessId()
        );
    
    HRESULT hr = CreateItemMoniker(L"!", wsz, &pMoniker);
    if (SUCCEEDED(hr)) 
    {
        hr = pROT->Register(ROTFLAGS_REGISTRATIONKEEPSALIVE, pUnkGraph,
            pMoniker, pdwRegister);
        pMoniker->Release();
    }
    pROT->Release();
    
    return hr;
}

void DShowApp::RemoveFromRot(DWORD pdwRegister)
{
    IRunningObjectTable *pROT;
    if (SUCCEEDED(GetRunningObjectTable(0, &pROT))) {
        pROT->Revoke(pdwRegister);
        pROT->Release();
    }
}
#endif

CINDER_APP_BASIC( DShowApp, RendererGl )
