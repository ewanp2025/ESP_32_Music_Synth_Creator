#ifndef SYNTH_SDK_MODULES_H
#define SYNTH_SDK_MODULES_H

#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <iomanip>

#include "xpressive_parser.h"
#include "effect.h"

#ifndef M_PI_F
#define M_PI_F 3.14159265358979f
#endif

namespace SynthSDK {


// PARAMETER ID & METADATA

enum ParamId : int {
    // Oscillator 1 & 2
    P_OSC1_TYPE = 0,   // 0=VA, 1=M1Organ, 2=Harmonium, 3=FM, 4=Supersaw, 5=Karplus, 6=Vowel, 7=Additive, 8=ChipNoise, 9=XpressiveAST
    P_OSC1_WAVE,
    P_OSC1_COARSE,
    P_OSC1_FINE,
    P_OSC1_SHAPE,
    P_OSC1_TIMBRE,
    P_OSC1_LEVEL,

    P_OSC2_TYPE,
    P_OSC2_COARSE,
    P_OSC2_FINE,
    P_OSC2_SHAPE,
    P_OSC2_LEVEL,
    P_SUB_LEVEL,
    P_NOISE_LEVEL,
    P_RING_MOD,

    // 8-Bit Sampler & Live Audio-In Module
    P_SMP_LEVEL,       // 0..1 Sampler voice level in polyphonic mix
    P_SMP_MODE,        // 0=Off, 1=One-Shot, 2=Loop Sustain, 3=Ping-Pong Loop
    P_SMP_PITCH,       // -24 .. +24 semitones
    P_SMP_START,       // 0.0 .. 0.95 sample start offset
    P_SMP_END,         // 0.05 .. 1.0 sample end/loop point
    P_SMP_SRATE_DIV,   // 1 .. 8 sample rate divider (vintage aliasing)
    P_SMP_IN_GAIN,     // Audio-In recording / pass-through gain (0.1 .. 4.0)
    P_SMP_THRU_MON,    // Route live Audio-In directly through Synth Filter & FX (0..1)
    P_SMP_ADC_PIN,     // ESP32 ADC GPIO pin for hardware Audio-In (-1 = Off)

    // Filter Section
    P_FILT_TYPE,       // 0=SVF_LP, 1=SVF_BP, 2=SVF_HP, 3=Ladder24dB, 4=303, 5=Formant
    P_FILT_CUTOFF,
    P_FILT_RESO,
    P_FILT_DRIVE,
    P_FILT_ENV_AMT,
    P_FILT_KEYTRK,
    P_CABINET_TF_EN,

    // Envelopes (Amp, Filter, Mod/Pitch)
    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,
    P_FLT_A, P_FLT_D, P_FLT_S, P_FLT_R,
    P_MOD_A, P_MOD_D, P_MOD_S, P_MOD_R,
    P_MOD_PITCH_AMT,

    // LFO 1, LFO 2, & Expression
    P_LFO1_WAVE,
    P_LFO1_RATE,
    P_LFO1_DEPTH,
    P_LFO1_TARGET,

    P_LFO2_WAVE,
    P_LFO2_RATE,
    P_LFO2_DEPTH,
    P_LFO2_TARGET,

    P_BELLOWS_PRESSURE,
    P_PORTAMENTO,

    // Arpeggiator, Step Sequencer & 6-Track Drum Machine
    P_ARP_MODE,        // 0=Off, 1=Up, 2=Down, 3=UpDown, 4=Random, 5=16-Step Sequencer
    P_ARP_BPM,         // 40 .. 240 BPM (Master Clock for Arp & Drum Machine)
    P_ARP_GATE,        // 0.1 .. 0.98
    P_ARP_OCTAVES,     // 1 .. 4
    P_ARP_SWING,       // 0.0 .. 0.65 shuffle swing (shared with Drum Machine)

    P_DRUM_ENABLE,     // 0=Stopped, 1=Running
    P_DRUM_LEVEL,      // 0..1.2 Master Drum Machine Volume
    P_DRUM_CRUSH,      // 0..1 8-Bit Crunch on Drum Bus

    // 10 Master Effects Chain
    P_FX_DRIVE_AMT,
    P_FX_CRUSH_BITS,
    P_FX_CRUSH_RATE,
    P_FX_PHASER_MIX,
    P_FX_PHASER_RATE,
    P_FX_FLANGER_MIX,
    P_FX_CHORUS_MIX,
    P_FX_CHORUS_DEPTH,
    P_FX_DELAY_MIX,
    P_FX_DELAY_TIME,
    P_FX_DELAY_FB,
    P_FX_REVERB_MIX,
    P_FX_REVERB_SIZE,
    P_FX_SUB_BOOST,
    P_FX_EQ_LOW,
    P_FX_EQ_MID,
    P_FX_EQ_HIGH,
    P_MASTER_VOL,

    PARAM_COUNT
};

enum GpioPinMode : int {
    GPIO_NONE = 0,
    GPIO_ADC_POT,          // AnalogRead 0..4095 -> normalized parameter
    GPIO_DIGITAL_BTN_TOG,  // Momentary button toggles parameter 0/1
    GPIO_DIGITAL_BTN_MOM,  // Momentary gate / trigger
    GPIO_ROTARY_ENC,       // Quadrature encoder increment/decrement
    GPIO_BELLOWS_HALL      // Continuous expression sensor
};

struct ParamSpec {
    int id = 0;
    std::string name;
    std::string shortName;
    std::string category;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultVal = 0.5f;
    float curVal = 0.5f;
    int stepCount = 0;

    // Hardware & MIDI Bindings (Works for both On-Screen and Off-Screen controls)
    int midiCc = -1;
    int gpioMode = GPIO_NONE;
    int gpioPinA = -1;
    int gpioPinB = -1;
    std::string hardwareNote; // Documentation note (e.g., "Front panel Cutoff Pot on GPIO 4")

    float getNormalized() const {
        if (maxVal <= minVal) return 0.0f;
        return std::clamp((curVal - minVal) / (maxVal - minVal), 0.0f, 1.0f);
    }

    void setNormalized(float norm) {
        norm = std::clamp(norm, 0.0f, 1.0f);
        float v = minVal + norm * (maxVal - minVal);
        if (stepCount > 0) v = std::round(v);
        curVal = std::clamp(v, minVal, maxVal);
    }
};


// UI WIDGETS, ARPEGGIATOR STEPS & DRUM MACHINE SPECS

enum WidgetType : int {
    W_ROTARY_KNOB = 0,
    W_VERT_FADER,
    W_HORIZ_SLIDER,
    W_TOGGLE_BTN,
    W_MOMENTARY_PAD,
    W_CYCLE_SELECT,
    W_XY_PAD,
    W_ADSR_GRAPH,
    W_OSCILLOSCOPE,
    W_STEP_SEQ_GRID,
    W_TOUCH_RIBBON,
    W_PIANO_KEYBOARD,
    W_DRUM_GRID,       // 6x16 Drum Machine Matrix Widget
    W_SAMPLER_WAVE,    // 8-Bit Sampler Waveform & Loop Marker Widget
    WIDGET_TYPE_COUNT
};

struct UiWidgetSpec {
    int uid = 0;
    WidgetType type = W_ROTARY_KNOB;
    std::string label = "Cutoff";
    int x = 10, y = 50, w = 70, h = 80;
    int paramId1 = P_FILT_CUTOFF;
    int paramId2 = P_FILT_RESO;
    int midiNote = 60;
    uint16_t bgCol565 = 0x18E3;
    uint16_t fgCol565 = 0x073F;
    uint16_t textCol565 = 0xFFFF;
};

struct ArpStepSpec {
    bool  gate = true;
    int   semitone = 0;   // -24 .. +24
    int   octave = 0;     // -2 .. +2
    float velocity = 0.9f;// 0.1 .. 1.0
    bool  slide = false;  // 303-style portamento tie
};

struct DrumTrackSpec {
    std::string name;
    bool  muted = false;
    float level = 0.85f;
    float tune  = 0.50f;  // 0..1 pitch scaling
    float decay = 0.50f;  // 0..1 envelope decay
    bool  steps[16] = {false};
};

struct ModMatrixSlot {
    bool enabled = false;
    int source = 0; // 0=Off, 1=LFO1, 2=LFO2, 3=ModEnv, 4=FiltEnv, 5=Velocity, 6=KeyTrack, 7=BellowsADC, 8=ModWheel
    int destParam = P_FILT_CUTOFF;
    float amount = 0.0f;
};

struct BoardPinoutConfig {
    int i2sBclk = 6;
    int i2sLrck = 16;
    int i2sDout = 15;
    int i2cSda  = 8;
    int i2cScl  = 9;
    int audioOutMode = 1;
};


// MODULAR DSP SYNTHESIS ENGINE

class ModularSynthEngine {
public:
    static constexpr int MAX_VOICES = 8;
    static constexpr int SCOPE_SIZE = 256;
    static constexpr int NUM_DRUM_TRACKS = 6;
    static constexpr int MAX_SAMPLE_BYTES = 16384; // 16 KB 8-bit sample (~1.5s @ 11kHz or 0.75s @ 22kHz)

    std::string synthName = "ESP32_Custom_Synth";
    std::string customOsc1Formula =
        "0.6 * saww(integrate(f)) + 0.4 * squarew(integrate(f * cent(8))) * exp(-3 * t)";
    std::string customCabinetFormula = TransferFunctionFX::defaultCombinedHarmoniumTf();

    std::array<ParamSpec, PARAM_COUNT> params;
    std::array<ModMatrixSlot, 8> modMatrix;
    std::array<ArpStepSpec, 16> arpSteps;
    std::array<DrumTrackSpec, NUM_DRUM_TRACKS> drumTracks;
    BoardPinoutConfig boardPins;

    // 8-Bit Signed PCM Sample Buffer (-128 .. +127)
    std::vector<int8_t> samplerData;
    bool isRecordingSample = false;
    int  recordWritePos = 0;
    float liveAudioInLevel = 0.0f;

    // UI Widget Layouts per Screen Profile (0: 800x480, 1: 320x240, 2: 128x64)
    std::vector<UiWidgetSpec> profileWidgets[3];

    // MIDI Learn State
    int midiLearnParamId = -1;
    int lastLearnedCc = -1;

    ModularSynthEngine() : m_cabinetFx(22050.0f) {
        initDefaultParams();
        initDefaultArpAndDrums();
        generateDefault8BitSample();
        recompileCustomAst();
    }

    void init(float sampleRate = 22050.0f) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_srate = std::max(8000.0f, sampleRate);
        m_cabinetFx.setSampleRate(m_srate);
        m_cabinetFx.setFormula(customCabinetFormula.c_str());
        m_delayBuf.assign(static_cast<size_t>(m_srate * 1.2f), 0.0f);
        m_chorusBuf.assign(2048, 0.0f);
        for (int i = 0; i < 4; ++i) {
            m_revCombs[i].assign(m_revLengths[i], 0.0f);
            m_revIdx[i] = 0;
        }
        for (int i = 0; i < 2; ++i) {
            m_revAp[i].assign(m_apLengths[i], 0.0f);
            m_apIdx[i] = 0;
        }
    }


