
#ifndef _HTML_WIDGET_H
#define _HTML_WIDGET_H

#include "globalincs/alphacolors.h"
#include "globalincs/pstypes.h"
#include "HTMLInterface/JavaScriptManager.h"

#include <Awesomium/WebCore.h>

// FOrward definitions
class HTMLInterface;

class HTMLWidget : public JavaScriptManager
{
private:
	HTMLInterface* parent;

	int x;
	int y;
	int width;
	int height;

	bool drawToScreen;

	uint id;

	color widgetColor;

public:
	HTMLWidget(HTMLInterface* parent, uint id, Awesomium::WebView* view, int width, int height);
	~HTMLWidget();

	void navigateTo(const SCP_string& string);

	void moveTo(int x, int y);
	void resize(int width, int height);

	void setDrawToScreen(bool draw);

	void setWidgetColor(const color& color);

	void setFocus(bool focus);

	void update();

	int getRenderTexture();

	uint getID() { return id; }
};


#endif // _HTML_WIDGET_H
