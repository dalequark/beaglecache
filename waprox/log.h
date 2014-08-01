#ifndef __WAPROX_LOG_H__
#define __WAPROX_LOG_H__

#include "applib.h"
#include "connection.h"

extern HANDLE hexLog;
extern HANDLE hstatLog;

void LogInit(int debug);
void LogSessionInfo(UserConnectionInfo* uci);

#endif /*__WAPROX_LOG_H__*/
