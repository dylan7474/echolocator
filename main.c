#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "font_data.h"

// --- Configuration ---
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define APP_NAME "Acoustic Echolocator v2"
#define FONT_SIZE 18

// Audio settings
#define AUDIO_FREQ 44100
#define AUDIO_FORMAT AUDIO_S16SYS
#define AUDIO_CHANNELS 1
#define AUDIO_SAMPLES 4096
#define RECORDING_SECONDS 1
#define RECORDING_BUFFER_SIZE (AUDIO_FREQ * RECORDING_SECONDS * sizeof(Sint16))

// Beep sound generation
#define BEEP_DURATION_MS 50
#define BEEP_FREQUENCY 1500.0f
#define SPEED_OF_SOUND 343.0f // meters per second

// --- Structs ---
typedef enum {
    STATE_IDLE,
    STATE_RECORDING,
    STATE_ANALYZING,
    STATE_DONE
} AppState;

typedef struct {
    int index;
    Sint16 amplitude;
    float time_s;
    float distance_m;
} AudioPeak;

// --- Global Variables ---
SDL_Window* gWindow = NULL;
SDL_Renderer* gRenderer = NULL;
TTF_Font* gFont = NULL;
Mix_Chunk* gBeepSound = NULL;
SDL_AudioDeviceID gRecordingDevice;

// App state
AppState gState = STATE_IDLE;
Sint16* gRecordingBuffer = NULL;
volatile int gBufferPosition = 0;

// Analysis results
AudioPeak gDetectedPeaks[20];
int gPeakCount = 0;
float gDetectionThreshold = 0.15f; // 15% of max amplitude

// --- Function Prototypes ---
bool init();
void close_app();
void render_text(const char* text, int x, int y, SDL_Color color);
Mix_Chunk* generate_beep_sound();
bool init_audio_recording();
void audio_recording_callback(void* userdata, Uint8* stream, int len);
void start_test();
void analyze_recording();
void handle_input(const SDL_Event* e);
void render();

// --- Main ---
int main(int argc, char* argv[]) {
    if (!init()) {
        printf("Failed to initialize application!\n");
        close_app();
        return 1;
    }

    bool quit = false;
    SDL_Event e;

    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
            handle_input(&e);
        }

        if (gState == STATE_RECORDING && gBufferPosition >= RECORDING_BUFFER_SIZE) {
            gState = STATE_ANALYZING;
            SDL_PauseAudioDevice(gRecordingDevice, 1);
            analyze_recording();
            gState = STATE_DONE;
        }

        render();
    }

    close_app();
    return 0;
}

// --- Function Implementations ---

void handle_input(const SDL_Event* e) {
    if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
            case SDLK_SPACE:
                if (gState == STATE_IDLE || gState == STATE_DONE) {
                    start_test();
                }
                break;
            case SDLK_UP:
                gDetectionThreshold += 0.01f;
                if (gDetectionThreshold > 1.0f) gDetectionThreshold = 1.0f;
                if (gState == STATE_DONE) { // Re-analyze with new threshold
                    analyze_recording();
                }
                break;
            case SDLK_DOWN:
                gDetectionThreshold -= 0.01f;
                if (gDetectionThreshold < 0.01f) gDetectionThreshold = 0.01f;
                if (gState == STATE_DONE) { // Re-analyze with new threshold
                    analyze_recording();
                }
                break;
        }
    }
}

