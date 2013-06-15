
#include "HTMLInterface/CFileDataSource.h"
#include "globalincs/pstypes.h"
#include "cfile/cfile.h"

#include <Awesomium/STLHelpers.h>

using namespace Awesomium;

CFileDataSource::CFileDataSource()
{
}


CFileDataSource::~CFileDataSource()
{
}

void CFileDataSource::OnRequest(int request_id, const Awesomium::WebString& path)
{
	std::string stringPath = ToString(path);

	CFILE* file = cfopen(const_cast<char*>(stringPath.c_str()), "r", CFILE_NORMAL, CF_TYPE_HTML);

	if (!file)
	{
		SendResponse(request_id, 0, NULL, WSLit(""));
		return;
	}

	int length = cfilelength(file);

	ubyte* data = new ubyte[length];
	cfread(data, length, 1, file);

	SendResponse(request_id, (uint) length, data, WSLit("text/html"));
}