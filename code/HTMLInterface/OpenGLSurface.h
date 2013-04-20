#pragma once

#include "globalincs/pstypes.h"
#include <Awesomium/Surface.h>
#include "graphics/gropengl.h"

class OpenGLSurfaceFactory : public Awesomium::SurfaceFactory
{
public:
    Awesomium::Surface* CreateSurface(Awesomium::WebView* view, int width, int height);
   
    void DestroySurface(Awesomium::Surface* surface);
};

class OpenGLSurface : public Awesomium::Surface
{
public:
    void Paint(unsigned char* src_buffer, int src_row_span, 
		const Awesomium::Rect& src_rect, const Awesomium::Rect& dest_rect);
    
    void Scroll(int dx, int dy, 
        const Awesomium::Rect& clip_rect);

	int renderTarget;

	friend class OpenGLSurfaceFactory;
};

