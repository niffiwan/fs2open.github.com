#include "HTMLInterface.h"
#include "globalincs/alphacolors.h"

#include <Awesomium/ResourceInterceptor.h>

using namespace Awesomium;

class RestrictingResourceInterceptor : public ResourceInterceptor
{
public:
	ResourceResponse* RestrictingResourceInterceptor::OnRequest(ResourceRequest* request)
	{
		WebURL url = request->url();

		return NULL;
	}
};

HTMLInterface::HTMLInterface()
{
	WebConfig config;

	webCore = WebCore::Initialize(config);

	openglFactory = new OpenGLSurfaceFactory();
	webCore->set_surface_factory(openglFactory);

	interceptor = new RestrictingResourceInterceptor();
	webCore->set_resource_interceptor(interceptor);
}

HTMLInterface::~HTMLInterface()
{
	while(!widgets.empty()) 
	{
		delete widgets.front();

		widgets.pop_front(); 
	}

	WebCore::Shutdown();

	if (openglFactory)
	{
		delete openglFactory;
	}

	if (interceptor)
	{
		delete interceptor;
	}
}

void HTMLInterface::update()
{
	webCore->Update();

	for (SCP_list<HTMLWidget*>::iterator iter = widgets.begin(); iter != widgets.end(); ++iter)
	{
		(*iter)->update();
	}
}

HTMLWidget* HTMLInterface::createDisplay(int width, int height)
{
	WebView* view = webCore->CreateWebView(width, height);

	HTMLWidget* widget = new HTMLWidget(this, view, width, height);

	widgets.push_back(widget);

	return widget;
}

void HTMLInterface::removeWidget(HTMLWidget* widget)
{
	widgets.remove(widget);
}

HTMLWidget::HTMLWidget(HTMLInterface* parent, WebView* view, int width, int height) 
	: parent(parent), width(width), height(height), webView(view), drawToScreen(true)
{
	Assertion(parent != NULL, "Parent is invalid!");
	Assertion(view != NULL, "WebView is invalid!");

	Assertion(width > 0, "Width is invalid, must be bigger than zero, is %d.", width);
	Assertion(height > 0, "Width is invalid, must be bigger than zero, is %d.", height);
}

HTMLWidget::~HTMLWidget()
{
	parent->removeWidget(this);

	if (webView)
	{
		webView->Destroy();
	}
}

void HTMLWidget::navigateTo(const SCP_string& string)
{
	webView->LoadURL(WebURL(WebString::CreateFromUTF8(string.c_str(), string.length())));
}

void HTMLWidget::moveTo(int x, int y)
{
	this->x = x;
	this->y = y;
}

void HTMLWidget::setDrawToScreen(bool draw)
{
	drawToScreen = draw;
}

void HTMLWidget::update()
{
	if (drawToScreen)
	{
		OpenGLSurface* surface = static_cast<OpenGLSurface*>(webView->surface());
	
		if (surface != NULL)
		{
			gr_set_bitmap(surface->renderTarget, GR_ALPHABLEND_NONE, GR_BITBLT_MODE_NORMAL, 1.0);
			bitmap_rect_list brl = bitmap_rect_list(x, y);
			gr_bitmap_list(&brl, 1, false);
		}
		else
		{
			gr_set_color_fast(&Color_black);
			gr_rect(this->x, this->y, this->width, this->height, false);
		}
	}
}

int HTMLWidget::getRenderTexture()
{
	OpenGLSurface* surface = static_cast<OpenGLSurface*>(webView->surface());

	if (surface != NULL)
	{
		return surface->renderTarget;
	}
	else
	{
		return -1;
	}
}
