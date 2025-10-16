#pragma once

#include "Export.hpp"
#include <iostream>
#include <streambuf>

namespace TGE {

class TGE_API LogBuffer : public std::streambuf
{
public:
	LogBuffer(std::string_view logFilePath);

	~LogBuffer();

private:
	virtual int overflow(int character);

	virtual int sync();

	static const int bufferSize = 256;
	char iBuffer[bufferSize];
	std::string logFilePath;
};

} // namespace TGE
