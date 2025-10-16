#include "Logging/LogBuffer.hpp"
#include <sstream>
#include <chrono>
#include <iomanip>

namespace TGE {
	
LogBuffer::LogBuffer(std::string_view logFilePath) : logFilePath(logFilePath)
{
	setp(iBuffer, iBuffer + (bufferSize - 1));
}

LogBuffer::~LogBuffer()
{
	sync();
}

int LogBuffer::overflow(int character)
{
	if((character != EOF) && (pptr() != epptr()))
	{
		return sputc(static_cast<char>(character));
	}
	else if(character != EOF)
	{
		sync();
		return overflow(character);
	}
	else
	{
		return sync();
	}
}

int LogBuffer::sync()
{
	if(pbase() != pptr())
	{
		std::size_t size = static_cast<int>(pptr() - pbase());

		// Write to stdout
		fwrite(pbase(), size, 1, stdout);

		// Write to log file
		// FILE* logFile = std::fopen(logFilePath.c_str(), "a");
		// fwrite(pbase(), size, 1, logFile);
		// std::fclose(logFile);

		setp(pbase(), epptr());
	}

	return 0;
}

} // namespace TGE