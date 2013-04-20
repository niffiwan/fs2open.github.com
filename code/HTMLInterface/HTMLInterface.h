#pragma once

#include "globalincs/pstypes.h"
#include "HTMLInterface/OpenGLSurface.h"

#include <Awesomium/WebCore.h>

class HTMLInterface;

class HTMLWidget
{
private:
	HTMLInterface* parent;
	Awesomium::WebView* webView;

	int x;
	int y;
	int width;
	int height;

	bool drawToScreen;

public:
	HTMLWidget(HTMLInterface* parent, Awesomium::WebView* view, int width, int height);
	~HTMLWidget();

	void navigateTo(const SCP_string& string);

	void moveTo(int x, int y);
	void setDrawToScreen(bool draw);

	void update();

	int getRenderTexture();
};

class HTMLInterface
{
private:
	Awesomium::WebCore* webCore;
	OpenGLSurfaceFactory* openglFactory;

	SCP_list<HTMLWidget*> widgets;
public:
	HTMLInterface();
	~HTMLInterface();

	void update();

	HTMLWidget* createDisplay(int width, int height);
	void removeWidget(HTMLWidget* widget);
};

