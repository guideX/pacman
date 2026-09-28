#pragma once

#include <guidexos/ui.h>

#include "pacman_audio.h"

// Loads the staged original WAV resources once into static application-owned
// PCM buffers. Missing or invalid audio remains a silent, non-fatal condition.
uint32_t pacman_audio_load_resources(gx_app_context* ctx);

// App Model play_pcm adapter used by the semantic event layer.
PacManAudioSubmitResult pacman_audio_submit(void* userData, PacManSoundId sound);
