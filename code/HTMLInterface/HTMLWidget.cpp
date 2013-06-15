
#include "HTMLInterface/HTMLWidget.h"
#include "HTMLInterface/HTMLInterface.h"
#include "globalincs/alphacolors.h"

#include <Awesomium/WebURL.h>

using namespace Awesomium;

HTMLWidget::HTMLWidget(HTMLInterface* parent, uint id, WebView* view, int width, int height) 
	: parent(parent), width(width), height(height), webView(view), drawToScreen(true), id(id),
	widgetColor(Color_black)
{
	Assertion(parent != NULL, "Parent is invalid!");
	Assertion(view != NULL, "WebView is invalid!");

	Assertion(width > 0, "Width is invalid, must be bigger than zero, is %d.", width);
	Assertion(height > 0, "Width is invalid, must be bigger than zero, is %d.", height);
}

HTMLWidget::~HTMLWidget()
{
	parent->removeWidget(this->id);

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

void HTMLWidget::setWidgetColor(const color& color)
{
	widgetColor = color;
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
			gr_set_color_fast(&widgetColor);
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