void render() {
    SDL_SetRenderDrawColor(gRenderer, 0x1A, 0x1A, 0x1A, 0xFF);
    SDL_RenderClear(gRenderer);

    // Draw Waveform
    if (gState == STATE_DONE) {
        SDL_SetRenderDrawColor(gRenderer, 0x7F, 0xFF, 0xD4, 0xFF); // Aquamarine
        for (int i = 0; i < (gBufferPosition / (int)sizeof(Sint16)) - 1; ++i) {
            float x1 = (float)i / (AUDIO_FREQ * RECORDING_SECONDS) * SCREEN_WIDTH;
            float y1 = SCREEN_HEIGHT / 2.0f - (gRecordingBuffer[i] / 32767.0f) * (SCREEN_HEIGHT / 2.0f);
            float x2 = (float)(i + 1) / (AUDIO_FREQ * RECORDING_SECONDS) * SCREEN_WIDTH;
            float y2 = SCREEN_HEIGHT / 2.0f - (gRecordingBuffer[i+1] / 32767.0f) * (SCREEN_HEIGHT / 2.0f);
            SDL_RenderDrawLineF(gRenderer, x1, y1, x2, y2);
        }

        // Draw Peak markers
        SDL_SetRenderDrawColor(gRenderer, 0xFF, 0x45, 0x00, 0xFF); // OrangeRed
        for (int i = 0; i < gPeakCount; ++i) {
            float peak_x = (float)gDetectedPeaks[i].index / (AUDIO_FREQ * RECORDING_SECONDS) * SCREEN_WIDTH;
            SDL_RenderDrawLine(gRenderer, (int)peak_x, 0, (int)peak_x, SCREEN_HEIGHT);
        }
    }

    // --- Render UI Text ---
    char buffer[128];
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color green = {127, 255, 212, 255};
    SDL_Color orange = {255, 69, 0, 255};

    switch(gState) {
        case STATE_IDLE:
            render_text("Press SPACE to start test", 10, 10, white);
            break;
        case STATE_RECORDING:
            render_text("Recording...", 10, 10, orange);
            break;
        case STATE_ANALYZING:
            render_text("Analyzing...", 10, 10, orange);
            break;
        case STATE_DONE:
            render_text("Test complete. Press SPACE for new test.", 10, 10, green);
            break;
    }
    
    snprintf(buffer, sizeof(buffer), "Echo Threshold: %.0f%% (Up/Down keys to change)", gDetectionThreshold * 100.0f);
    render_text(buffer, 10, 40, white);

    if (gState == STATE_DONE) {
        snprintf(buffer, sizeof(buffer), "Peaks Found: %d", gPeakCount);
        render_text(buffer, 10, 70, white);
        if (gPeakCount > 1) {
            snprintf(buffer, sizeof(buffer), "First Echo Distance: %.2f m", gDetectedPeaks[1].distance_m);
            render_text(buffer, 10, 100, green);
        }
    }

    SDL_RenderPresent(gRenderer);
}


void start_test() {
    gState = STATE_RECORDING;
    memset(gRecordingBuffer, 0, RECORDING_BUFFER_SIZE);
    gBufferPosition = 0;
    gPeakCount = 0;

    SDL_PauseAudioDevice(gRecordingDevice, 0); // Start recording
    Mix_PlayChannel(-1, gBeepSound, 0); // Play beep
}

void analyze_recording() {
    gPeakCount = 0;
    int total_samples = gBufferPosition / (int)sizeof(Sint16);
    if (total_samples == 0) return;

    // Find absolute max amplitude
    Sint16 max_amplitude = 0;
    int max_amp_index = 0;
    for (int i = 0; i < total_samples; ++i) {
        if (abs(gRecordingBuffer[i]) > max_amplitude) {
            max_amplitude = abs(gRecordingBuffer[i]);
            max_amp_index = i;
        }
    }
    if (max_amplitude == 0) return;

    // Add first peak (the direct beep)
    gDetectedPeaks[gPeakCount].index = max_amp_index;
    gDetectedPeaks[gPeakCount].amplitude = max_amplitude;
    gPeakCount++;
    
    // Find subsequent peaks (echoes)
    Sint16 threshold_val = max_amplitude * gDetectionThreshold;
    int search_start_index = max_amp_index + (int)((BEEP_DURATION_MS * 1.5 / 1000.0f) * AUDIO_FREQ);

    for (int i = search_start_index; i < total_samples - 1 && gPeakCount < 20; ++i) {
        // Smarter peak detection: must be a local maximum
        if (abs(gRecordingBuffer[i]) > threshold_val &&
            abs(gRecordingBuffer[i]) > abs(gRecordingBuffer[i - 1]) &&
            abs(gRecordingBuffer[i]) > abs(gRecordingBuffer[i + 1])) {
            
            AudioPeak* p = &gDetectedPeaks[gPeakCount];
            p->index = i;
            p->amplitude = abs(gRecordingBuffer[i]);
            
            // Calculate distance based on time delta from the *first* peak
            float time_delta = (float)(p->index - gDetectedPeaks[0].index) / AUDIO_FREQ;
            p->distance_m = (time_delta * SPEED_OF_SOUND) / 2.0f;
            p->time_s = time_delta;

            gPeakCount++;

            // Skip forward to avoid detecting the same peak cluster
            i += (int)((BEEP_DURATION_MS / 1000.0f) * AUDIO_FREQ);
        }
    }
}


