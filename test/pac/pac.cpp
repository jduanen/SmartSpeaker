#include <pulse/simple.h>
#include <pulse/error.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

// WAV header structure
struct WAVHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t overall_size;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt_chunk_marker[4] = {'f', 'm', 't', ' '};
    uint32_t length_of_fmt = 16;
    uint16_t format_type = 1;
    uint16_t channels = 1;
    uint32_t sample_rate = 44100;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample = 16;
    char data_chunk_header[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size;
};

int main() {
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.channels = 1;
    ss.rate = 44100;

    pa_simple *s = nullptr;
    int error;

    s = pa_simple_new(NULL, "AudioCapture", PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error);

    if (!s) {
        std::cerr << "Failed to create PulseAudio stream: " << pa_strerror(error) << std::endl;
        return 1;
    }

    const int duration = 10; // 10 seconds
    const int buffer_size = ss.rate * ss.channels * duration * sizeof(int16_t);
    std::vector<char> buffer(buffer_size);

    if (pa_simple_read(s, buffer.data(), buffer.size(), &error) < 0) {
        std::cerr << "Failed to read audio data: " << pa_strerror(error) << std::endl;
        pa_simple_free(s);
        return 1;
    }

    pa_simple_free(s);

    // Prepare WAV header
    WAVHeader header;
    header.channels = ss.channels;
    header.sample_rate = ss.rate;
    header.bits_per_sample = 16;
    header.byterate = header.sample_rate * header.channels * header.bits_per_sample / 8;
    header.block_align = header.channels * header.bits_per_sample / 8;
    header.data_size = buffer_size;
    header.overall_size = header.data_size + sizeof(WAVHeader) - 8;

    // Write WAV file
    std::ofstream outfile("output.wav", std::ios::binary);
    outfile.write(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));
    outfile.write(buffer.data(), buffer.size());
    outfile.close();

    std::cout << "Audio captured and saved to output.wav" << std::endl;

    return 0;
}
