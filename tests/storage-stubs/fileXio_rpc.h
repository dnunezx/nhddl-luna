#ifndef STORAGE_TEST_FILEXIO_H
#define STORAGE_TEST_FILEXIO_H
int fileXioDevctl(const char *path, int command, void *input, int inputSize,
                 void *output, int outputSize);
#endif
