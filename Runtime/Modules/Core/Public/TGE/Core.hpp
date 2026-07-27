#pragma once

// Convenience header that pulls in the complete public surface area of the
// Core runtime module. Consumers can include this single header for dependency
// injection, execution, logging, and options primitives.

#include "TGE/Export.hpp"
#include "TGE/Features.hpp"

#include "TGE/Execution/Task.hpp"

#include "TGE/Logging/GlobalLogger.hpp"
#include "TGE/Logging/ILogDispatcher.hpp"
#include "TGE/Logging/ILogSink.hpp"
#include "TGE/Logging/LogFormatter.hpp"
#include "TGE/Logging/LogLevel.hpp"
#include "TGE/Logging/LogMessage.hpp"
#include "TGE/Logging/Logger.hpp"
#include "TGE/Logging/LoggingOptions.hpp"
#include "TGE/Logging/Sinks/ConsoleLogSink.hpp"
#include "TGE/Logging/Sinks/FileLogSink.hpp"

#include "TGE/Options/Options.hpp"

#include "TGE/Services/Service.hpp"
#include "TGE/Services/ServiceCollection.hpp"
#include "TGE/Services/ServiceDescriptor.hpp"
#include "TGE/Services/ServiceLifetime.hpp"
#include "TGE/Services/ServiceLocator.hpp"
#include "TGE/Services/ServiceProvider.hpp"
#include "TGE/Services/ServiceScopeState.hpp"
#include "TGE/Services/ServiceTraits.hpp"
