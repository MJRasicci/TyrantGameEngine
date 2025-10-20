#pragma once

// Convenience header that pulls in the complete public surface area of the
// Core runtime module.  Applications can include this single header to access
// logging facilities and the module export macros without worrying about the
// underlying folder layout.

#include "TGE/Core/Export.hpp"

#include "TGE/Logging/GlobalLogger.hpp"
#include "TGE/Logging/ILogSink.hpp"
#include "TGE/Logging/LogFormatter.hpp"
#include "TGE/Logging/LogLevel.hpp"
#include "TGE/Logging/LogMessage.hpp"
#include "TGE/Logging/Logger.hpp"
#include "TGE/Logging/LoggingOptions.hpp"
#include "TGE/Logging/Sinks/ConsoleLogSink.hpp"
#include "TGE/Logging/Sinks/FileLogSink.hpp"
