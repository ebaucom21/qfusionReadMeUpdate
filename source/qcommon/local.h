#ifndef WSW_bac442f1_f64d_4f40_a306_2fe44f42499c_H
#define WSW_bac442f1_f64d_4f40_a306_2fe44f42499c_H

#include "outputmessages.h"

#define comDebug()   wsw::PendingOutputMessage( wsw::createMessageStream( wsw::MessageCategory::Common, wsw::MessageSeverity::Debug ) ).getWriter()
#define comNotice()  wsw::PendingOutputMessage( wsw::createMessageStream( wsw::MessageCategory::Common, wsw::MessageSeverity::Info ) ).getWriter()
#define comWarning() wsw::PendingOutputMessage( wsw::createMessageStream( wsw::MessageCategory::Common, wsw::MessageSeverity::Warning ) ).getWriter()
#define comError()   wsw::PendingOutputMessage( wsw::createMessageStream( wsw::MessageCategory::Common, wsw::MessageSeverity::Error ) ).getWriter()

#endif