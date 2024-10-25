#include <pulse/simple.h>
#include <pulse/error.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <getopt.h>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n"
              << "Options:\n"
              << "  -i, --input FILE    Output WAV file name (default: output.wav)\n"
              << "  -h, --help           Show this help message\n";
}

int main(int argc, char* argv[]) {
    std::string inputFile = "input.wav";

    static struct option long_options[] = {
        {"input",   required_argument, 0, 'i'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "o:d:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'i':
                inputFile = optarg;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    std::cout << "Playing from " << inputFile << "\n";

    // Open and read WAV file
    std::ifstream file(inputFile, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file" << std::endl;
        return 1;
    }

    // Skip WAV header (44 bytes)
    file.seekg(44);

    // Read audio data
    std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});

    //// TODO make data format and data rate cli args
    // Set up PulseAudio
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.channels = 1;
    ss.rate = 44100;

    pa_simple *s = nullptr;
    int error;

    //// TODO make output device be a cli arg
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
