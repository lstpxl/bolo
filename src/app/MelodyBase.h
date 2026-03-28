#pragma once

#include <cstdint>

// Interface for procedural melody generators used by MenuMusicPlayer.
// Implement Reset() to clear synthesis state and Synthesize(t, tense) to produce
// one sample at time t (seconds). Return values should be in [-1.0, 1.0].
class MelodyBase {
public:
    virtual ~MelodyBase() = default;

    // Audio stream sample rate this melody is designed for.
    // The player opens its AudioStream at this rate, so the melody's
    // discrete-time filters behave correctly. Override when a melody
    // is tuned for a rate other than the default 16 kHz.
    virtual std::uint32_t SampleRate() const { return 16000; }

    // Called by the player on initialize and whenever the stream restarts.
    virtual void Reset() = 0;

    // Produce one output sample. t is playback time in seconds.
    // tense lets melodies select an alternate tense tonal sequence.
    virtual float Synthesize(float t, bool tense) = 0;
};
