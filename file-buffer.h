#ifndef FILE_BUFFER_H
#define FILE_BUFFER_H

#include <algorithm>
#include <fstream>

#include "util.h"

class FileBufferedWriter {
public:
    FileBufferedWriter(const char* filename, const uint32_t size);
    ~FileBufferedWriter();

    Range<uint32_t> write(const char* data, const Range<uint32_t>& r);
    uint32_t adjust();

    /// Return: total number of bytes written to file
    uint32_t close();

private:
    FileBufferedWriter(const FileBufferedWriter&);
    FileBufferedWriter& operator=(const FileBufferedWriter&);

    Range<uint32_t> write_in_buf(const char* data, const Range<uint32_t>& r);
    bool soft_reset();
    void hard_reset();

    std::ofstream of;
    char* buf;
    bool* acked;
    Range<uint32_t> buf_writing_window, file_writing_window;
    uint32_t tot_written = 0;

    const uint32_t buf_size;
    const uint32_t threashold;
};

#endif // FILE_BUFFER_H