    // 8-BIT SAMPLER MANAGEMENT (AUDIO-IN RECORDING, AST BAKE, DEFAULT CHORD)

    void generateDefault8BitSample() {
        // Pre-loads a punchy 8-bit minor chord hit (Amiga style) so the sampler works out of the box
        samplerData.resize(8192, 0);
        const auto& lut = XpressiveParser::lut();
        for (size_t i = 0; i < samplerData.size(); ++i) {
            float t = static_cast<float>(i) / 22050.0f;
            float env = (1.0f - std::exp(-80.0f * t)) * std::exp(-5.5f * t);
            float fRoot = 261.63f; // C4
            float fMin3 = 311.13f; // Eb4
            float fFifth= 392.00f; // G4
            float sig = 0.45f * lut.sinUnit(fRoot * t) + 0.35f * lut.sinUnit(fMin3 * t)
                        + 0.35f * lut.sinUnit(fFifth * t) + 0.20f * (( std::fmod(fRoot * t, 1.0f) < 0.5f ) ? 1.0f : -1.0f);
            int q = std::clamp(static_cast<int>( std::round(sig * env * 120.0f) ), -127, 127);
            samplerData[i] = static_cast<int8_t>(q);
        }
    }

    void bakeFormulaTo8BitSample(const std::string& formulaStr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto ast = XpressiveParser::parse(formulaStr.c_str());
        if (!ast) return;
        samplerData.resize(11025, 0); // 0.5 sec @ 22050 Hz
        float intState[16] = {0.0f};
        float lastOut = 0.0f;
        for (size_t i = 0; i < samplerData.size(); ++i) {
            XpressiveParser::Env env;
            env.t = static_cast<float>(i) / m_srate;
            env.srate = m_srate;
            env.tempo = params[P_ARP_BPM].curVal;
            env.f = 261.63f; // C4 root
            env.A1 = params[P_OSC1_SHAPE].curVal;
            env.A2 = params[P_OSC1_TIMBRE].curVal;
            env.A3 = 0.5f;
            env.lastOut = lastOut;
            env.intState = intState;
            env.numIntegrators = 16;
            float v = std::clamp(ast->eval(env), -1.0f, 1.0f);
            lastOut = v;
            samplerData[i] = static_cast<int8_t>(std::clamp(static_cast<int>(v * 127.0f), -127, 127));
        }
    }

