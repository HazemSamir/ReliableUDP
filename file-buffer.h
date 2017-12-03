#ifndef FILE_BUFFER_H
#define FILE_BUFFER_H

#include <algorithm>
#include <fstream>

class FileBufferedWriter {
public:
    FileBufferedWriter(const char* filename, const int size);
    ~FileBufferedWriter();

    Range write(const char* data, const Range& r);
    int adjust();

    /// Return: total number of bytes written to file
    int close();

private:
    Range write_in_buf(const char* data, const Range& r);
    bool soft_reset();
    void hard_reset();

    std::ofstream of;
    char* buf;
    bool* acked;
    Range buf_writing_window, file_writing_window;
    int tot_written = 0;

    const int buf_size;
    const int threashold;
};

#endif // FILE_BUFFER_H
