
#ifndef _CFILE_DATA_SOURCE_H
#define _CFILE_DATA_SOURCE_H

#include <Awesomium/DataSource.h>

class CFileDataSource : public Awesomium::DataSource
{

public:
    CFileDataSource();
    ~CFileDataSource();
    
    void OnRequest(int request_id, const Awesomium::WebString& path);
};

#endif _CFILE_DATA_SOURCE_H