    void startRecordingAudioIn() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (samplerData.size() < 4096) samplerData.resize(11025, 0);
        std::fill(samplerData.begin(), samplerData.end(), 0);
        recordWritePos = 0;
        isRecordingSample = true;
    }

    void stopRecordingAudioIn() {
        std::lock_guard<std::mutex> lock(m_mutex);
        isRecordingSample = false;
    }

    void pushAudioInSamples(const float* inMono, int count) {
        if (!inMono || count <= 0) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        float gain = params[P_SMP_IN_GAIN].curVal;
        float peak = 0.0f;
        for (int i = 0; i < count; ++i) {
            float s = std::clamp(inMono[i] * gain, -1.0f, 1.0f);
            peak = std::max(peak, std::fabs(s));

            // Store in ring buffer for live filter pass-through
            m_audioInRing[m_audioInWrite] = s;
            m_audioInWrite = (m_audioInWrite + 1) & (SCOPE_SIZE - 1);

            // Quantize to 8-bit signed PCM if recording into sampler
            if (isRecordingSample && !samplerData.empty()) {
                samplerData[recordWritePos] = static_cast<int8_t>(std::clamp(static_cast<int>(s * 127.0f), -127, 127));
                if (++recordWritePos >= static_cast<int>(samplerData.size())) {
                    isRecordingSample = false; // Buffer full
                }
            }
        }
        liveAudioInLevel = liveAudioInLevel * 0.7f + peak * 0.3f;
    }

    // DRUM MACHINE & ARPEGGIATOR PRESETS

    void initDefaultArpAndDrums() {
        static const int defNotes[16] = {0, 0, 12, 0, 7, 10, 0, 12, 0, 0, 10, 7, 5, 3, 0, -2};
        static const bool defGates[16]= {1, 1, 1,  0, 1, 1,  0, 1,  1, 0, 1,  1, 1, 1, 0, 1};
        static const bool defSlide[16]= {0, 1, 0,  0, 0, 1,  0, 0,  0, 0, 1,  0, 0, 1, 0, 0};
        for (int i = 0; i < 16; ++i) {
            arpSteps[i].gate     = defGates[i];
            arpSteps[i].semitone = defNotes[i];
            arpSteps[i].octave   = 0;
            arpSteps[i].velocity = (i % 4 == 0) ? 1.0f : 0.78f;
            arpSteps[i].slide    = defSlide[i];
        }

        const char* dNames[NUM_DRUM_TRACKS] = {
            "1: 808/909 Kick", "2: Crispy Snare", "3: House Clap",
            "4: Closed Hat",   "5: Open Hat",     "6: FM Tom / Perc"
        };
        for (int t = 0; t < NUM_DRUM_TRACKS; ++t) {
            drumTracks[t].name  = dNames[t];
            drumTracks[t].muted = false;
            drumTracks[t].level = 0.85f;
            drumTracks[t].tune  = 0.50f;
            drumTracks[t].decay = 0.50f;
            std::memset(drumTracks[t].steps, 0, sizeof(drumTracks[t].steps));
        }

        for (int s = 0; s < 16; s += 4) drumTracks[0].steps[s] = true; // Kick on 1, 5, 9, 13
        drumTracks[1].steps[4] = drumTracks[1].steps[12] = true;       // Snare on 5, 13
        drumTracks[2].steps[4] = drumTracks[2].steps[12] = true;       // Clap on 5, 13
        drumTracks[3].steps[0] = drumTracks[3].steps[6]  = drumTracks[3].steps[8] = drumTracks[3].steps[14] = true; // CH
        drumTracks[4].steps[2] = drumTracks[4].steps[10] = drumTracks[4].steps[14]= true; // Open Hat on offbeats
        drumTracks[5].steps[3] = drumTracks[5].steps[11] = drumTracks[5].steps[15]= true; // Syncopated FM Perc
    }

    void triggerDrumTrack(int trackIdx, float vel = 1.0f) {
        if (trackIdx < 0 || trackIdx >= NUM_DRUM_TRACKS) return;
        if (drumTracks[trackIdx].muted) return;
        // Closed Hat (3) chokes Open Hat (4)
        if (trackIdx == 3) m_drumStates[4].active = false;
        auto& ds = m_drumStates[trackIdx];
        ds.active = true;
        ds.t = 0.0f;
        ds.phase = 0.0f;
        ds.phase2 = 0.0f;
        ds.vel = vel;
    }

    void recompileCustomAst() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_customAst = XpressiveParser::parse(customOsc1Formula.c_str());
        m_cabinetFx.setFormula(customCabinetFormula.c_str());
    }

    void handleMidiCC(int cc, int value7bit) {
        float norm = std::clamp(value7bit / 127.0f, 0.0f, 1.0f);
        if (midiLearnParamId >= 0 && midiLearnParamId < PARAM_COUNT) {
            params[midiLearnParamId].midiCc = cc;
            lastLearnedCc = cc;
            midiLearnParamId = -1;
        }
        if (cc == 1) m_modWheel = norm;
        for (auto& p : params) {
            if (p.midiCc == cc) p.setNormalized(norm);
        }
    }

    void handlePitchBend(float bendNormMinus1To1) {
        m_pitchBendSemis = std::clamp(bendNormMinus1To1, -1.0f, 1.0f) * 2.0f;
    }

    void noteOn(int midiNote, int velocity) {
        std::lock_guard<std::mutex> lock(m_mutex);
        midiNote = std::clamp(midiNote, 12, 108);
        if (std::find(m_heldKeys.begin(), m_heldKeys.end(), midiNote) == m_heldKeys.end()) {
            m_heldKeys.push_back(midiNote);
            std::sort(m_heldKeys.begin(), m_heldKeys.end());
        }
        if (static_cast<int>(params[P_ARP_MODE].curVal) == 0) {
            triggerVoiceInternal(midiNote, velocity / 127.0f, false);
        }
    }

    void noteOff(int midiNote) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_heldKeys.erase(std::remove(m_heldKeys.begin(), m_heldKeys.end(), midiNote), m_heldKeys.end());
        for (auto& v : m_voices) {
            if (v.active && v.held && v.midiNote == midiNote) {
                v.held = false;
                v.trel = 0.0f;
            }
        }
    }

    void getScopeSnapshot(float* outBuf256) const {
        int r = m_scopeWriteIdx;
        for (int i = 0; i < SCOPE_SIZE; ++i) {
            outBuf256[i] = m_scopeRing[(r + i) & (SCOPE_SIZE - 1)];
        }
    }

    int getCurrentSeqStep() const { return m_seqStep; }


    // REAL-TIME MASTER AUDIO RENDERER (SYNTH + 8-BIT SAMPLER + DRUMS + FX)

    void renderFramesMono(float* outMono, int numFrames) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const float dt = 1.0f / m_srate;
        const auto& lut = XpressiveParser::lut();

        float baseVals[PARAM_COUNT];
        for (int i = 0; i < PARAM_COUNT; ++i) baseVals[i] = params[i].curVal;

        int arpMode = static_cast<int>(baseVals[P_ARP_MODE]);
        bool drumsRunning = (baseVals[P_DRUM_ENABLE] > 0.5f);
        float bpm = std::max(40.0f, baseVals[P_ARP_BPM]);
        float baseStepSec = 15.0f / bpm; // 1/16th note
        float swing = std::clamp(baseVals[P_ARP_SWING], 0.0f, 0.65f);

        for (int f = 0; f < numFrames; ++f) {
            // 1. Master 16th-Note Clock (Shared by Arpeggiator & 6-Track Drum Machine)
            bool clockActive = (arpMode > 0 && (!m_heldKeys.empty() || arpMode == 5)) || drumsRunning;
            float curStepDur = baseStepSec * ((m_seqStep & 1) ? (1.0f - 0.5f * swing) : (1.0f + 0.5f * swing));

            if (clockActive) {
                m_arpTimer += dt;
                float gateLen = curStepDur * std::clamp(baseVals[P_ARP_GATE], 0.1f, 0.98f);
                if (m_arpNoteActive && !m_currentStepSlide && m_arpTimer >= gateLen) {
                    for (auto& v : m_voices) {
                        if (v.active && v.held) { v.held = false; v.trel = 0.0f; }
                    }
                    m_arpNoteActive = false;
                }

                if (m_arpTimer >= curStepDur) {
                    m_arpTimer -= curStepDur;
                    m_seqStep = (m_seqStep + 1) & 15;

                    // Trigger Drum Machine Tracks on this 16th step
                    if (drumsRunning) {
                        for (int dtIdx = 0; dtIdx < NUM_DRUM_TRACKS; ++dtIdx) {
                            if (!drumTracks[dtIdx].muted && drumTracks[dtIdx].steps[m_seqStep]) {
                                triggerDrumTrack(dtIdx, (m_seqStep % 4 == 0) ? 1.0f : 0.85f);
                            }
                        }
                    }

                    // Trigger Arpeggiator / 16-Step Sequencer
                    int rootKey = m_heldKeys.empty() ? 60 : m_heldKeys.front();
                    const auto& stSpec = arpSteps[m_seqStep];

                    if (arpMode == 5) { // Programmable 16-Step Sequencer
                        if (stSpec.gate) {
                            int targetNote = rootKey + stSpec.semitone + stSpec.octave * 12;
                            triggerVoiceInternal(targetNote, stSpec.velocity, m_currentStepSlide);
                            m_arpNoteActive = true;
                        }
                        m_currentStepSlide = stSpec.slide;
                    } else if (arpMode > 0 && !m_heldKeys.empty()) {
                        int octs = std::clamp(static_cast<int>(baseVals[P_ARP_OCTAVES]), 1, 4);
                        int totalNotes = static_cast<int>(m_heldKeys.size()) * octs;
                        if (totalNotes > 0 && stSpec.gate) {
                            if (arpMode == 1) m_arpIdx = (m_arpIdx + 1) % totalNotes;
                            else if (arpMode == 2) m_arpIdx = (m_arpIdx - 1 + totalNotes) % totalNotes;
                            else if (arpMode == 3) m_arpIdx = (m_arpIdx + 1) % totalNotes;
                            else m_arpIdx = std::rand() % totalNotes;

                            int kIdx = m_arpIdx % static_cast<int>(m_heldKeys.size());
                            int octShift = (m_arpIdx / static_cast<int>(m_heldKeys.size())) * 12 + stSpec.octave * 12;
                            triggerVoiceInternal(m_heldKeys[kIdx] + octShift + stSpec.semitone, stSpec.velocity, stSpec.slide);
                            m_arpNoteActive = true;
                            m_currentStepSlide = stSpec.slide;
                        }
                    }
                }
            } else {
                m_arpNoteActive = false;
            }

            // 2. Global LFOs & Mod Matrix
            float lfo1Val = evalLfo(m_lfo1Phase, m_lfo1Sh, static_cast<int>(baseVals[P_LFO1_WAVE]), baseVals[P_LFO1_RATE], dt);
            float lfo2Val = evalLfo(m_lfo2Phase, m_lfo2Sh, static_cast<int>(baseVals[P_LFO2_WAVE]), baseVals[P_LFO2_RATE], dt);

            float pitchLfoSemis = 0.0f, cutoffLfoMod = 0.0f, shapeLfoMod = 0.0f, ampLfoMod = 1.0f;
            auto applyLfoTarget = [&](int target, float val, float depth) {
                if (target == 1) pitchLfoSemis += val * depth * 2.0f;
                else if (target == 2) cutoffLfoMod += val * depth * 0.5f;
                else if (target == 3) shapeLfoMod  += val * depth * 0.45f;
                else if (target == 4) ampLfoMod    *= (1.0f - depth * 0.5f * (1.0f - val));
            };
            applyLfoTarget(static_cast<int>(baseVals[P_LFO1_TARGET]), lfo1Val, baseVals[P_LFO1_DEPTH]);
            applyLfoTarget(static_cast<int>(baseVals[P_LFO2_TARGET]), lfo2Val, baseVals[P_LFO2_DEPTH]);

            float modOffsets[PARAM_COUNT] = {0.0f};
            for (const auto& slot : modMatrix) {
                if (!slot.enabled || std::fabs(slot.amount) < 0.001f) continue;
                float srcVal = 0.0f;
                if (slot.source == 1) srcVal = lfo1Val;
                else if (slot.source == 2) srcVal = lfo2Val;
                else if (slot.source == 7) srcVal = baseVals[P_BELLOWS_PRESSURE];
                else if (slot.source == 8) srcVal = m_modWheel;
                if (slot.destParam >= 0 && slot.destParam < PARAM_COUNT) {
                    float span = params[slot.destParam].maxVal - params[slot.destParam].minVal;
                    modOffsets[slot.destParam] += srcVal * slot.amount * span;
                }
            }

            // Live Audio-In Pass-Through Sample (if enabled)
            float audioInThru = 0.0f;
            if (baseVals[P_SMP_THRU_MON] > 0.01f) {
                audioInThru = m_audioInRing[m_audioInRead] * baseVals[P_SMP_THRU_MON];
                m_audioInRead = (m_audioInRead + 1) & (SCOPE_SIZE - 1);
            }


            float voiceSum = 0.0f;
            for (auto& v : m_voices) {
                if (!v.active) continue;

                float ampEnv = stepAdsr(v.ampStage, v.ampLevel, v.held,
                                        baseVals[P_AMP_A], baseVals[P_AMP_D], baseVals[P_AMP_S], baseVals[P_AMP_R], dt);
                float fltEnv = stepAdsr(v.fltStage, v.fltLevel, v.held,
                                        baseVals[P_FLT_A], baseVals[P_FLT_D], baseVals[P_FLT_S], baseVals[P_FLT_R], dt);
                float modEnv = stepAdsr(v.modStage, v.modLevel, v.held,
                                        baseVals[P_MOD_A], baseVals[P_MOD_D], baseVals[P_MOD_S], baseVals[P_MOD_R], dt);

                if (v.ampStage == 0) {
                    v.active = false;
                    continue;
                }

                float effGlide = v.slideActive ? std::max(0.18f, baseVals[P_PORTAMENTO]) : baseVals[P_PORTAMENTO];
                float glideRate = (effGlide <= 0.005f) ? 1.0f : std::min(1.0f, dt / (effGlide * 0.35f));
                v.curNote += (static_cast<float>(v.midiNote) - v.curNote) * glideRate;

                float pitchMod = m_pitchBendSemis + pitchLfoSemis + modEnv * baseVals[P_MOD_PITCH_AMT];
                float note1 = v.curNote + baseVals[P_OSC1_COARSE] + baseVals[P_OSC1_FINE] * 0.01f + pitchMod;
                float note2 = v.curNote + baseVals[P_OSC2_COARSE] + baseVals[P_OSC2_FINE] * 0.01f + pitchMod;

                float freq1 = 440.0f * std::pow(2.0f, (note1 - 69.0f) * (1.0f / 12.0f));
                float freq2 = 440.0f * std::pow(2.0f, (note2 - 69.0f) * (1.0f / 12.0f));

                float shape1 = std::clamp(baseVals[P_OSC1_SHAPE] + shapeLfoMod + modOffsets[P_OSC1_SHAPE], 0.0f, 1.0f);
                float timbre1 = std::clamp(baseVals[P_OSC1_TIMBRE] + modOffsets[P_OSC1_TIMBRE], 0.0f, 1.0f);

                float osc1 = renderOscillator(v, 0, static_cast<int>(baseVals[P_OSC1_TYPE]),
                                              static_cast<int>(baseVals[P_OSC1_WAVE]),
                                              freq1, shape1, timbre1, dt);
                float osc2 = renderOscillator(v, 1, static_cast<int>(baseVals[P_OSC2_TYPE]),
                                              0, freq2, baseVals[P_OSC2_SHAPE], 0.5f, dt);

                v.subPhase += (freq1 * 0.5f) * dt;
                v.subPhase -= std::floor(v.subPhase);
                float subSig = (v.subPhase < 0.5f ? 1.0f : -1.0f) * 0.5f + 0.5f * lut.sinUnit(v.subPhase);

                // 8-Bit Sampler Voice Playback
                float smpSig = 0.0f;
                int smpMode = static_cast<int>(baseVals[P_SMP_MODE]);
                if (smpMode > 0 && baseVals[P_SMP_LEVEL] > 0.01f && !samplerData.empty()) {
                    smpSig = render8BitSamplerVoice(v, smpMode, baseVals);
                }

                float rawMix = osc1 * baseVals[P_OSC1_LEVEL]
                               + osc2 * baseVals[P_OSC2_LEVEL]
                               + (osc1 * osc2) * baseVals[P_RING_MOD]
                               + subSig * baseVals[P_SUB_LEVEL]
                               + nextWhiteNoise() * baseVals[P_NOISE_LEVEL]
                               + smpSig * baseVals[P_SMP_LEVEL]
                               + audioInThru;

                float cutoffNorm = std::clamp(baseVals[P_FILT_CUTOFF] + cutoffLfoMod
                                                  + fltEnv * baseVals[P_FILT_ENV_AMT]
                                                  + ((v.curNote - 60.0f) / 48.0f) * baseVals[P_FILT_KEYTRK]
                                                  + modOffsets[P_FILT_CUTOFF], 0.01f, 0.99f);
                float cutoffHz = 30.0f * std::pow(320.0f, cutoffNorm);
                float reso = std::clamp(baseVals[P_FILT_RESO] + modOffsets[P_FILT_RESO], 0.0f, 0.96f);
                float drive = 1.0f + baseVals[P_FILT_DRIVE] * 4.0f;

                float filtered = processVoiceFilter(v, static_cast<int>(baseVals[P_FILT_TYPE]),
                                                    rawMix * drive, cutoffHz, reso);

                float bellowsExp = 0.25f + 0.75f * std::clamp(baseVals[P_BELLOWS_PRESSURE], 0.0f, 1.0f);
                voiceSum += filtered * ampEnv * v.velocity * bellowsExp * ampLfoMod * 0.32f;

                v.t += dt;
                if (!v.held) v.trel += dt;
            }

            // If no synth keys are held but Audio-In Thru Monitor is turned up, still pass it through
            if (voiceSum == 0.0f && std::fabs(audioInThru) > 0.001f) {
                voiceSum = audioInThru * 0.5f;
            }

       
            m_cabinetFx.enabled = (baseVals[P_CABINET_TF_EN] > 0.5f);
            if (m_cabinetFx.enabled) {
                voiceSum = m_cabinetFx.process(voiceSum);
            }

            // Render 6-Track Drum Machine Bus
            float drumBus = renderDrumMachineSample(dt, baseVals[P_DRUM_CRUSH]) * baseVals[P_DRUM_LEVEL];

            // Master Effects Rack & Output Limiter
            float fxSig = processMasterEffects(voiceSum + drumBus, baseVals, dt);
            fxSig *= baseVals[P_MASTER_VOL];
            float outSample = fastTanhClip(fxSig);
            outMono[f] = outSample;

            if ((f & 1) == 0) {
                m_scopeRing[m_scopeWriteIdx] = outSample;
                m_scopeWriteIdx = (m_scopeWriteIdx + 1) & (SCOPE_SIZE - 1);
            }
        }
    }


    // EXPORT TO SELF-CONTAINED C++ HEADER (`synth_config.h`)

    std::string generateCppHeader() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "// AUTO-GENERATED BY ESP32-S3 VISUAL SYNTH STUDIO (Qt6 SDK)\n";
        ss << "// Includes: Off-Screen GPIO/MIDI Notes, 8-Bit Sample Data, Drum & Arp Patterns\n";
        ss << "#ifndef SYNTH_CONFIG_H\n";
        ss << "#define SYNTH_CONFIG_H\n\n";
        ss << "#include <cstdint>\n\n";
        ss << "namespace GeneratedSynthConfig {\n\n";
        ss << "static constexpr const char* SYNTH_NAME = \"" << synthName << "\";\n";
        ss << "static constexpr int DEFAULT_I2S_BCLK = " << boardPins.i2sBclk << ";\n";
        ss << "static constexpr int DEFAULT_I2S_LRCK = " << boardPins.i2sLrck << ";\n";
        ss << "static constexpr int DEFAULT_I2S_DOUT = " << boardPins.i2sDout << ";\n";
        ss << "static constexpr int DEFAULT_I2C_SDA  = " << boardPins.i2cSda << ";\n";
        ss << "static constexpr int DEFAULT_I2C_SCL  = " << boardPins.i2cScl << ";\n";
        ss << "static constexpr int DEFAULT_AUDIO_MODE = " << boardPins.audioOutMode << ";\n\n";

        ss << "static constexpr const char* CUSTOM_OSC1_AST = \"" << escapeCString(customOsc1Formula) << "\";\n";
        ss << "static constexpr const char* CUSTOM_CABINET_TF = \"" << escapeCString(customCabinetFormula) << "\";\n\n";

        // Parameter Table + Hardware / MIDI / GPIO Notes
        ss << "struct CompiledParam {\n";
        ss << "    int id;\n    const char* name;\n    float minVal, maxVal, defaultVal;\n";
        ss << "    int midiCc, gpioMode, gpioPinA, gpioPinB;\n    const char* hardwareNote;\n};\n\n";

        ss << "static constexpr CompiledParam PARAMS[" << PARAM_COUNT << "] = {\n";
        for (int i = 0; i < PARAM_COUNT; ++i) {
            const auto& p = params[i];
            ss << "    {" << p.id << ", \"" << p.shortName << "\", "
               << p.minVal << "f, " << p.maxVal << "f, " << p.curVal << "f, "
               << p.midiCc << ", " << p.gpioMode << ", " << p.gpioPinA << ", " << p.gpioPinB << ", \""
               << escapeCString(p.hardwareNote) << "\"}"
               << (i + 1 < PARAM_COUNT ? "," : "")
               << (p.hardwareNote.empty() ? "\n" : (" // " + escapeCString(p.hardwareNote) + "\n"));
        }
        ss << "};\n\n";

        // Arpeggiator / 16-Step Sequencer Pattern
        ss << "struct CompiledArpStep { bool gate; int semitone; int octave; float velocity; bool slide; };\n";
        ss << "static constexpr CompiledArpStep ARP_STEPS[16] = {\n";
        for (int i = 0; i < 16; ++i) {
            const auto& st = arpSteps[i];
            ss << "    {" << (st.gate ? "true" : "false") << ", " << st.semitone << ", "
               << st.octave << ", " << st.velocity << "f, " << (st.slide ? "true" : "false") << "}"
               << (i < 15 ? ",\n" : "\n");
        }
        ss << "};\n\n";

        // 6-Track Drum Machine Pattern & Settings
        ss << "struct CompiledDrumTrack { bool muted; float level, tune, decay; uint16_t stepMask; };\n";
        ss << "static constexpr CompiledDrumTrack DRUM_TRACKS[" << NUM_DRUM_TRACKS << "] = {\n";
        for (int t = 0; t < NUM_DRUM_TRACKS; ++t) {
            const auto& dt = drumTracks[t];
            uint16_t mask = 0;
            for (int s = 0; s < 16; ++s) if (dt.steps[s]) mask |= (1u << s);
            ss << "    {" << (dt.muted ? "true" : "false") << ", "
               << dt.level << "f, " << dt.tune << "f, " << dt.decay << "f, 0x"
               << std::hex << std::uppercase << mask << std::dec << "}"
               << (t + 1 < NUM_DRUM_TRACKS ? ",\n" : "\n");
        }
        ss << "};\n\n";


	// 8-Slot Modulation Matrix
        ss << "struct CompiledModSlot { bool enabled; int source; int destParam; float amount; };\n";
        ss << "static constexpr CompiledModSlot MOD_MATRIX[8] = {\n";
        for (int s = 0; s < 8; ++s) {
            const auto& m = modMatrix[s];
            ss << "    {" << (m.enabled ? "true" : "false") << ", "
               << m.source << ", " << m.destParam << ", " << m.amount << "f}"
               << (s < 7 ? ",\n" : "\n");
        }
        ss << "};\n\n";



        // 8-Bit Signed PCM Sampler Data
        size_t smpLen = std::min<size_t>(samplerData.size(), MAX_SAMPLE_BYTES);
        ss << "static constexpr int SAMPLER_8BIT_LEN = " << smpLen << ";\n";
        ss << "static const int8_t SAMPLER_8BIT_DATA[" << std::max<size_t>(1, smpLen) << "] = {\n    ";
        for (size_t i = 0; i < smpLen; ++i) {
            ss << static_cast<int>(samplerData[i]) << (i + 1 < smpLen ? "," : "");
            if ((i + 1) % 24 == 0) ss << "\n    ";
        }
        if (smpLen == 0) ss << "0";
        ss << "\n};\n\n";

        // 5. Screen Widgets per Profile
        ss << "struct CompiledWidget {\n";
        ss << "    int type;\n    const char* label;\n    int x, y, w, h;\n";
        ss << "    int paramId1, paramId2, midiNote;\n    uint16_t bgCol565, fgCol565, textCol565;\n};\n\n";

        const char* profNames[3] = {"WIDGETS_800x480", "WIDGETS_320x240", "WIDGETS_128x64"};
        for (int prof = 0; prof < 3; ++prof) {
            const auto& wList = profileWidgets[prof];
            ss << "static constexpr int NUM_" << profNames[prof] << " = " << wList.size() << ";\n";
            ss << "static constexpr CompiledWidget " << profNames[prof] << "[" << std::max<size_t>(1, wList.size()) << "] = {\n";
            for (size_t i = 0; i < wList.size(); ++i) {
                const auto& w = wList[i];
                ss << "    {" << static_cast<int>(w.type) << ", \"" << escapeCString(w.label) << "\", "
                   << w.x << ", " << w.y << ", " << w.w << ", " << w.h << ", "
                   << w.paramId1 << ", " << w.paramId2 << ", " << w.midiNote << ", "
                   << "0x" << std::hex << std::uppercase << w.bgCol565 << ", "
                   << "0x" << w.fgCol565 << ", 0x" << w.textCol565 << std::dec << "}"
                   << (i + 1 < wList.size() ? "," : "") << "\n";
            }
            if (wList.empty()) {
                ss << "    {0, \"\", 0, 0, 0, 0, 0, 0, 60, 0, 0, 0}\n";
            }
            ss << "};\n\n";
        }

        ss << "} // namespace GeneratedSynthConfig\n";
        ss << "#endif // SYNTH_CONFIG_H\n";
        return ss.str();
    }

    void loadPresetTemplate(int templateIdx) {
        std::lock_guard<std::mutex> lock(m_mutex);
        initDefaultParams();
        if (templateIdx == 0) {
            synthName = "Chicago_M1_House_Workstation";
            params[P_OSC1_TYPE].curVal   = 1.0f; // M1 Organ
            params[P_OSC1_SHAPE].curVal  = 0.78f;
            params[P_OSC1_TIMBRE].curVal = 0.85f;
            params[P_FILT_TYPE].curVal   = 3.0f; // 24dB Ladder
            params[P_FILT_CUTOFF].curVal = 0.68f;
            params[P_FILT_RESO].curVal   = 0.35f;
            params[P_DRUM_ENABLE].curVal = 0.0f;
            params[P_SMP_LEVEL].curVal   = 0.35f;
        } else if (templateIdx == 1) {
            synthName = "Vintage_1970s_Analog_Lead";
            params[P_OSC1_TYPE].curVal   = 0.0f; // VA Saw
            params[P_OSC2_TYPE].curVal   = 0.0f;
            params[P_OSC2_FINE].curVal   = 9.0f;
            params[P_OSC2_LEVEL].curVal  = 0.75f;
            params[P_FILT_TYPE].curVal   = 3.0f; // 24dB Moog Ladder
            params[P_FILT_CUTOFF].curVal = 0.58f;
            params[P_FILT_RESO].curVal   = 0.52f;
            params[P_FILT_DRIVE].curVal  = 0.35f;
            params[P_PORTAMENTO].curVal  = 0.15f;
        } else if (templateIdx == 2) {
            synthName = "Amiga_8Bit_Sampler_Groovebox";
            params[P_OSC1_LEVEL].curVal  = 0.25f;
            params[P_SMP_LEVEL].curVal   = 0.95f;
            params[P_SMP_MODE].curVal    = 2.0f; // Loop Sustain
            params[P_SMP_SRATE_DIV].curVal = 2.0f;
            params[P_DRUM_ENABLE].curVal = 1.0f;
            params[P_ARP_MODE].curVal    = 5.0f; // 16-Step Sequencer
        } else if (templateIdx == 3) {
            synthName = "303_Sequencer_Rig";
            params[P_OSC1_TYPE].curVal   = 0.0f;
            params[P_FILT_TYPE].curVal   = 4.0f; 
            params[P_FILT_CUTOFF].curVal = 0.42f;
            params[P_FILT_RESO].curVal   = 0.82f;
            params[P_FILT_ENV_AMT].curVal= 0.75f;
            params[P_ARP_MODE].curVal    = 5.0f;
            params[P_DRUM_ENABLE].curVal = 1.0f;
            params[P_FX_DRIVE_AMT].curVal= 0.40f;
        }
        buildDefaultLayouts();
    }

    void buildDefaultLayouts() {
        profileWidgets[0] = {
            {1,  W_CYCLE_SELECT,   "OSC1 ENGINE",   10,  42, 126, 34, P_OSC1_TYPE,   0, 60, 0x18E3, 0x073F, 0xFFFF},
            {2,  W_CYCLE_SELECT,   "FILTER TYPE",   10,  80, 126, 34, P_FILT_TYPE,   0, 60, 0x18E3, 0x07E0, 0xFFFF},
            {3,  W_ROTARY_KNOB,    "OSC1 SHAPE",   142,  42,  68, 72, P_OSC1_SHAPE,  0, 60, 0x18E3, 0x073F, 0xFFFF},
            {4,  W_ROTARY_KNOB,    "8-BIT SMP",    214,  42,  68, 72, P_SMP_LEVEL,   0, 60, 0x18E3, 0xFD20, 0xFFFF},
            {5,  W_XY_PAD,         "CUTOFF / RESO",288,  42, 148, 118, P_FILT_CUTOFF, P_FILT_RESO, 60, 0x0861, 0x07E0, 0xFFFF},
            {6,  W_SAMPLER_WAVE,   "8-BIT SAMPLE", 442,  42, 170, 118, P_SMP_START,   P_SMP_END,   60, 0x0861, 0xFD20, 0xFFFF},
            {7,  W_OSCILLOSCOPE,   "LIVE SCOPE",   618,  42, 172, 118, P_MASTER_VOL,  0, 60, 0x0000, 0x073F, 0xFFFF},

            {8,  W_ADSR_GRAPH,     "AMP ADSR",      10, 120, 130,  76, P_AMP_A,       0, 60, 0x0861, 0xFFE0, 0xFFFF},
            {9,  W_ADSR_GRAPH,     "FILTER ADSR",  146, 120, 136,  76, P_FLT_A,       1, 60, 0x0861, 0xF81F, 0xFFFF},

            {10, W_TOGGLE_BTN,     "DRUMS RUN",     10, 202,  92,  34, P_DRUM_ENABLE, 0, 60, 0x18E3, 0x07E0, 0xFFFF},
            {11, W_CYCLE_SELECT,   "ARP / SEQ",    108, 202, 110,  34, P_ARP_MODE,    0, 60, 0x18E3, 0xFFE0, 0xFFFF},
            {12, W_ROTARY_KNOB,    "TEMPO BPM",    224, 166,  58,  70, P_ARP_BPM,     0, 60, 0x18E3, 0xFFE0, 0xFFFF},
            {13, W_DRUM_GRID,      "6x16 DRUMS",   288, 166, 260, 126, P_DRUM_LEVEL,  0, 60, 0x0861, 0x073F, 0xFFFF},
            {14, W_STEP_SEQ_GRID,  "16-STEP ARP",   10, 240, 272,  52, P_ARP_BPM,     0, 60, 0x0861, 0xFFE0, 0xFFFF},

            {15, W_VERT_FADER,     "DRIVE",        554, 166,  54, 126, P_FX_DRIVE_AMT,0, 60, 0x18E3, 0xF800, 0xFFFF},
            {16, W_VERT_FADER,     "CHORUS",       614, 166,  54, 126, P_FX_CHORUS_MIX,0,60, 0x18E3, 0x073F, 0xFFFF},
            {17, W_VERT_FADER,     "DELAY",        674, 166,  54, 126, P_FX_DELAY_MIX,0, 60, 0x18E3, 0xFFE0, 0xFFFF},
            {18, W_VERT_FADER,     "REVERB",       734, 166,  56, 126, P_FX_REVERB_MIX,0,60, 0x18E3, 0xF81F, 0xFFFF},

            {19, W_TOUCH_RIBBON,   "EXPRESSION RIBBON", 10, 298, 780, 24, P_BELLOWS_PRESSURE, 0, 60, 0x0861, 0xFD20, 0xFFFF},
            {20, W_PIANO_KEYBOARD, "37-KEY TOUCH PIANO",10, 328, 780, 144, P_OSC1_COARSE, 0, 48, 0x18E3, 0x073F, 0xFFFF}
        };

        profileWidgets[1] = {
            {101, W_CYCLE_SELECT, "OSC1",      4,  26,  98, 24, P_OSC1_TYPE,    0, 60, 0x18E3, 0x073F, 0xFFFF},
            {102, W_TOGGLE_BTN,   "DRUMS",   106,  26,  98, 24, P_DRUM_ENABLE,  0, 60, 0x18E3, 0x07E0, 0xFFFF},
            {103, W_OSCILLOSCOPE, "SCOPE",   208,  26, 108, 52, P_MASTER_VOL,   0, 60, 0x0000, 0x073F, 0xFFFF},
            {104, W_ROTARY_KNOB,  "CUTOFF",    4,  54,  64, 64, P_FILT_CUTOFF,  0, 60, 0x18E3, 0x07E0, 0xFFFF},
            {105, W_ROTARY_KNOB,  "RESO",     72,  54,  64, 64, P_FILT_RESO,    0, 60, 0x18E3, 0x07E0, 0xFFFF},
            {106, W_ROTARY_KNOB,  "8BIT SMP",140,  54,  64, 64, P_SMP_LEVEL,    0, 60, 0x18E3, 0xFD20, 0xFFFF},
            {107, W_XY_PAD,       "XY MORPH",208,  82, 108, 74, P_FILT_CUTOFF,  P_OSC1_TIMBRE, 60, 0x0861, 0xFD20, 0xFFFF},
            {108, W_DRUM_GRID,    "DRUMS",     4, 122, 198, 60, P_DRUM_LEVEL,   0, 60, 0x0861, 0x073F, 0xFFFF},
            {109, W_MOMENTARY_PAD,"PAD C3",    4, 186,  48, 48, P_OSC1_TYPE,    0, 60, 0x18E3, 0x073F, 0xFFFF},
            {110, W_MOMENTARY_PAD,"PAD Eb3",  56, 186,  48, 48, P_OSC1_TYPE,    0, 63, 0x18E3, 0x073F, 0xFFFF},
            {111, W_MOMENTARY_PAD,"PAD G3",  108, 186,  48, 48, P_OSC1_TYPE,    0, 67, 0x18E3, 0x073F, 0xFFFF},
            {112, W_CYCLE_SELECT, "ARP",     160, 186,  48, 48, P_ARP_MODE,     0, 60, 0x18E3, 0xFFE0, 0xFFFF},
            {113, W_ROTARY_KNOB,  "DELAY",   212, 162,  50, 72, P_FX_DELAY_MIX, 0, 60, 0x18E3, 0xFFE0, 0xFFFF},
            {114, W_ROTARY_KNOB,  "REVERB",  266, 162,  50, 72, P_FX_REVERB_MIX,0, 60, 0x18E3, 0xF81F, 0xFFFF}
        };

        profileWidgets[2] = {
            {201, W_CYCLE_SELECT, "OSC",    2, 14, 60, 16, P_OSC1_TYPE,   0, 60, 0x0000, 0xFFFF, 0xFFFF},
            {202, W_OSCILLOSCOPE, "WAVE",  64, 14, 62, 26, P_MASTER_VOL,  0, 60, 0x0000, 0xFFFF, 0xFFFF},
            {203, W_HORIZ_SLIDER, "CUT",    2, 33, 60, 13, P_FILT_CUTOFF, 0, 60, 0x0000, 0xFFFF, 0xFFFF},
            {204, W_HORIZ_SLIDER, "RES",    2, 48, 60, 13, P_FILT_RESO,   0, 60, 0x0000, 0xFFFF, 0xFFFF},
            {205, W_TOGGLE_BTN,   "DRUM",  64, 44, 62, 17, P_DRUM_ENABLE, 0, 60, 0x0000, 0xFFFF, 0xFFFF}
        };
    }

