#pragma once
// Test-only POSIX boundary: compile the actual storage_probe.cpp unchanged.
struct stat {unsigned st_mode;};
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(mode) (((mode)&0170000)==S_IFDIR)
#define S_ISREG(mode) (((mode)&0170000)==S_IFREG)
int stat(const char* path,struct stat* info);
