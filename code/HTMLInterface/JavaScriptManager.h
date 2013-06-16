
#ifndef _JAVA_SCRIPT_MANAGER_H
#define _JAVA_SCRIPT_MANAGER_H

#include "globalincs/pstypes.h"
#include <Awesomium/WebView.h>
#include <Awesomium/JSObject.h>

class JavaScriptManager;
class ObjectHandle
{
private:
	Awesomium::JSObject object;
	JavaScriptManager* manager;

public:
	ObjectHandle(JavaScriptManager* manager, const Awesomium::JSObject& object);

	Awesomium::JSObject getObject() { return object; }

	void setProperty(const SCP_string& name, Awesomium::JSValue value);
	
	template<class Func>
	void addCustomFunction(const SCP_string& name, Func callback);
};

class JavaScriptManager : public Awesomium::JSMethodHandler
{
public:
	typedef void (*MethodCall)(Awesomium::WebView* caller, uint remote_id,
		const Awesomium::WebString& method_name, const Awesomium::JSArray& args);

	typedef Awesomium::JSValue (*MethodCallWithReturn)(Awesomium::WebView* caller, uint remote_id,
		const Awesomium::WebString& method_name, const Awesomium::JSArray& args);

	JavaScriptManager(Awesomium::WebView* webView);
	virtual ~JavaScriptManager();
	
	void OnMethodCall(Awesomium::WebView* caller,
								unsigned int remote_object_id,
								const Awesomium::WebString& method_name,
								const Awesomium::JSArray& args);

	Awesomium::JSValue OnMethodCallWithReturnValue(Awesomium::WebView* caller,
												  unsigned int remote_object_id,
												  const Awesomium::WebString& method_name,
												  const Awesomium::JSArray& args);
	
	template<class Func>
	void addCustomFunction(uint remote_id, Func callback);

	ObjectHandle createGlobalObject(const SCP_string& name);

	Awesomium::WebView* getWebView() { return webView; }
protected:
	Awesomium::WebView* webView;

	SCP_hash_map<uint, MethodCall> methodCalls;

	SCP_hash_map<uint, MethodCallWithReturn> methodCallsWithReturn;

};

#endif // _JAVA_SCRIPT_MANAGER_H
