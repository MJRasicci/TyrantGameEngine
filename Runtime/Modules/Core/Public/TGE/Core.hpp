#pragma once

// Convenience header that pulls in the complete public surface area of the
// Core runtime module.  Applications can include this single header to access
// logging facilities and the module export macros without worrying about the
// underlying folder layout.

#include "TGE/Export.hpp"
#include "TGE/Features.hpp"

#include "TGE/Application/Application.hpp"
#include "TGE/Application/ApplicationLifetime.hpp"
#include "TGE/Application/ApplicationState.hpp"
#include "TGE/Application/IHostedService.hpp"

#include "TGE/Execution/Task.hpp"

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