// --- Init and Cleanup ---

bool init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return false;

    gWindow = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!gWindow) return false;

    gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!gRenderer) return false;

    if (Mix_OpenAudio(AUDIO_FREQ, MIX_DEFAULT_FORMAT, AUDIO_CHANNELS, AUDIO_SAMPLES) < 0) return false;
    
    if (TTF_Init() == -1) return false;

    SDL_RWops* rw = SDL_RWFromConstMem(RobotoMono_Regular_ttf, RobotoMono_Regular_ttf_len);
    if (!rw) {
        printf("Failed to create RWops from font data!\n");
        return false;
    }
    gFont = TTF_OpenFontRW(rw, 1, FONT_SIZE); // The '1' auto-closes the RWops stream
    if (!gFont) {
        printf("Failed to load font from memory! TTF_Error: %s\n", TTF_GetError());
        return false;
    }

    gBeepSound = generate_beep_sound();
    if (!gBeepSound) return false;

    if (!init_audio_recording()) return false;
    
    gRecordingBuffer = (Sint16*)malloc(RECORDING_BUFFER_SIZE);
    if (!gRecordingBuffer) return false;
    
    return true;
}

void close_app() {
    free(gRecordingBuffer);
    if (gBeepSound) Mix_FreeChunk(gBeepSound);
    if (gFont) TTF_CloseFont(gFont);
    if (gRecordingDevice > 0) SDL_CloseAudioDevice(gRecordingDevice);
    TTF_Quit();
    Mix_Quit();
    if (gRenderer) SDL_DestroyRenderer(gRenderer);
    if (gWindow) SDL_DestroyWindow(gWindow);
    SDL_Quit();
}

void render_text(const char* text, int x, int y, SDL_Color color) {
    SDL_Surface* text_surface = TTF_RenderText_Blended(gFont, text, color);
    if (!text_surface) {
        return; // Failed to create surface
    }
    
    SDL_Texture* text_texture = SDL_CreateTextureFromSurface(gRenderer, text_surface);
    // We can free the surface now as its data has been copied to the texture
    SDL_FreeSurface(text_surface);

    if (!text_texture) {
        return; // Failed to create texture
    }

    SDL_Rect dest_rect = {x, y, 0, 0};
    // Get the width and height from the texture
    SDL_QueryTexture(text_texture, NULL, NULL, &dest_rect.w, &dest_rect.h);
    
    // Render the texture
    SDL_RenderCopy(gRenderer, text_texture, NULL, &dest_rect);

    // Clean up the texture
    SDL_DestroyTexture(text_texture);
}

// ---- Boring stuff from V1 ----
Mix_Chunk* generate_beep_sound() {
    int sample_count = (int)((BEEP_DURATION_MS / 1000.0f) * AUDIO_FREQ);
    int buffer_size = sample_count * sizeof(Sint16);
    Sint16* buffer = (Sint16*)malloc(buffer_size);
    if (!buffer) return NULL;
    for (int i = 0; i < sample_count; ++i) {
        double time = (double)i / AUDIO_FREQ;
        buffer[i] = (Sint16)(32767.0 * sin(2.0 * M_PI * BEEP_FREQUENCY * time));
    }
    Mix_Chunk* chunk = Mix_QuickLoad_RAW((Uint8*)buffer, buffer_size);
    free(buffer);
    return chunk;
}
void audio_recording_callback(void* userdata, Uint8* stream, int len) {
    if (gState != STATE_RECORDING) return;
    int copy_len = len > (RECORDING_BUFFER_SIZE - gBufferPosition) ? (RECORDING_BUFFER_SIZE - gBufferPosition) : len;
    if (copy_len > 0) {
        memcpy(gRecordingBuffer + (gBufferPosition / sizeof(Sint16)), stream, copy_len);
        gBufferPosition += copy_len;
    }
}
bool init_audio_recording() {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_FREQ;
    want.format = AUDIO_FORMAT;
    want.channels = AUDIO_CHANNELS;
    want.samples = AUDIO_SAMPLES;
    want.callback = audio_recording_callback;
    gRecordingDevice = SDL_OpenAudioDevice(NULL, 1, &want, &have, 0); // 1 for capture
    if (gRecordingDevice == 0) return false;
    return true;
}
