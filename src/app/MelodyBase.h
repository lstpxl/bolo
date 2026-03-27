#pragma once

// Interface for procedural melody generators used by MenuMusicPlayer.
// Implement Reset() to clear synthesis state and Synthesize(t) to produce
// one sample at time t (seconds). Return values should be in [-1.0, 1.0].
class MelodyBase {
public:
    virtual ~MelodyBase() = default;

    // Called by the player on initialize and whenever the stream restarts.
    virtual void Reset() = 0;

    // Produce one output sample. t is playback time in seconds.
    virtual float Synthesize(float t) = 0;
};
