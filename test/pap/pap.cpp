#include <pulse/simple.h>
#include <pulse/error.h>
#include <iostream>
#include <fstream>
#include <vector>

int main() {
    // Open and read WAV file
    std::ifstream file("tone.wav", std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file" << std::endl;
        return 1;
    }

    // Skip WAV header (44 bytes)
    file.seekg(44);

    // Read audio data
    std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});

    // Set up PulseAudio
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.channels = 2;
    ss.rate = 44100;

    pa_simple *s = nullptr;
    int error;

    s = pa_simple_new(NULL, "WAV Player", PA_STREAM_PLAYBACK, NULL, "Music", &ss, NULL, NULL, &error);

    if (!s) {
        std::cerr << "Failed to create PulseAudio stream: " << pa_strerror(error) << std::endl;
        return 1;
    }

    // Play audio
    if (pa_simple_write(s, buffer.data(), buffer.size(), &error) < 0) {
        std::cerr << "Failed to write audio data: " << pa_strerror(error) << std::endl;
        pa_simple_free(s);
        return 1;
    }

    // Clean up
    pa_simple_drain(s, &error);
    pa_simple_free(s);

    return 0;
}
