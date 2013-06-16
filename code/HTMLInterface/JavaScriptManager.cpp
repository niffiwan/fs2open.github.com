#include "JavaScriptManager.h"

#include <Awesomium/STLHelpers.h>

using namespace Awesomium;

JavaScriptManager::JavaScriptManager(WebView* webView) : webView(webView)
{
	webView->set_js_method_handler(this);
	webView->set_sync_message_timeout(5000);
}

JavaScriptManager::~JavaScriptManager()
{
	if (webView)
	{
		webView->Destroy();
	}
}

template<>
void JavaScriptManager::addCustomFunction<JavaScriptManager::MethodCall>(uint remote_id, MethodCall callback)
{
	std::pair<uint, MethodCall> pair(remote_id, callback);

	methodCalls.insert(pair);
}

template<>
void JavaScriptManager::addCustomFunction<JavaScriptManager::MethodCallWithReturn>(uint remote_id, MethodCallWithReturn callback)
{
	std::pair<uint, MethodCallWithReturn> pair(remote_id, callback);

	methodCallsWithReturn.insert(pair);
}

ObjectHandle JavaScriptManager::createGlobalObject(const SCP_string& name)
{
	WebString string = WebString::CreateFromUTF8(name.data(), name.length());

	JSValue value = webView->CreateGlobalJavascriptObject(string);

	Awesomium::Error err = webView->last_error();

	Assert(err == kError_None);

	JSObject object = value.ToObject();

	return ObjectHandle(this, object);
}

void JavaScriptManager::OnMethodCall(WebView* caller,
							unsigned int remote_object_id,
							const WebString& method_name,
							const JSArray& args)
{
	SCP_hash_map<uint, MethodCall>::iterator iter = methodCalls.find(remote_object_id);

	if (iter == methodCalls.end())
	{
		return;
	}

	iter->second(caller, remote_object_id, method_name, args);
}

Awesomium::JSValue JavaScriptManager::OnMethodCallWithReturnValue(WebView* caller,
												unsigned int remote_object_id,
												const WebString& method_name,
												const JSArray& args)
{
	SCP_hash_map<uint, MethodCallWithReturn>::iterator iter = methodCallsWithReturn.find(remote_object_id);

	if (iter == methodCallsWithReturn.end())
	{
		return JSValue::Undefined();
	}

	return iter->second(caller, remote_object_id, method_name, args);
}

ObjectHandle::ObjectHandle(JavaScriptManager* manager, const JSObject& object) : manager(manager), object(object)
{
}

void ObjectHandle::setProperty(const SCP_string& name, Awesomium::JSValue value)
{
	WebString string = WebString::CreateFromUTF8(name.data(), name.length());

	object.SetPropertyAsync(string, value);
}

template<>
void ObjectHandle::addCustomFunction<JavaScriptManager::MethodCall>(const SCP_string& name, JavaScriptManager::MethodCall callback)
{
	WebString string = WebString::CreateFromUTF8(name.data(), name.length());

	object.SetCustomMethod(string, false);

	manager->addCustomFunction(object.remote_id(), callback);
}

template<>
void ObjectHandle::addCustomFunction<JavaScriptManager::MethodCallWithReturn>(const SCP_string& name, JavaScriptManager::MethodCallWithReturn callback)
{
	WebString string = WebString::CreateFromUTF8(name.data(), name.length());

	object.SetCustomMethod(string, true);

	manager->addCustomFunction(object.remote_id(), callback);
}
