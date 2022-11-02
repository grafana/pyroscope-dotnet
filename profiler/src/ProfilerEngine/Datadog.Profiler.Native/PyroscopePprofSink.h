//
// Created by root on 11/3/22.
//

#pragma once

#include <PprofExporter.h>

class PyroscopePprofSink: public PProfExportSink
{
public:
    virtual void Export(std::string pprof);
    virtual ~PyroscopePprofSink() = default;

};

