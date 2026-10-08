#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int initDriveboard();
void driveboardObserveWriteContext(uint64_t timestampNs, int endpoint, int writeApi, size_t requestedLength);
void processDrivePacket(uint8_t *buf, int player);
ssize_t driveboardRead(int fd, void *buf, size_t count);
ssize_t driveboardWrite(int fd, const void *buf, size_t count);
int driveBoardioctl(int fd, unsigned int request, void *data);

#ifdef __cplusplus
}
#endif
