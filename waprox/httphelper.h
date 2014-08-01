#ifndef __WAPROX_HTTPHELPER_H__
#define __WAPROX_HTTPHELPER_H__

/* extract HOST name from HTTP request */
char* GetHttpHostName(char* buffer, int length, unsigned short* dport);

#endif /*__WAPROX_HTTPHELPER_H__*/
