#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UINT_TYPE(b) uint##b##_t
#define INT_TYPE(b) int##b##_t

#define WRITE_STR_LITERAL(f, s) fwrite((s), 1, sizeof(s) - 1, f)

#define SAMPLE_RATE 44100

#define SAMPLE_BITS 16
#define NUM_CHANNELS 2 // stereo

typedef struct {
    // must be NUM_CHANNELS channels
    INT_TYPE(16) l; // num must equal SAMPLE_BITS
    INT_TYPE(16) r;
} bloc_t;

#define DURATION_S 8

#define FILE_NAME "output/created.wav"

size_t fwrite8(FILE *f, uint8_t b) {
    return fwrite(&b, sizeof(b), 1, f);
}
size_t fwrite16(FILE *f, uint16_t b) {
    uint8_t bytes[] = {
        (uint8_t)(b & 0xFF),
        (uint8_t)((b >> 8) & 0xFF)
    };
    return fwrite(bytes, sizeof(uint8_t), sizeof(b) / sizeof(uint8_t), f) / sizeof(b);
}
size_t fwrite32(FILE *f, uint32_t b) {
    uint8_t bytes[] = {
        (uint8_t)(b & 0xFF),
        (uint8_t)((b >> 8) & 0xFF),
        (uint8_t)((b >> 16) & 0xFF),
        (uint8_t)((b >> 24) & 0xFF),
    };
    return fwrite(bytes, sizeof(uint8_t), sizeof(b) / sizeof(uint8_t), f) / sizeof(b);
}


enum waveform_function_t {
    SINE,
    SAWTOOTH
};

float waveform_function(enum waveform_function_t func, float phase) {
    phase = phase - floorf(phase);

    switch (func) {
        case SINE:
            return sin(phase * 2 * M_PI);
        case SAWTOOTH:
            return -1.0f + phase * 2;
        default:
            return 0;
    }
}


float time_at_bloc_index(uint32_t i) {
    return (float) i / SAMPLE_RATE;
}

void write_normalized_sample_to_buffer_index(INT_TYPE(16) *buffer_i, float sample) {
    sample = sample > 1.0f ? 1.0f : (sample < -1.0f ? -1.0f : sample);
    *buffer_i = sample * INT16_MAX;
}

void make_audio(uint32_t num_blocs, INT_TYPE(16) buffer_l[], INT_TYPE(16) buffer_r[]) {
    // do whatever you want with the buffer to write audio data
    for (uint32_t i = 0; i < num_blocs; i++) {
        float t = time_at_bloc_index(i);
        float amplitude = 1.0f;
        if (t < 0.5f) {
            amplitude = t * 2;
            amplitude = powf(amplitude, 3.0f);
        } else if (t > 1 && t <= 2) {
            amplitude = 1.0f;
        } else if (t > 2) {
            amplitude = (8.0f - t) / (8.0f - 2.0f);
            amplitude = powf(amplitude, 4.0f);
        }
        float saw = amplitude * waveform_function(SAWTOOTH, t * 440.0);
        float sine_wub_wub = amplitude * waveform_function(SINE, t * 110.0) * waveform_function(SINE, t * 2.0f);
        float sine_sub = waveform_function(SINE, t * 40.0) * waveform_function(SINE, t * 0.5f);
        
        write_normalized_sample_to_buffer_index(&buffer_l[i], saw * 0.1f + sine_wub_wub * 0.5f + sine_sub * 0.4f);
        write_normalized_sample_to_buffer_index(&buffer_r[i], saw * 0.2f + sine_wub_wub * 0.4f + sine_sub * 0.4f);
    }
}

int main(int argc, char *argv[]) {
    FILE *f;

    if (!(argc > 1 && strcmp(argv[1], "overwrite") == 0)) {
        // check if file already exists
        f = fopen(FILE_NAME, "rb");
        if (f != NULL) {
            // file exists, terminate
            printf("file %s already exists, terminating\n", FILE_NAME);
            return 1;
        }
        // done checking if file exists
    }

    const uint32_t bytes_per_bloc = NUM_CHANNELS * SAMPLE_BITS / 8;
    const uint32_t bytes_per_second = SAMPLE_RATE * bytes_per_bloc;
    const uint32_t num_blocs = DURATION_S * SAMPLE_RATE;

    const uint32_t total_bytes_sampled_data = DURATION_S * bytes_per_second;
    const uint32_t total_bytes_file = 44 + total_bytes_sampled_data;

    f = fopen(FILE_NAME, "wb");
    if (f == NULL) {
        // failed to open file for writing
        printf("failed to open file %s for writing, terminating", FILE_NAME);
        return 1;
    }

    // format based on https://en.wikipedia.org/wiki/WAV
    // also referenced https://youtu.be/JqJPBu7GXvw

    // master RIFF chunk
    WRITE_STR_LITERAL(f, "RIFF");
    fwrite32(f, total_bytes_file - 8);
    WRITE_STR_LITERAL(f, "WAVE");

    // chunk describing the data format
    WRITE_STR_LITERAL(f, "fmt ");
    fwrite32(f, 0x0010);
    fwrite16(f, 0x01); // PCM int
    fwrite16(f, NUM_CHANNELS);
    fwrite32(f, SAMPLE_RATE);
    fwrite32(f, bytes_per_second); // bytes to read per second (freq * byte per bloc)
    fwrite16(f, bytes_per_bloc); // bytes per bloc (a bloc contains 1 sample per channel)
    fwrite16(f, SAMPLE_BITS);

    // chunk containing the samlped data
    WRITE_STR_LITERAL(f, "data");
    fwrite32(f, total_bytes_sampled_data);

    INT_TYPE(16)* buffer_l = (INT_TYPE(16)*)malloc(sizeof(INT_TYPE(16)) * num_blocs);
    INT_TYPE(16)* buffer_r = (INT_TYPE(16)*)malloc(sizeof(INT_TYPE(16)) * num_blocs);
    make_audio(num_blocs, buffer_l, buffer_r);

    // once audio has been written to the buffer, write it to the file
    size_t written_blocs = 0;
    for (uint32_t i = 0; i < num_blocs; i++) {
        size_t written =
            fwrite16(f, buffer_l[i]);
        written +=
            fwrite16(f, buffer_r[i]);
        written_blocs += written / 2;
    }

    fclose(f);

    printf("wrote to file %s\n", FILE_NAME);

    printf("SAMPLE DATA:\n"
            "\texpected:\t%u blocs \t(%u bytes)\n"
            "\tactual:  \t%lu blocs \t(%lu bytes)\n",
            num_blocs, total_bytes_sampled_data,
            written_blocs, written_blocs * bytes_per_bloc
            );

    printf("FULL FILE: expected %u bytes\n", total_bytes_file);

    return 0;
}

