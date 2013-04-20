
#include "HTMLInterface/OpenGLSurface.h"
#include "graphics/gropengl.h"
#include "graphics/gropenglextension.h"
#include "graphics/gropengltexture.h"
#include "graphics/gropenglstate.h"
#include "graphics/gropengltnl.h"

static const int BYTES_PER_PIXEL = 4;

Awesomium::Surface* OpenGLSurfaceFactory::CreateSurface(Awesomium::WebView* view, int width, int height)
{
	int target = bm_make_render_target(width, height, BMP_FLAG_RENDER_TARGET_DYNAMIC);

	OpenGLSurface* surface = new OpenGLSurface();
	surface->renderTarget = target;

	return surface;
}
   
void OpenGLSurfaceFactory::DestroySurface(Awesomium::Surface* surface)
{
	OpenGLSurface* openglSurface = static_cast<OpenGLSurface*>(surface);
	
	bm_unload(openglSurface->renderTarget);

	delete openglSurface;
}

extern GLuint opengl_get_rtt_framebuffer();
void OpenGLSurface::Paint(unsigned char* src_buffer, int src_row_span, 
						   const Awesomium::Rect& src_rect, const Awesomium::Rect& dest_rect)
{
	GLuint framebuffer = opengl_get_rtt_framebuffer();

	if (bm_set_render_target(renderTarget))
	{
		glPushAttrib(GL_CURRENT_BIT);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		int offset = src_rect.x + src_rect.y * src_row_span;
		
		glRasterPos2i(dest_rect.x, dest_rect.y);
		glDrawPixels(dest_rect.width, dest_rect.height, GL_BGRA, GL_UNSIGNED_BYTE, src_buffer + offset);

		glPopAttrib();

		bm_set_render_target(-1);
		vglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer);
	}
}

void OpenGLSurface::Scroll(int dx, int dy, const Awesomium::Rect& clip_rect)
{

}
