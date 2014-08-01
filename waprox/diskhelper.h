#ifndef __WAPROX_DISKHELPER_H__
#define __WAPROX_DISKHELPER_H__

int CreateDiskHelper();
void DiskSlaveMain(int fd, void* token);
void PreloadHints();

#endif /*__WAPROX_DISKHELPER_H__*/