private:
    struct SynthVoice {
        bool active = false;
        bool held = false;
        bool slideActive = false;
        int midiNote = 60;
        float curNote = 60.0f;
        float velocity = 0.8f;
        float t = 0.0f;
        float trel = 0.0f;

        int ampStage = 0, fltStage = 0, modStage = 0;
        float ampLevel = 0.0f, fltLevel = 0.0f, modLevel = 0.0f;

        float phase[2] = {0.0f, 0.0f};
        float auxPhase[2][7] = {{0.0f}};
        float fmPrev[2] = {0.0f, 0.0f};
        float subPhase = 0.0f;
        float astIntegrators[16] = {0.0f};
        float astLastOut = 0.0f;

        // 8-Bit Sampler voice playback head
        float smpPos = 0.0f;
        int   smpDir = 1;
        int   smpHoldCount = 0;
        float smpHeldVal = 0.0f;

        float ksBuf[512] = {0.0f};
        int ksIdx = 0;
        bool ksExcited = false;

        float svfLow = 0.0f, svfBand = 0.0f;
        float ladderStage[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct DrumVoiceState {
        bool active = false;
        float t = 0.0f;
        float phase = 0.0f;
        float phase2 = 0.0f;
        float vel = 1.0f;
    };

    std::mutex m_mutex;
    float m_srate = 22050.0f;
    std::array<SynthVoice, MAX_VOICES> m_voices;
    std::array<DrumVoiceState, NUM_DRUM_TRACKS> m_drumStates;
    std::vector<int> m_heldKeys;

    std::unique_ptr<XpressiveParser::Node> m_customAst;
    TransferFunctionFX m_cabinetFx;

    float m_lfo1Phase = 0.0f, m_lfo2Phase = 0.0f;
    float m_lfo1Sh = 0.0f, m_lfo2Sh = 0.0f;
    float m_pitchBendSemis = 0.0f;
    float m_modWheel = 0.0f;

    float m_arpTimer = 0.0f;
    int m_arpIdx = 0;
    int m_seqStep = 0;
    bool m_arpNoteActive = false;
    bool m_currentStepSlide = false;

    float m_audioInRing[SCOPE_SIZE] = {0.0f};
    int   m_audioInWrite = 0, m_audioInRead = 0;

    float m_crushHold = 0.0f;
    int   m_crushCounter = 0;
    float m_phaserPhase = 0.0f;
    float m_phaserAp[4] = {0.0f};
    float m_chorusPhase = 0.0f;
    std::vector<float> m_chorusBuf;
    int   m_chorusWrite = 0;
    std::vector<float> m_delayBuf;
    int   m_delayWrite = 0;
    static constexpr int m_revLengths[4] = {743, 887, 1013, 1153};
    static constexpr int m_apLengths[2]  = {179, 263};
    std::vector<float> m_revCombs[4];
    int m_revIdx[4] = {0};
    std::vector<float> m_revAp[2];
    int m_apIdx[2] = {0};
    float m_eqLowState = 0.0f, m_eqHighState = 0.0f;

    float m_scopeRing[SCOPE_SIZE] = {0.0f};
    int   m_scopeWriteIdx = 0;
    uint32_t m_rng = 0x9E3779B9u;

    static float fastTanhClip(float x) {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    float nextWhiteNoise() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return static_cast<float>(static_cast<int32_t>(m_rng)) * (1.0f / 2147483648.0f);
    }

    static std::string escapeCString(const std::string& in) {
        std::string out;
        for (char c : in) {
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\\"";
            else if (c != '\r' && c != '\n') out += c;
        }
        return out;
    }

    void triggerVoiceInternal(int midiNote, float velNorm, bool slideFromPrev) {
        SynthVoice* slot = &m_voices[0];
        if (slideFromPrev) {
            for (auto& v : m_voices) {
                if (v.active && v.held) { slot = &v; break; }
            }
        } else {
            for (auto& v : m_voices) {
                if (v.active && v.midiNote == midiNote) { slot = &v; break; }
                if (!v.active) { slot = &v; break; }
            }
        }
        bool wasActive = slot->active;
        slot->active = true;
        slot->held = true;
        slot->slideActive = slideFromPrev;
        slot->midiNote = std::clamp(midiNote, 12, 108);
        if (!wasActive || (!slideFromPrev && params[P_PORTAMENTO].curVal <= 0.005f)) {
            slot->curNote = static_cast<float>(slot->midiNote);
        }
        slot->velocity = std::clamp(velNorm, 0.1f, 1.0f);
        if (!slideFromPrev || !wasActive) {
            slot->t = 0.0f;
            slot->trel = 0.0f;
            slot->ampStage = 1;
            slot->fltStage = 1;
            slot->modStage = 1;
            slot->ksExcited = false;
            float startFrac = std::clamp(params[P_SMP_START].curVal, 0.0f, 0.95f);
            slot->smpPos = startFrac * static_cast<float>(samplerData.size());
            slot->smpDir = 1;
            std::memset(slot->astIntegrators, 0, sizeof(slot->astIntegrators));
        }
    }

    float render8BitSamplerVoice(SynthVoice& v, int smpMode, const float* baseVals) {
        if (samplerData.empty()) return 0.0f;
        int totalLen = static_cast<int>(samplerData.size());
        int startIdx = std::clamp(static_cast<int>(baseVals[P_SMP_START] * totalLen), 0, totalLen - 2);
        int endIdx   = std::clamp(static_cast<int>(baseVals[P_SMP_END]   * totalLen), startIdx + 1, totalLen - 1);

        int div = std::clamp(static_cast<int>(baseVals[P_SMP_SRATE_DIV]), 1, 8);
        if (++v.smpHoldCount >= div) {
            v.smpHoldCount = 0;
            int idx = std::clamp(static_cast<int>(v.smpPos), 0, totalLen - 1);
            v.smpHeldVal = static_cast<float>(samplerData[idx]) * (1.0f / 128.0f);
        }

        float semiOffset = (v.curNote - 60.0f) + baseVals[P_SMP_PITCH];
        float rate = std::pow(2.0f, semiOffset * (1.0f / 12.0f));
        v.smpPos += rate * static_cast<float>(v.smpDir);

        if (smpMode == 1) { // One-Shot
            if (v.smpPos >= endIdx) return 0.0f;
        } else if (smpMode == 2) { // Forward Loop
            if (v.smpPos >= endIdx) v.smpPos = static_cast<float>(startIdx);
        } else if (smpMode == 3) { // Ping-Pong Loop
            if (v.smpPos >= endIdx) { v.smpPos = static_cast<float>(endIdx); v.smpDir = -1; }
            else if (v.smpPos <= startIdx) { v.smpPos = static_cast<float>(startIdx); v.smpDir = 1; }
        }
        return v.smpHeldVal;
    }

    float renderDrumMachineSample(float dt, float crushAmt) {
        const auto& lut = XpressiveParser::lut();
        float sum = 0.0f;

        for (int t = 0; t < NUM_DRUM_TRACKS; ++t) {
            auto& ds = m_drumStates[t];
            if (!ds.active) continue;
            const auto& spec = drumTracks[t];
            float tuneMul = 0.6f + 0.8f * spec.tune;
            float decMul  = 0.35f + 1.3f * spec.decay;
            float sig = 0.0f;

            switch (t) {
            case 0: { // 808/909 Analog Kick
                float env = std::exp(-ds.t * (7.5f / decMul));
                if (env < 0.002f) { ds.active = false; break; }
                float freq = (52.0f + 160.0f * std::exp(-ds.t * 42.0f)) * tuneMul;
                ds.phase += freq * dt;
                ds.phase -= std::floor(ds.phase);
                float click = (ds.t < 0.006f) ? nextWhiteNoise() * 0.35f : 0.0f;
                sig = (lut.sinUnit(ds.phase) + click) * env;
                break;
            }
            case 1: { // Crispy Snare
                float toneEnv = std::exp(-ds.t * 24.0f);
                float noiseEnv = std::exp(-ds.t * (13.0f / decMul));
                if (noiseEnv < 0.002f) { ds.active = false; break; }
                ds.phase += (185.0f * tuneMul) * dt;
                ds.phase -= std::floor(ds.phase);
                sig = 0.45f * lut.sinUnit(ds.phase) * toneEnv + 0.65f * nextWhiteNoise() * noiseEnv;
                break;
            }
            case 2: { // House Clap
                float env = (ds.t < 0.030f) ? (0.6f + 0.4f * lut.sinUnit(ds.t * 110.0f))
                                            : std::exp(-(ds.t - 0.030f) * (15.0f / decMul));
                if (env < 0.002f) { ds.active = false; break; }
                sig = nextWhiteNoise() * env * 0.8f;
                break;
            }
            case 3:   // Closed Hi-Hat
            case 4: { // Open Hi-Hat
                float rate = (t == 3) ? (42.0f / decMul) : (7.5f / decMul);
                float env = std::exp(-ds.t * rate);
                if (env < 0.002f) { ds.active = false; break; }
                ds.phase  += (3120.0f * tuneMul) * dt; ds.phase -= std::floor(ds.phase);
                ds.phase2 += (4780.0f * tuneMul) * dt; ds.phase2 -= std::floor(ds.phase2);
                float metal = ((ds.phase < 0.5f ? 1.0f : -1.0f) + (ds.phase2 < 0.5f ? 1.0f : -1.0f)) * 0.5f;
                sig = (0.45f * metal + 0.55f * nextWhiteNoise()) * env * 0.7f;
                break;
            }
            case 5: { // FM Tom / Perc
                float env = std::exp(-ds.t * (14.0f / decMul));
                if (env < 0.002f) { ds.active = false; break; }
                float f0 = (140.0f + 90.0f * std::exp(-ds.t * 25.0f)) * tuneMul;
                ds.phase  += f0 * dt; ds.phase -= std::floor(ds.phase);
                ds.phase2 += (f0 * 2.41f) * dt; ds.phase2 -= std::floor(ds.phase2);
                sig = lut.sinUnit(ds.phase + 1.8f * env * lut.sinUnit(ds.phase2)) * env;
                break;
            }
            }
            ds.t += dt;
            sum += sig * spec.level * ds.vel * 0.45f;
        }

        if (crushAmt > 0.02f) {
            float levels = std::pow(2.0f, 8.0f - crushAmt * 5.0f);
            sum = std::round(sum * levels) / levels;
        }
        return sum;
    }

    static float stepAdsr(int& stage, float& level, bool held,
                          float aSec, float dSec, float sLev, float rSec, float dt) {
        if (stage == 0) return 0.0f;
        if (!held && stage != 4) stage = 4;

        if (stage == 1) {
            level += dt / std::max(0.002f, aSec);
            if (level >= 1.0f) { level = 1.0f; stage = 2; }
        } else if (stage == 2) {
            level -= (level - sLev) * std::min(1.0f, (dt / std::max(0.004f, dSec)) * 4.0f);
            if (std::fabs(level - sLev) < 0.005f) { level = sLev; stage = 3; }
        } else if (stage == 3) {
            level = sLev;
        } else if (stage == 4) {
            level -= level * std::min(1.0f, (dt / std::max(0.005f, rSec)) * 4.5f);
            if (level < 0.001f) { level = 0.0f; stage = 0; }
        }
        return level;
    }

    float evalLfo(float& phase, float& shVal, int wave, float rateHz, float dt) {
        phase += rateHz * dt;
        if (phase >= 1.0f) {
            phase -= std::floor(phase);
            shVal = nextWhiteNoise();
        }
        const auto& lut = XpressiveParser::lut();
        switch (wave) {
        case 0: return lut.sinUnit(phase);
        case 1: return 1.0f - 4.0f * std::fabs(phase - 0.5f);
        case 2: return 2.0f * phase - 1.0f;
        case 3: return (phase < 0.5f) ? 1.0f : -1.0f;
        case 4: return shVal;
        default: return 0.7f * lut.sinUnit(phase) + 0.3f * shVal;
        }
    }

    float renderOscillator(SynthVoice& v, int oscIdx, int type, int subWave,
                           float freq, float shape, float timbre, float dt) {
        const auto& lut = XpressiveParser::lut();
        float& p = v.phase[oscIdx];
        p += freq * dt;
        p -= std::floor(p);

        switch (type) {
        case 0: { // Virtual Analog
            float saw = 2.0f * p - 1.0f;
            float pw  = 0.1f + 0.8f * shape;
            float pls = (p < pw) ? 1.0f : -1.0f;
            float tri = 1.0f - 4.0f * std::fabs(p - 0.5f);
            if (subWave == 1) return pls;
            if (subWave == 2) return tri;
            return (1.0f - timbre) * saw + timbre * pls;
        }
        case 1: { // Chicago House M1 Organ
            float p5th = v.auxPhase[oscIdx][0] + (freq * 1.498307f) * dt;
            p5th -= std::floor(p5th);
            v.auxPhase[oscIdx][0] = p5th;
            float root = lut.sinUnit(p) + 0.35f * lut.sinUnit(2.0f * p);
            float fifth = lut.sinUnit(p5th) + 0.25f * lut.sinUnit(2.0f * p5th);
            float superOct = lut.sinUnit(4.0f * p);
            float clickEnv = (v.t < 0.018f) ? (1.0f - v.t / 0.018f) : 0.0f;
            return 0.55f * root + (0.20f + 0.55f * shape) * fifth + 0.25f * superOct + clickEnv * timbre * lut.sinUnit(6.0f * p);
        }
        case 2: { // Harmonium Multi-Reed
            float pCel = v.auxPhase[oscIdx][0] + (freq * 1.00463f) * dt;
            pCel -= std::floor(pCel);
            v.auxPhase[oscIdx][0] = pCel;
            float r1 = lut.sinUnit(p) - 0.5f * lut.sinUnit(2.0f * p) + 0.33f * lut.sinUnit(3.0f * p);
            float r2 = lut.sinUnit(pCel) - 0.5f * lut.sinUnit(2.0f * pCel) + 0.33f * lut.sinUnit(3.0f * pCel);
            float rSub = lut.sinUnit(0.5f * p) - 0.4f * lut.sinUnit(p);
            return 0.5f * r1 + (0.25f + 0.45f * shape) * r2 + (0.25f + 0.4f * timbre) * rSub;
        }
        case 3: { // 4-Op FM
            float ratio = 1.0f + std::floor(timbre * 7.0f);
            float modIdx = shape * 6.5f;
            float mPhase = v.auxPhase[oscIdx][0] + (freq * ratio) * dt;
            mPhase -= std::floor(mPhase);
            v.auxPhase[oscIdx][0] = mPhase;
            float modSig = lut.sinUnit(mPhase + 0.15f * v.fmPrev[oscIdx]);
            v.fmPrev[oscIdx] = modSig;
            return lut.sinUnit(p + modIdx * modSig);
        }
        case 4: { // 7-Voice Supersaw
            static const float detunes[7] = {-0.032f, -0.019f, -0.008f, 0.0f, 0.009f, 0.021f, 0.034f};
            float spread = 0.15f + 0.85f * shape;
            float sum = 0.0f;
            for (int u = 0; u < 7; ++u) {
                float up = v.auxPhase[oscIdx][u] + freq * (1.0f + detunes[u] * spread) * dt;
                up -= std::floor(up);
                v.auxPhase[oscIdx][u] = up;
                sum += (2.0f * up - 1.0f);
            }
            return sum * 0.24f;
        }
        case 5: { // Karplus-Strong Plucked String
            int delaySamples = std::clamp(static_cast<int>(m_srate / std::max(40.0f, freq)), 8, 500);
            if (!v.ksExcited) {
                for (int i = 0; i < delaySamples; ++i) v.ksBuf[i] = nextWhiteNoise();
                v.ksExcited = true;
                v.ksIdx = 0;
            }
            int nextIdx = (v.ksIdx + 1) % delaySamples;
            float cur = v.ksBuf[v.ksIdx];
            v.ksBuf[v.ksIdx] = (0.975f + 0.023f * timbre) * (0.5f * (cur + v.ksBuf[nextIdx]));
            v.ksIdx = nextIdx;
            return cur;
        }
        case 6: { // Formant Vocal
            float f1 = 300.0f + 500.0f * (1.0f - shape);
            float f2 = 900.0f + 1300.0f * shape;
            float win = (1.0f - p) * (1.0f - p);
            return win * (0.65f * lut.sinUnit((f1 / freq) * p) + 0.45f * lut.sinUnit((f2 / freq) * p));
        }
        case 7: { // Additive Drawbars
            return 0.45f * lut.sinUnit(p) + 0.30f * shape * lut.sinUnit(2.0f * p)
                   + 0.25f * timbre * lut.sinUnit(3.0f * p) + 0.20f * shape * lut.sinUnit(4.0f * p);
        }
        case 8: { // Chiptune + Noise
            float qP = std::floor(p * 8.0f) / 8.0f;
            float chip = (qP < (0.125f + 0.75f * shape)) ? 0.8f : -0.8f;
            return (1.0f - timbre * 0.5f) * chip + (timbre * 0.5f) * nextWhiteNoise();
        }
        case 9: { // Custom Xpressive AST
            if (!m_customAst) return lut.sinUnit(p);
            XpressiveParser::Env env;
            env.t = v.t; env.tempo = params[P_ARP_BPM].curVal; env.srate = m_srate; env.f = freq;
            env.rel = v.held ? 0.0f : 1.0f; env.trel = v.trel;
            env.A1 = shape; env.A2 = timbre; env.A3 = params[P_BELLOWS_PRESSURE].curVal;
            env.lastOut = v.astLastOut; env.intState = v.astIntegrators; env.numIntegrators = 16;
            float out = std::clamp(m_customAst->eval(env), -1.5f, 1.5f);
            v.astLastOut = out;
            return out;
        }
        default: return lut.sinUnit(p);
        }
    }

    float processVoiceFilter(SynthVoice& v, int filtType, float in, float cutoffHz, float reso) {
        cutoffHz = std::clamp(cutoffHz, 30.0f, m_srate * 0.44f);
        float f = std::clamp(2.0f * std::sin(M_PI_F * cutoffHz / m_srate), 0.005f, 0.92f);
        float q = 1.0f - std::clamp(reso, 0.0f, 0.94f);

        if (filtType <= 2) {
            v.svfLow += f * v.svfBand;
            float high = in - v.svfLow - q * v.svfBand;
            v.svfBand += f * high;
            v.svfLow  = std::clamp(v.svfLow, -4.0f, 4.0f);
            v.svfBand = std::clamp(v.svfBand, -4.0f, 4.0f);
            if (filtType == 1) return v.svfBand;
            if (filtType == 2) return high;
            return v.svfLow;
        }
        if (filtType == 3 || filtType == 4) {
            float fb = (filtType == 4 ? 4.3f : 3.8f) * reso;
            float x = fastTanhClip(in - fb * v.ladderStage[3]);
            for (int s = 0; s < 4; ++s) {
                v.ladderStage[s] += f * (x - v.ladderStage[s]);
                x = v.ladderStage[s];
            }
            return v.ladderStage[3];
        }
        v.svfLow += f * v.svfBand;
        float high = in - v.svfLow - (q * 0.5f) * v.svfBand;
        v.svfBand += f * high;
        return 0.6f * v.svfLow + 0.4f * v.svfBand;
    }

    float processMasterEffects(float x, const float* p, float dt) {
        const auto& lut = XpressiveParser::lut();
        if (p[P_FX_DRIVE_AMT] > 0.01f) {
            x = fastTanhClip(x * (1.0f + p[P_FX_DRIVE_AMT] * 7.0f));
        }
        if (p[P_FX_CRUSH_BITS] > 0.02f || p[P_FX_CRUSH_RATE] > 0.02f) {
            int downsample = 1 + static_cast<int>(p[P_FX_CRUSH_RATE] * 24.0f);
            if (++m_crushCounter >= downsample) {
                m_crushCounter = 0;
                float levels = std::pow(2.0f, 12.0f - p[P_FX_CRUSH_BITS] * 9.5f);
                m_crushHold = std::round(x * levels) / levels;
            }
            x = m_crushHold;
        }
        if (p[P_FX_PHASER_MIX] > 0.01f) {
            m_phaserPhase += p[P_FX_PHASER_RATE] * dt;
            m_phaserPhase -= std::floor(m_phaserPhase);
            float coef = 0.25f + 0.60f * (0.5f + 0.5f * lut.sinUnit(m_phaserPhase));
            float y = x;
            for (int s = 0; s < 4; ++s) {
                float tmp = -coef * y + m_phaserAp[s];
                m_phaserAp[s] = y + coef * tmp;
                y = tmp;
            }
            x = x * (1.0f - 0.5f * p[P_FX_PHASER_MIX]) + y * (0.5f * p[P_FX_PHASER_MIX]);
        }
        if (!m_chorusBuf.empty() && (p[P_FX_CHORUS_MIX] > 0.01f || p[P_FX_FLANGER_MIX] > 0.01f)) {
            int bufMask = static_cast<int>(m_chorusBuf.size()) - 1;
            m_chorusBuf[m_chorusWrite] = x;
            m_chorusPhase += 0.85f * dt;
            m_chorusPhase -= std::floor(m_chorusPhase);
            int rIdx = (m_chorusWrite - static_cast<int>(120.0f + 95.0f * p[P_FX_CHORUS_DEPTH] * lut.sinUnit(m_chorusPhase))) & bufMask;
            int fIdx = (m_chorusWrite - static_cast<int>(18.0f + 15.0f * lut.sinUnit(m_chorusPhase * 0.5f))) & bufMask;
            m_chorusWrite = (m_chorusWrite + 1) & bufMask;
            x += m_chorusBuf[rIdx] * p[P_FX_CHORUS_MIX] * 0.55f + m_chorusBuf[fIdx] * p[P_FX_FLANGER_MIX] * 0.55f;
        }
        if (p[P_FX_SUB_BOOST] > 0.01f) x += m_eqLowState * p[P_FX_SUB_BOOST] * 0.65f;

        m_eqLowState += 0.08f * (x - m_eqLowState);
        m_eqHighState += 0.45f * (x - m_eqHighState);
        x = m_eqLowState * std::pow(10.0f, p[P_FX_EQ_LOW] * 0.05f)
            + (m_eqHighState - m_eqLowState) * std::pow(10.0f, p[P_FX_EQ_MID] * 0.05f)
            + (x - m_eqHighState) * std::pow(10.0f, p[P_FX_EQ_HIGH] * 0.05f);

        if (!m_delayBuf.empty() && p[P_FX_DELAY_MIX] > 0.01f) {
            int maxDel = static_cast<int>(m_delayBuf.size());
            int delSamples = std::clamp(static_cast<int>(p[P_FX_DELAY_TIME] * m_srate), 256, maxDel - 1);
            int rIdx = (m_delayWrite - delSamples + maxDel) % maxDel;
            float delayed = m_delayBuf[rIdx];
            m_delayBuf[m_delayWrite] = std::clamp(x + delayed * p[P_FX_DELAY_FB], -2.0f, 2.0f);
            m_delayWrite = (m_delayWrite + 1) % maxDel;
            x += delayed * p[P_FX_DELAY_MIX];
        }
        if (p[P_FX_REVERB_MIX] > 0.01f) {
            float fb = 0.65f + 0.28f * p[P_FX_REVERB_SIZE];
            float combSum = 0.0f;
            for (int c = 0; c < 4; ++c) {
                float outC = m_revCombs[c][m_revIdx[c]];
                m_revCombs[c][m_revIdx[c]] = x * 0.25f + outC * fb;
                if (++m_revIdx[c] >= m_revLengths[c]) m_revIdx[c] = 0;
                combSum += outC;
            }
            for (int a = 0; a < 2; ++a) {
                float bufOut = m_revAp[a][m_apIdx[a]];
                float inVal = combSum + bufOut * 0.5f;
                m_revAp[a][m_apIdx[a]] = inVal;
                combSum = bufOut - inVal * 0.5f;
                if (++m_apIdx[a] >= m_apLengths[a]) m_apIdx[a] = 0;
            }
            x = x * (1.0f - 0.35f * p[P_FX_REVERB_MIX]) + combSum * p[P_FX_REVERB_MIX];
        }
        return x;
    }

    void initDefaultParams() {
        auto reg = [&](ParamId id, const char* name, const char* shortN, const char* cat,
                       float minV, float maxV, float defV, int steps = 0, int cc = -1, const char* note = "") {
            params[id] = {id, name, shortN, cat, minV, maxV, defV, defV, steps, cc, GPIO_NONE, -1, -1, note};
        };

        reg(P_OSC1_TYPE,   "Osc 1 Engine (VA/M1/Harm/FM/Saw/KP/Vow/Add/Chip/AST)", "Osc1Type", "Oscillator 1", 0, 9, 1, 9, 14);
        reg(P_OSC1_WAVE,   "Osc 1 Sub-Waveform",     "Osc1Wave",  "Oscillator 1", 0, 2, 0, 2, 15);
        reg(P_OSC1_COARSE, "Osc 1 Coarse Semitones", "Osc1Semi",  "Oscillator 1", -24, 24, 0, 48, 16);
        reg(P_OSC1_FINE,   "Osc 1 Fine Detune (c)",  "Osc1Fine",  "Oscillator 1", -100, 100, 0, 0, 17);
        reg(P_OSC1_SHAPE,  "Osc 1 Shape / PWM / FM", "Osc1Shape", "Oscillator 1", 0, 1, 0.65f, 0, 18);
        reg(P_OSC1_TIMBRE, "Osc 1 Timbre / 5th Mix", "Osc1Timb",  "Oscillator 1", 0, 1, 0.55f, 0, 19);
        reg(P_OSC1_LEVEL,  "Osc 1 Mix Level",        "Osc1Lvl",   "Oscillator 1", 0, 1, 0.85f, 0, 20);

        reg(P_OSC2_TYPE,   "Osc 2 Engine",           "Osc2Type",  "Oscillator 2", 0, 9, 0, 9, 21);
        reg(P_OSC2_COARSE, "Osc 2 Coarse Semitones", "Osc2Semi",  "Oscillator 2", -24, 24, -12, 48, 22);
        reg(P_OSC2_FINE,   "Osc 2 Fine Detune (c)",  "Osc2Fine",  "Oscillator 2", -100, 100, 7, 0, 23);
        reg(P_OSC2_SHAPE,  "Osc 2 Shape",            "Osc2Shape", "Oscillator 2", 0, 1, 0.5f, 0, 24);
        reg(P_OSC2_LEVEL,  "Osc 2 Mix Level",        "Osc2Lvl",   "Oscillator 2", 0, 1, 0.35f, 0, 25);
        reg(P_SUB_LEVEL,   "Sub Oscillator (-1 Oct)","SubLvl",    "Oscillator 2", 0, 1, 0.25f, 0, 26);
        reg(P_NOISE_LEVEL, "White Noise Generator",  "NoiseLvl",  "Oscillator 2", 0, 1, 0.0f, 0, 27);
        reg(P_RING_MOD,    "Ring Modulator (1x2)",   "RingMod",   "Oscillator 2", 0, 1, 0.0f, 0, 28);

        reg(P_SMP_LEVEL,     "8-Bit Sampler Mix Level",     "SmpLvl",    "8-Bit Sampler", 0, 1, 0.35f, 0, 29, "8-bit PCM voice level");
        reg(P_SMP_MODE,      "Sampler Mode (0=Off,1=1Shot,2=Loop,3=PingPong)", "SmpMode", "8-Bit Sampler", 0, 3, 2, 3, 30);
        reg(P_SMP_PITCH,     "Sampler Pitch Semitones",     "SmpPitch",  "8-Bit Sampler", -24, 24, 0, 48, 31);
        reg(P_SMP_START,     "Sample Start Point",          "SmpStart",  "8-Bit Sampler", 0.0f, 0.95f, 0.0f, 0, 32);
        reg(P_SMP_END,       "Sample Loop / End Point",     "SmpEnd",    "8-Bit Sampler", 0.05f, 1.0f, 1.0f, 0, 33);
        reg(P_SMP_SRATE_DIV, "8-Bit Clock Divider (Crunch)","SmpCrush",  "8-Bit Sampler", 1, 8, 2, 7, 34);
        reg(P_SMP_IN_GAIN,   "Audio-In Mic/Line Preamp",    "AudInGain", "8-Bit Sampler", 0.1f, 4.0f, 1.2f, 0, -1, "Preamp gain for Audio-In");
        reg(P_SMP_THRU_MON,  "Live Audio-In Filter Thru",   "AudInThru", "8-Bit Sampler", 0, 1, 0.0f, 0, -1, "Routes external Audio-In into VCF+FX");
        reg(P_SMP_ADC_PIN,   "ESP32 Audio-In ADC GPIO Pin", "AudInPin",  "8-Bit Sampler", -1, 18, -1, 19, -1, "Bias Audio-In to 1.65V on ADC1 GPIO");

        reg(P_FILT_TYPE,   "Filter Topology (LP/BP/HP/Lad24/303/Form)", "FltType", "Filter", 0, 5, 3, 5, 70);
        reg(P_FILT_CUTOFF, "Filter Cutoff Frequency","Cutoff",    "Filter", 0, 1, 0.68f, 0, 74, "Main Panel Cutoff Knob / CC74");
        reg(P_FILT_RESO,   "Filter Resonance (Q)",   "Resonance", "Filter", 0, 0.96f, 0.35f, 0, 71, "Main Panel Resonance Knob / CC71");
        reg(P_FILT_DRIVE,  "Filter Input Drive",     "FltDrive",  "Filter", 0, 1, 0.15f, 0, 75);
        reg(P_FILT_ENV_AMT,"Filter Envelope Amount", "FltEnvAmt", "Filter", -1, 1, 0.42f, 0, 76);
        reg(P_FILT_KEYTRK, "Filter Keyboard Track",  "FltKeyTrk", "Filter", 0, 1, 0.50f, 0, 77);
        reg(P_CABINET_TF_EN,"Wooden Cabinet Body TF","CabinetFX", "Filter", 0, 1, 0.0f, 1, 78);

        reg(P_AMP_A, "Amp Attack (s)",  "AmpAtk", "Envelopes", 0.002f, 2.5f, 0.01f, 0, 73);
        reg(P_AMP_D, "Amp Decay (s)",   "AmpDec", "Envelopes", 0.01f,  3.0f, 0.35f, 0, 75);
        reg(P_AMP_S, "Amp Sustain",     "AmpSus", "Envelopes", 0.0f,   1.0f, 0.70f, 0, 79);
        reg(P_AMP_R, "Amp Release (s)", "AmpRel", "Envelopes", 0.01f,  4.0f, 0.22f, 0, 72);

        reg(P_FLT_A, "Filter Attack",   "FltAtk", "Envelopes", 0.002f, 2.5f, 0.01f, 0, 80);
        reg(P_FLT_D, "Filter Decay",    "FltDec", "Envelopes", 0.01f,  3.0f, 0.28f, 0, 81);
        reg(P_FLT_S, "Filter Sustain",  "FltSus", "Envelopes", 0.0f,   1.0f, 0.25f, 0, 82);
        reg(P_FLT_R, "Filter Release",  "FltRel", "Envelopes", 0.01f,  4.0f, 0.20f, 0, 83);

        reg(P_MOD_A, "Mod Env Attack",  "ModAtk", "Envelopes", 0.002f, 2.0f, 0.01f);
        reg(P_MOD_D, "Mod Env Decay",   "ModDec", "Envelopes", 0.01f,  2.0f, 0.18f);
        reg(P_MOD_S, "Mod Env Sustain", "ModSus", "Envelopes", 0.0f,   1.0f, 0.0f);
        reg(P_MOD_R, "Mod Env Release", "ModRel", "Envelopes", 0.01f,  2.0f, 0.12f);
        reg(P_MOD_PITCH_AMT, "Mod Env Pitch Semis", "ModPitch", "Envelopes", -24, 24, 0);

        reg(P_LFO1_WAVE,   "LFO 1 Waveform",     "Lfo1Wave", "LFO & Expression", 0, 5, 0, 5);
        reg(P_LFO1_RATE,   "LFO 1 Rate (Hz)",    "Lfo1Rate", "LFO & Expression", 0.1f, 25.0f, 4.5f, 0, 3);
        reg(P_LFO1_DEPTH,  "LFO 1 Depth",        "Lfo1Dep",  "LFO & Expression", 0, 1, 0.0f, 0, 1, "Mod Wheel / Vibrato Depth");
        reg(P_LFO1_TARGET, "LFO 1 Target",       "Lfo1Tgt",  "LFO & Expression", 0, 4, 2, 4);

        reg(P_LFO2_WAVE,   "LFO 2 Waveform",     "Lfo2Wave", "LFO & Expression", 0, 5, 1, 5);
        reg(P_LFO2_RATE,   "LFO 2 Rate (Hz)",    "Lfo2Rate", "LFO & Expression", 0.1f, 25.0f, 2.0f);
        reg(P_LFO2_DEPTH,  "LFO 2 Depth",        "Lfo2Dep",  "LFO & Expression", 0, 1, 0.0f);
        reg(P_LFO2_TARGET, "LFO 2 Target",       "Lfo2Tgt",  "LFO & Expression", 0, 4, 3, 4);

        reg(P_BELLOWS_PRESSURE, "Bellows / Expression Sensor", "Bellows", "LFO & Expression", 0, 1, 0.95f, 0, 11, "Expression pedal or Bellows ADC");
        reg(P_PORTAMENTO,       "Portamento Glide Time",       "Glide",   "LFO & Expression", 0, 1, 0.0f,  0, 5);

        reg(P_ARP_MODE,    "Arp / Step Seq Mode (0=Off..5=Seq)", "ArpMode", "Arp & Drums", 0, 5, 0, 5);
        reg(P_ARP_BPM,     "Master Tempo (BPM)", "BPM",      "Arp & Drums", 40, 240, 125, 200);
        reg(P_ARP_GATE,    "Arp Gate Length",    "ArpGate",  "Arp & Drums", 0.1f, 0.98f, 0.65f);
        reg(P_ARP_OCTAVES, "Arp Octave Range",   "ArpOct",   "Arp & Drums", 1, 4, 2, 3);
        reg(P_ARP_SWING,   "16th Swing / Shuffle","Swing",   "Arp & Drums", 0.0f, 0.65f, 0.12f);

        reg(P_DRUM_ENABLE, "Drum Machine Run/Stop","DrumRun","Arp & Drums", 0, 1, 0.0f, 1, 102, "External Foot Switch / Start Button");
        reg(P_DRUM_LEVEL,  "Drum Bus Master Level","DrumLvl","Arp & Drums", 0, 1.2f, 0.85f, 0, 103);
        reg(P_DRUM_CRUSH,  "Drum Bus 8-Bit Crunch","DrumBit","Arp & Drums", 0, 1.0f, 0.0f,  0, 104);

        reg(P_FX_DRIVE_AMT,  "Tube Overdrive / Fold", "Overdrive", "Master FX", 0, 1, 0.05f, 0, 90);
        reg(P_FX_CRUSH_BITS, "Bitcrusher Depth",      "Bitcrush",  "Master FX", 0, 1, 0.0f);
        reg(P_FX_CRUSH_RATE, "Sample Rate Decimate",  "Decimate",  "Master FX", 0, 1, 0.0f);
        reg(P_FX_PHASER_MIX, "4-Stage Phaser Mix",    "PhaserMix", "Master FX", 0, 1, 0.0f);
        reg(P_FX_PHASER_RATE,"Phaser LFO Rate",       "PhaserRate","Master FX", 0.1f, 8.0f, 0.8f);
        reg(P_FX_FLANGER_MIX,"Flanger Mix",           "Flanger",   "Master FX", 0, 1, 0.0f);
        reg(P_FX_CHORUS_MIX, "BBD Ensemble Chorus",   "ChorusMix", "Master FX", 0, 1, 0.30f, 0, 93);
        reg(P_FX_CHORUS_DEPTH,"Chorus Mod Depth",     "ChorusDep", "Master FX", 0, 1, 0.55f);
        reg(P_FX_DELAY_MIX,  "Tape Delay Mix",        "DelayMix",  "Master FX", 0, 1, 0.20f, 0, 94);
        reg(P_FX_DELAY_TIME, "Delay Time (sec)",      "DelayTime", "Master FX", 0.05f, 0.75f, 0.32f);
        reg(P_FX_DELAY_FB,   "Delay Feedback",        "DelayFB",   "Master FX", 0, 0.90f, 0.45f);
        reg(P_FX_REVERB_MIX, "Hall Reverb Mix",       "ReverbMix", "Master FX", 0, 1, 0.25f, 0, 91);
        reg(P_FX_REVERB_SIZE,"Reverb Room Size",      "ReverbSz",  "Master FX", 0.1f, 0.96f, 0.72f);
        reg(P_FX_SUB_BOOST,  "Sub-Bass Enhancer",     "SubBoost",  "Master FX", 0, 1, 0.15f);
        reg(P_FX_EQ_LOW,     "EQ Low Shelf (dB)",     "EQLow",     "Master FX", -12, 12, 1.5f);
        reg(P_FX_EQ_MID,     "EQ Mid Peak (dB)",      "EQMid",     "Master FX", -12, 12, 0.0f);
        reg(P_FX_EQ_HIGH,    "EQ High Shelf (dB)",    "EQHigh",    "Master FX", -12, 12, 1.0f);
        reg(P_MASTER_VOL,    "Master Output Volume",  "MasterVol", "Master FX", 0, 1.2f, 0.85f, 0, 7, "Master Volume Potentiometer / CC7");
    }
};

} // namespace SynthSDK

#endif // SYNTH_SDK_MODULES_H