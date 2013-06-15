#include "HTMLInterface.h"
#include "globalincs/alphacolors.h"
#include "cmdline/cmdline.h"

#include <Awesomium/ResourceInterceptor.h>
#include <Awesomium/STLHelpers.h>

using namespace Awesomium;

const std::string cfileDataSourceName = "cfile";
const std::string assetSchemeName = "asset";

class RestrictingResourceInterceptor : public ResourceInterceptor
{
public:
	ResourceResponse* RestrictingResourceInterceptor::OnRequest(ResourceRequest* request)
	{
		WebURL url = request->url();

		std::string host = ToString(url.host());
		std::string scheme = ToString(url.scheme());

		if (!host.compare(cfileDataSourceName) && !scheme.compare(assetSchemeName))
		{
			// If scheme and data source match, allow the request
			return NULL;
		}
		else
		{
			request->Cancel();

			return NULL;
		}
	}
};

HTMLInterface::HTMLInterface() : nextWidgetID(0)
{
	WebConfig config;

	webCore = WebCore::Initialize(config);

	openglFactory = boost::shared_ptr<OpenGLSurfaceFactory>(new OpenGLSurfaceFactory());
	webCore->set_surface_factory(openglFactory.get());

	if (!Cmdline_allow_external_requests)
	{
		interceptor = boost::shared_ptr<RestrictingResourceInterceptor>(new RestrictingResourceInterceptor());
		webCore->set_resource_interceptor(interceptor.get());
	}
}

HTMLInterface::~HTMLInterface()
{
	widgets.clear();

	WebCore::Shutdown();
}

void HTMLInterface::update()
{
	webCore->Update();

	for (SCP_vector<boost::shared_ptr<HTMLWidget>>::iterator iter = widgets.begin(); iter != widgets.end(); ++iter)
	{
		(*iter)->update();
	}
}

boost::weak_ptr<HTMLWidget> HTMLInterface::createDisplay(int width, int height)
{
	WebView* view = webCore->CreateWebView(width, height);

	boost::shared_ptr<HTMLWidget> widget = boost::shared_ptr<HTMLWidget>(new HTMLWidget(this, nextWidgetID++, view, width, height));

	widgets.push_back(widget);

	return boost::weak_ptr<HTMLWidget>(widget);
}

bool HTMLInterface::removeWidget(uint widgetID)
{
	for (SCP_vector<boost::shared_ptr<HTMLWidget>>::iterator iter = widgets.begin(); iter != widgets.end(); ++iter)
	{
		if ((*iter)->getID() == widgetID)
		{
			// Remove this entry if it isn't active anymore
			if (iter + 1 == widgets.end())
			{
				widgets.pop_back();
			}
			else
			{
				*iter = widgets.back();
				widgets.pop_back();
			}

			return true;
		}
	}

	return false;
}
