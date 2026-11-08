#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include "display_driver.h"
#include "usb_midi_host.h"
#include "usb_audio.h"
#include "wifi_stream.h"
#include "synth_sdk_modules.h"
#include "synth_config.h"

#define AUDIO_SRATE 22050

static Waveshare7Display g_display;
static UsbMidiHost g_usbMidi;
static UsbAudioOut g_usbAudio;
static SynthSDK::ModularSynthEngine g_engine;

static int g_audioOutMode = GeneratedSynthConfig::DEFAULT_AUDIO_MODE;
static int g_activeTouchWidget = -1;
static int g_dragStartY = 0;
static float g_dragStartNorm = 0.5f;
static int g_heldPianoMidi = -1;
static bool g_wasTouching = false;
static int g_lastDrawnSeqStep = -1;

struct GpioRuntimeState {
    bool lastBtnState = true;
    uint8_t lastEncAB = 0;
    float smoothedAdc = 0.5f;
};
static GpioRuntimeState g_gpioState[SynthSDK::PARAM_COUNT];

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}


// LOAD GENERATED HEADER INTO RUNTIME ENGINE & SETUP ESP32 GPIO PINS

static void loadCompiledConfigIntoEngine() {
    using namespace GeneratedSynthConfig;
    g_engine.synthName = SYNTH_NAME;
    g_engine.customOsc1Formula = CUSTOM_OSC1_AST;
    g_engine.customCabinetFormula = CUSTOM_CABINET_TF;

    g_engine.boardPins.i2sBclk = DEFAULT_I2S_BCLK;
    g_engine.boardPins.i2sLrck = DEFAULT_I2S_LRCK;
    g_engine.boardPins.i2sDout = DEFAULT_I2S_DOUT;
    g_engine.boardPins.i2cSda  = DEFAULT_I2C_SDA;
    g_engine.boardPins.i2cScl  = DEFAULT_I2C_SCL;
    g_engine.boardPins.audioOutMode = DEFAULT_AUDIO_MODE;

    const int paramCount = std::min<int>( sizeof(PARAMS) / sizeof(PARAMS[0]), SynthSDK::PARAM_COUNT );
    for (int i = 0; i < paramCount; ++i) {
        const auto& cp = PARAMS[i];
        if (cp.id < 0 || cp.id >= SynthSDK::PARAM_COUNT) continue;
        auto& ep = g_engine.params[cp.id];
        ep.minVal       = cp.minVal;
        ep.maxVal       = cp.maxVal;
        ep.defaultVal   = cp.defaultVal;
        ep.curVal       = cp.defaultVal;
        ep.midiCc       = cp.midiCc;
        ep.gpioMode     = cp.gpioMode;
        ep.gpioPinA     = cp.gpioPinA;
        ep.gpioPinB     = cp.gpioPinB;
        ep.hardwareNote = cp.hardwareNote ? cp.hardwareNote : "";

        if (ep.gpioPinA >= 0) {
            if (ep.gpioMode == SynthSDK::GPIO_ADC_POT || ep.gpioMode == SynthSDK::GPIO_BELLOWS_HALL) {
                pinMode(ep.gpioPinA, INPUT);
            } else if (ep.gpioMode == SynthSDK::GPIO_DIGITAL_BTN_TOG ||
                       ep.gpioMode == SynthSDK::GPIO_DIGITAL_BTN_MOM ||
                       ep.gpioMode == SynthSDK::GPIO_ROTARY_ENC) {
                pinMode(ep.gpioPinA, INPUT_PULLUP);
                if (ep.gpioMode == SynthSDK::GPIO_ROTARY_ENC && ep.gpioPinB >= 0) {
                    pinMode(ep.gpioPinB, INPUT_PULLUP);
                }
            }
        }
    }

    int adcInPin = static_cast<int>(g_engine.params[SynthSDK::P_SMP_ADC_PIN].curVal);
    if (adcInPin >= 0) {
        pinMode(adcInPin, INPUT);
    }

    // Load 16-Step Arpeggiator / Sequencer Pattern
    for (int i = 0; i < 16; ++i) {
        g_engine.arpSteps[i].gate     = ARP_STEPS[i].gate;
        g_engine.arpSteps[i].semitone = ARP_STEPS[i].semitone;
        g_engine.arpSteps[i].octave   = ARP_STEPS[i].octave;
        g_engine.arpSteps[i].velocity = ARP_STEPS[i].velocity;
        g_engine.arpSteps[i].slide    = ARP_STEPS[i].slide;
    }

    // Load 6-Track Drum Machine Pattern & Settings
    for (int t = 0; t < SynthSDK::ModularSynthEngine::NUM_DRUM_TRACKS; ++t) {
        g_engine.drumTracks[t].muted = DRUM_TRACKS[t].muted;
        g_engine.drumTracks[t].level = DRUM_TRACKS[t].level;
        g_engine.drumTracks[t].tune  = DRUM_TRACKS[t].tune;
        g_engine.drumTracks[t].decay = DRUM_TRACKS[t].decay;
        for (int s = 0; s < 16; ++s) {
            g_engine.drumTracks[t].steps[s] = (DRUM_TRACKS[t].stepMask & (1u << s)) != 0;
        }
    }

    // Load 8-Slot Modulation Matrix
    for (int s = 0; s < 8; ++s) {
        g_engine.modMatrix[s].enabled   = MOD_MATRIX[s].enabled;
        g_engine.modMatrix[s].source    = MOD_MATRIX[s].source;
        g_engine.modMatrix[s].destParam = MOD_MATRIX[s].destParam;
        g_engine.modMatrix[s].amount    = MOD_MATRIX[s].amount;
    }

    // Load 8-Bit Signed PCM Sampler Data
    if (SAMPLER_8BIT_LEN > 0) {
        g_engine.samplerData.assign(SAMPLER_8BIT_DATA, SAMPLER_8BIT_DATA + SAMPLER_8BIT_LEN);
    }

    // Load Screen Widgets for all 3 Profiles
    auto loadWidArray = [](std::vector<SynthSDK::UiWidgetSpec>& dst, const CompiledWidget* src, int count) {
        dst.clear();
        for (int i = 0; i < count; ++i) {
            if (!src[i].label || src[i].w <= 0 || src[i].h <= 0) continue;
            SynthSDK::UiWidgetSpec w;
            w.uid        = i + 1;
            w.type       = static_cast<SynthSDK::WidgetType>(src[i].type);
            w.label      = src[i].label;
            w.x          = src[i].x;
            w.y          = src[i].y;
            w.w          = src[i].w;
            w.h          = src[i].h;
            w.paramId1   = src[i].paramId1;
            w.paramId2   = src[i].paramId2;
            w.midiNote   = src[i].midiNote;
            w.bgCol565   = src[i].bgCol565;
            w.fgCol565   = src[i].fgCol565;
            w.textCol565 = src[i].textCol565;
            dst.push_back(w);
        }
    };

    loadWidArray(g_engine.profileWidgets[0], WIDGETS_800x480, NUM_WIDGETS_800x480);
    loadWidArray(g_engine.profileWidgets[1], WIDGETS_320x240, NUM_WIDGETS_320x240);
    loadWidArray(g_engine.profileWidgets[2], WIDGETS_128x64,  NUM_WIDGETS_128x64);

    g_engine.recompileCustomAst();
}


// HARDWARE GPIO POLLING (ADC POTS, BUTTONS, ROTARY ENCODERS)

static bool pollHardwareGpioControls() {
    bool anyChanged = false;
    for (int i = 0; i < SynthSDK::PARAM_COUNT; ++i) {
        auto& p = g_engine.params[i];
        if (p.gpioMode == SynthSDK::GPIO_NONE || p.gpioPinA < 0) continue;
        auto& st = g_gpioState[i];

        if (p.gpioMode == SynthSDK::GPIO_ADC_POT || p.gpioMode == SynthSDK::GPIO_BELLOWS_HALL) {
            float raw = std::clamp(analogRead(p.gpioPinA) / 4095.0f, 0.0f, 1.0f);
            st.smoothedAdc = st.smoothedAdc * 0.85f + raw * 0.15f;
            if (std::fabs(st.smoothedAdc - p.getNormalized()) > 0.015f) {
                p.setNormalized(st.smoothedAdc);
                anyChanged = true;
            }
        } else if (p.gpioMode == SynthSDK::GPIO_DIGITAL_BTN_TOG) {
            bool cur = digitalRead(p.gpioPinA);
            if (!cur && st.lastBtnState) {
                p.setNormalized(p.getNormalized() > 0.5f ? 0.0f : 1.0f);
                anyChanged = true;
            }
            st.lastBtnState = cur;
        } else if (p.gpioMode == SynthSDK::GPIO_DIGITAL_BTN_MOM) {
            bool pressed = (digitalRead(p.gpioPinA) == LOW);
            float target = pressed ? 1.0f : 0.0f;
            if (std::fabs(p.getNormalized() - target) > 0.1f) {
                p.setNormalized(target);
                anyChanged = true;
            }
        } else if (p.gpioMode == SynthSDK::GPIO_ROTARY_ENC && p.gpioPinB >= 0) {
            uint8_t a = digitalRead(p.gpioPinA) ? 1 : 0;
            uint8_t b = digitalRead(p.gpioPinB) ? 1 : 0;
            uint8_t ab = (a << 1) | b;
            if (ab != st.lastEncAB) {
                if ((st.lastEncAB == 0b00 && ab == 0b01) || (st.lastEncAB == 0b11 && ab == 0b10)) {
                    p.setNormalized(p.getNormalized() + 0.025f);
                    anyChanged = true;
                } else if ((st.lastEncAB == 0b00 && ab == 0b10) || (st.lastEncAB == 0b11 && ab == 0b01)) {
                    p.setNormalized(p.getNormalized() - 0.025f);
                    anyChanged = true;
                }
                st.lastEncAB = ab;
            }
        }
    }
    return anyChanged;
}


// LOVYANGFX WIDGET RENDERING FOR PHYSICAL ESP32 SCREEN (ALL 14 WIDGETS)

static inline bool isBlackMidi(int m) {
    int n = m % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

static void drawSingleWidget(const SynthSDK::UiWidgetSpec& w) {
    using namespace SynthSDK;
    const auto& p1 = g_engine.params[std::clamp(w.paramId1, 0, static_cast<int>(PARAM_COUNT) - 1)];
    const auto& p2 = g_engine.params[std::clamp(w.paramId2, 0, static_cast<int>(PARAM_COUNT) - 1)];
    float norm1 = p1.getNormalized();
    float norm2 = p2.getNormalized();

    if (w.type != W_PIANO_KEYBOARD) {
        g_display.fillRoundRect(w.x, w.y, w.w, w.h, 4, w.bgCol565);
        g_display.drawRoundRect(w.x, w.y, w.w, w.h, 4, w.fgCol565);
    }

    switch (w.type) {
        case W_ROTARY_KNOB: {
            int rad = std::min(w.w, w.h - 14) / 2 - 4;
            int cx = w.x + w.w / 2;
            int cy = w.y + (w.h - 12) / 2;
            g_display.drawCircle(cx, cy, rad, 0x4208);
            float angleRad = (225.0f - 270.0f * norm1) * M_PI_F / 180.0f;
            int lx = cx + static_cast<int>(std::cos(angleRad) * (rad - 2));
            int ly = cy - static_cast<int>(std::sin(angleRad) * (rad - 2));
            g_display.drawLine(cx, cy, lx, ly, w.fgCol565);
            g_display.fillCircle(cx, cy, 3, w.textCol565);
            g_display.setTextColor(w.textCol565, w.bgCol565);
            g_display.setTextDatum(BC_DATUM);
            g_display.drawString(w.label.c_str(), cx, w.y + w.h - 2);
            break;
        }
        case W_VERT_FADER: {
            int trackH = w.h - 22;
            int fillH = static_cast<int>(trackH * norm1);
            g_display.fillRect(w.x + w.w / 2 - 4, w.y + 6, 8, trackH, 0x0841);
            g_display.fillRect(w.x + w.w / 2 - 4, w.y + 6 + (trackH - fillH), 8, fillH, w.fgCol565);
            g_display.fillRect(w.x + 6, w.y + 4 + (trackH - fillH), w.w - 12, 5, w.textCol565);
            g_display.setTextColor(w.textCol565, w.bgCol565);
            g_display.setTextDatum(BC_DATUM);
            g_display.drawString(w.label.c_str(), w.x + w.w / 2, w.y + w.h - 2);
            break;
        }
        case W_HORIZ_SLIDER:
        case W_TOUCH_RIBBON: {
            int fillW = static_cast<int>((w.w - 6) * norm1);
            g_display.fillRect(w.x + 3, w.y + 3, fillW, w.h - 6, w.fgCol565);
            g_display.setTextColor(w.textCol565);
            g_display.setTextDatum(MC_DATUM);
            g_display.drawString(w.label.c_str(), w.x + w.w / 2, w.y + w.h / 2);
            break;
        }
        case W_TOGGLE_BTN:
        case W_MOMENTARY_PAD: {
            bool on = (w.type == W_TOGGLE_BTN) && (norm1 > 0.5f);
            if (on) g_display.fillRoundRect(w.x + 2, w.y + 2, w.w - 4, w.h - 4, 3, w.fgCol565);
            g_display.setTextColor(on ? 0x0000 : w.textCol565);
            g_display.setTextDatum(MC_DATUM);
            g_display.drawString(w.label.c_str(), w.x + w.w / 2, w.y + w.h / 2);
            break;
        }
        case W_CYCLE_SELECT: {
            char buf[44];
            if (w.paramId1 == P_OSC1_TYPE || w.paramId1 == P_OSC2_TYPE) {
                static const char* oscNames[] = {"VA", "M1Org", "Harm", "4OpFM", "SupSaw", "Karplus", "Vowel", "Add8", "Chip", "AST"};
                int idx = std::clamp(static_cast<int>(p1.curVal), 0, 9);
                std::snprintf(buf, sizeof(buf), "%s: %s", w.label.c_str(), oscNames[idx]);
            } else if (w.paramId1 == P_FILT_TYPE) {
                static const char* fltNames[] = {"LP12", "BP12", "HP12", "Lad24", "Acid303", "Formant"};
                int idx = std::clamp(static_cast<int>(p1.curVal), 0, 5);
                std::snprintf(buf, sizeof(buf), "%s: %s", w.label.c_str(), fltNames[idx]);
            } else if (w.paramId1 == P_ARP_MODE) {
                static const char* arpNames[] = {"OFF", "UP", "DOWN", "UP/DN", "RAND", "16-SEQ"};
                int idx = std::clamp(static_cast<int>(p1.curVal), 0, 5);
                std::snprintf(buf, sizeof(buf), "%s: %s", w.label.c_str(), arpNames[idx]);
            } else if (w.paramId1 == P_SMP_MODE) {
                static const char* smpNames[] = {"OFF", "1SHOT", "LOOP", "PINGPONG"};
                int idx = std::clamp(static_cast<int>(p1.curVal), 0, 3);
                std::snprintf(buf, sizeof(buf), "%s: %s", w.label.c_str(), smpNames[idx]);
            } else {
                std::snprintf(buf, sizeof(buf), "%s: %d", w.label.c_str(), static_cast<int>(p1.curVal));
            }
            g_display.setTextColor(w.textCol565, w.bgCol565);
            g_display.setTextDatum(MC_DATUM);
            g_display.drawString(buf, w.x + w.w / 2, w.y + w.h / 2);
            break;
        }
        case W_XY_PAD: {
            int cx = w.x + 6 + static_cast<int>((w.w - 12) * norm1);
            int cy = w.y + 6 + static_cast<int>((w.h - 12) * (1.0f - norm2));
            g_display.drawFastVLine(cx, w.y + 2, w.h - 4, 0x3186);
            g_display.drawFastHLine(w.x + 2, cy, w.w - 4, 0x3186);
            g_display.fillCircle(cx, cy, 4, w.fgCol565);
            g_display.setTextColor(w.textCol565);
            g_display.setTextDatum(TL_DATUM);
            g_display.drawString(w.label.c_str(), w.x + 4, w.y + 3);
            break;
        }
        case W_SAMPLER_WAVE: {
            const auto& sdata = g_engine.samplerData;
            if (!sdata.empty()) {
                int midY = w.y + w.h / 2 + 4;
                int prevX = w.x + 4, prevY = midY;
                int n = static_cast<int>(sdata.size());
                for (int dx = 4; dx < w.w - 4; dx += 2) {
                    int idx = std::clamp((dx - 4) * n / std::max(1, w.w - 8), 0, n - 1);
                    int sy = midY - (static_cast<int>(sdata[idx]) * (w.h * 35 / 100)) / 128;
                    if (dx > 4) g_display.drawLine(prevX, prevY, w.x + dx, sy, w.fgCol565);
                    prevX = w.x + dx; prevY = sy;
                }
            }
            int sx = w.x + 4 + static_cast<int>(g_engine.params[P_SMP_START].curVal * (w.w - 8));
            int ex = w.x + 4 + static_cast<int>(g_engine.params[P_SMP_END].curVal   * (w.w - 8));
            g_display.drawFastVLine(sx, w.y + 14, w.h - 17, 0x07E0);
            g_display.drawFastVLine(ex, w.y + 14, w.h - 17, 0xF800);
            g_display.setTextColor(w.textCol565);
            g_display.setTextDatum(TL_DATUM);
            g_display.drawString(w.label.c_str(), w.x + 4, w.y + 3);
            break;
        }
        case W_ADSR_GRAPH: {
            int baseId = (w.paramId2 == 1) ? P_FLT_A : (w.paramId2 == 2 ? P_MOD_A : P_AMP_A);
            float a = g_engine.params[baseId].getNormalized();
            float d = g_engine.params[baseId + 1].getNormalized();
            float s = g_engine.params[baseId + 2].getNormalized();
            float r = g_engine.params[baseId + 3].getNormalized();

            int x0 = w.x + 6, yBot = w.y + w.h - 6, yTop = w.y + 16;
            int availW = w.w - 12;
            int x1 = x0 + static_cast<int>(availW * 0.25f * a + 4);
            int x2 = x1 + static_cast<int>(availW * 0.25f * d + 4);
            int ySus = yBot - static_cast<int>((yBot - yTop) * s);
            int x3 = x2 + static_cast<int>(availW * 0.22f);
            int x4 = std::min(w.x + w.w - 6, x3 + static_cast<int>(availW * 0.25f * r + 4));

            g_display.drawLine(x0, yBot, x1, yTop, w.fgCol565);
            g_display.drawLine(x1, yTop, x2, ySus, w.fgCol565);
            g_display.drawLine(x2, ySus, x3, ySus, w.fgCol565);
            g_display.drawLine(x3, ySus, x4, yBot, w.fgCol565);
            g_display.setTextColor(w.textCol565);
            g_display.setTextDatum(TL_DATUM);
            g_display.drawString(w.label.c_str(), w.x + 5, w.y + 3);
            break;
        }
        case W_OSCILLOSCOPE: {
            float scope[ModularSynthEngine::SCOPE_SIZE];
            g_engine.getScopeSnapshot(scope);
            int midY = w.y + w.h / 2;
            int prevX = w.x + 2, prevY = midY;
            for (int i = 0; i < ModularSynthEngine::SCOPE_SIZE; i += 4) {
                int sx = w.x + 2 + (i * (w.w - 4)) / ModularSynthEngine::SCOPE_SIZE;
                int sy = std::clamp(midY - static_cast<int>(scope[i] * (w.h * 0.42f)), w.y + 2, w.y + w.h - 2);
                if (i > 0) g_display.drawLine(prevX, prevY, sx, sy, w.fgCol565);
                prevX = sx; prevY = sy;
            }
            break;
        }
        case W_STEP_SEQ_GRID: {
            int stepW = std::max(4, (w.w - 8) / 16);
            int curStep = g_engine.getCurrentSeqStep();
            for (int s = 0; s < 16; ++s) {
                int sx = w.x + 4 + s * stepW;
                bool gate = g_engine.arpSteps[s].gate;
                uint16_t col = (s == curStep) ? 0xFFE0 : (gate ? w.fgCol565 : 0x2124);
                int barH = std::max(6, ((g_engine.arpSteps[s].semitone + 24) * (w.h - 20)) / 48);
                g_display.fillRect(sx, w.y + w.h - 4 - barH, stepW - 2, barH, col);
            }
            g_display.setTextColor(w.textCol565);
            g_display.setTextDatum(TL_DATUM);
            g_display.drawString(w.label.c_str(), w.x + 4, w.y + 2);
            break;
        }
        case W_DRUM_GRID: {
            int stepW = std::max(4, (w.w - 8) / 16);
            int rowH  = std::max(4, (w.h - 18) / ModularSynthEngine::NUM_DRUM_TRACKS);
            int curStep = g_engine.getCurrentSeqStep();
            for (int r = 0; r < ModularSynthEngine::NUM_DRUM_TRACKS; ++r) {
                for (int s = 0; s < 16; ++s) {
                    bool on = g_engine.drumTracks[r].steps[s];
                    uint16_t col = on ? ((s == curStep) ? 0xFFE0 : w.fgCol565)
                                      : ((s == curStep) ? 0x4208 : 0x18E3);
                    g_display.fillRect(w.x + 4 + s * stepW, w.y + 14 + r * rowH, stepW - 2, rowH - 2, col);
                }
            }
            g_display.setTextColor(w.textCol565);
            g_display.setTextDatum(TL_DATUM);
            g_display.drawString(w.label.c_str(), w.x + 4, w.y + 2);
            break;
        }
        case W_PIANO_KEYBOARD: {
            const int numKeys = 37;
            const int startMidi = w.midiNote;
            int whiteCount = 0;
            for (int i = 0; i < numKeys; ++i) if (!isBlackMidi(startMidi + i)) whiteCount++;
            float whiteW = static_cast<float>(w.w) / whiteCount;
            int blackH = static_cast<int>(w.h * 0.60f);

            int wIdx = 0;
            for (int i = 0; i < numKeys; ++i) {
                int m = startMidi + i;
                if (isBlackMidi(m)) continue;
                int kx = w.x + static_cast<int>(wIdx * whiteW);
                int kw = static_cast<int>((wIdx + 1) * whiteW) - static_cast<int>(wIdx * whiteW) - 1;
                g_display.fillRect(kx, w.y, kw, w.h, (m == g_heldPianoMidi) ? w.fgCol565 : 0xF7BE);
                g_display.drawRect(kx, w.y, kw, w.h, 0x2945);
                wIdx++;
            }
            wIdx = 0;
            for (int i = 0; i < numKeys; ++i) {
                int m = startMidi + i;
                if (!isBlackMidi(m)) { wIdx++; continue; }
                int kx = w.x + static_cast<int>((wIdx - 0.33f) * whiteW);
                int kw = static_cast<int>(whiteW * 0.66f);
                g_display.fillRect(kx, w.y, kw, blackH, (m == g_heldPianoMidi) ? 0xFD20 : 0x18E3);
            }
            break;
        }
        default: break;
    }
}

static void drawTopStatusHeader() {
    int scrW = g_display.screenWidth();
    int barH = (SCREEN_PROFILE == 0) ? 34 : 22;
    g_display.fillRect(0, 0, scrW, barH, rgb565(28, 36, 48));
    g_display.setTextColor(rgb565(0, 229, 255), rgb565(28, 36, 48));
    g_display.setTextDatum(ML_DATUM);
    g_display.drawString(g_engine.synthName.c_str(), 8, barH / 2);

    const char* audLbl = (g_audioOutMode == 0) ? g_usbAudio.getStatusLabel()
                       : (g_audioOutMode == 1) ? "OUT: I2S DAC" : "OUT: WIFI";
    int btnW = (SCREEN_PROFILE == 0) ? 120 : 78;
    g_display.fillRoundRect(scrW - btnW - 4, 2, btnW, barH - 4, 3, rgb565(37, 48, 64));
    g_display.drawRoundRect(scrW - btnW - 4, 2, btnW, barH - 4, 3, rgb565(0, 229, 255));
    g_display.setTextDatum(MC_DATUM);
    g_display.drawString(audLbl, scrW - btnW / 2 - 4, barH / 2);
}

static void redrawAllScreenWidgets() {
    g_display.fillScreen(rgb565(18, 22, 29));
    drawTopStatusHeader();
    const auto& list = g_engine.profileWidgets[SCREEN_PROFILE];
    for (const auto& w : list) {
        drawSingleWidget(w);
    }
}


// TOUCH INTERACTION LOOP ON ESP32

static int hitTestPianoOnEsp32(const SynthSDK::UiWidgetSpec& w, int tx, int ty) {
    const int numKeys = 37;
    const int startMidi = w.midiNote;
    int whiteCount = 0;
    for (int i = 0; i < numKeys; ++i) if (!isBlackMidi(startMidi + i)) whiteCount++;
    float whiteW = static_cast<float>(w.w) / whiteCount;
    int blackH = static_cast<int>(w.h * 0.60f);

    if (ty - w.y <= blackH) {
        int wIdx = 0;
        for (int i = 0; i < numKeys; ++i) {
            int m = startMidi + i;
            if (!isBlackMidi(m)) wIdx++;
            else {
                int kx = w.x + static_cast<int>((wIdx - 0.33f) * whiteW);
                int kw = static_cast<int>(whiteW * 0.66f);
                if (tx >= kx && tx <= kx + kw) return m;
            }
        }
    }
    int targetWhite = std::clamp(static_cast<int>((tx - w.x) / whiteW), 0, whiteCount - 1);
    int wIdx = 0;
    for (int i = 0; i < numKeys; ++i) {
        int m = startMidi + i;
        if (!isBlackMidi(m)) {
            if (wIdx == targetWhite) return m;
            wIdx++;
        }
    }
    return -1;
}

static void handleTouchTick() {
    if (pollHardwareGpioControls()) {
        const auto& list = g_engine.profileWidgets[SCREEN_PROFILE];
        for (const auto& w : list) {
            if (w.type != SynthSDK::W_PIANO_KEYBOARD && w.type != SynthSDK::W_OSCILLOSCOPE) {
                drawSingleWidget(w);
            }
        }
    }

    uint16_t tx = 0, ty = 0;
    bool touching = g_display.getTouch(&tx, &ty);
    auto& list = g_engine.profileWidgets[SCREEN_PROFILE];
    int barH = (SCREEN_PROFILE == 0) ? 34 : 22;

    if (touching && !g_wasTouching) {
        if (ty < barH && tx > g_display.screenWidth() - 125) {
            g_audioOutMode = (g_audioOutMode + 1) % 3;
            UsbAudioPartner::s_enableUsbAudio = (g_audioOutMode == 0);
            drawTopStatusHeader();
            g_wasTouching = true;
            return;
        }

        for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i) {
            auto& w = list[i];
            if (tx >= w.x && tx <= w.x + w.w && ty >= w.y && ty <= w.y + w.h) {
                g_activeTouchWidget = i;
                g_dragStartY = ty;
                auto& p1 = g_engine.params[std::clamp(w.paramId1, 0, static_cast<int>(SynthSDK::PARAM_COUNT) - 1)];
                g_dragStartNorm = p1.getNormalized();

                if (w.type == SynthSDK::W_TOGGLE_BTN) {
                    p1.setNormalized(p1.getNormalized() > 0.5f ? 0.0f : 1.0f);
                    drawSingleWidget(w);
                } else if (w.type == SynthSDK::W_CYCLE_SELECT) {
                    float nxt = p1.curVal + 1.0f;
                    if (nxt > p1.maxVal) nxt = p1.minVal;
                    p1.curVal = nxt;
                    drawSingleWidget(w);
                } else if (w.type == SynthSDK::W_MOMENTARY_PAD) {
                    g_engine.noteOn(w.midiNote, 115);
                } else if (w.type == SynthSDK::W_STEP_SEQ_GRID) {
                    int step = std::clamp((static_cast<int>(tx) - w.x) * 16 / std::max(16, w.w), 0, 15);
                    g_engine.arpSteps[step].gate = !g_engine.arpSteps[step].gate;
                    drawSingleWidget(w);
                } else if (w.type == SynthSDK::W_DRUM_GRID) {
                    int step = std::clamp((static_cast<int>(tx) - (w.x + 4)) * 16 / std::max(16, w.w - 8), 0, 15);
                    int trk  = std::clamp((static_cast<int>(ty) - (w.y + 14)) * SynthSDK::ModularSynthEngine::NUM_DRUM_TRACKS / std::max(12, w.h - 16),
                                          0, SynthSDK::ModularSynthEngine::NUM_DRUM_TRACKS - 1);
                    g_engine.drumTracks[trk].steps[step] = !g_engine.drumTracks[trk].steps[step];
                    drawSingleWidget(w);
                } else if (w.type == SynthSDK::W_PIANO_KEYBOARD) {
                    g_heldPianoMidi = hitTestPianoOnEsp32(w, tx, ty);
                    if (g_heldPianoMidi >= 0) g_engine.noteOn(g_heldPianoMidi, 108);
                    drawSingleWidget(w);
                }
                break;
            }
        }
    } else if (touching && g_activeTouchWidget >= 0 && g_activeTouchWidget < static_cast<int>(list.size())) {
        auto& w = list[g_activeTouchWidget];
        auto& p1 = g_engine.params[std::clamp(w.paramId1, 0, static_cast<int>(SynthSDK::PARAM_COUNT) - 1)];
        auto& p2 = g_engine.params[std::clamp(w.paramId2, 0, static_cast<int>(SynthSDK::PARAM_COUNT) - 1)];

        if (w.type == SynthSDK::W_ROTARY_KNOB) {
            p1.setNormalized(g_dragStartNorm + (g_dragStartY - static_cast<int>(ty)) / 80.0f);
            drawSingleWidget(w);
        } else if (w.type == SynthSDK::W_VERT_FADER) {
            float norm = 1.0f - static_cast<float>(static_cast<int>(ty) - (w.y + 8)) / std::max(10, w.h - 24);
            p1.setNormalized(norm);
            drawSingleWidget(w);
        } else if (w.type == SynthSDK::W_HORIZ_SLIDER || w.type == SynthSDK::W_TOUCH_RIBBON) {
            float norm = static_cast<float>(static_cast<int>(tx) - (w.x + 4)) / std::max(10, w.w - 8);
            p1.setNormalized(norm);
            drawSingleWidget(w);
        } else if (w.type == SynthSDK::W_XY_PAD || w.type == SynthSDK::W_SAMPLER_WAVE) {
            p1.setNormalized(static_cast<float>(static_cast<int>(tx) - w.x) / std::max(10, w.w));
            p2.setNormalized(1.0f - static_cast<float>(static_cast<int>(ty) - w.y) / std::max(10, w.h));
            drawSingleWidget(w);
        } else if (w.type == SynthSDK::W_PIANO_KEYBOARD) {
            int newMidi = hitTestPianoOnEsp32(w, tx, ty);
            if (newMidi != g_heldPianoMidi) {
                if (g_heldPianoMidi >= 0) g_engine.noteOff(g_heldPianoMidi);
                g_heldPianoMidi = newMidi;
                if (g_heldPianoMidi >= 0) g_engine.noteOn(g_heldPianoMidi, 108);
                drawSingleWidget(w);
            }
        }
    } else if (!touching && g_wasTouching) {
        if (g_activeTouchWidget >= 0 && g_activeTouchWidget < static_cast<int>(list.size())) {
            const auto& w = list[g_activeTouchWidget];
            if (w.type == SynthSDK::W_MOMENTARY_PAD) g_engine.noteOff(w.midiNote);
        }
        if (g_heldPianoMidi >= 0) {
            g_engine.noteOff(g_heldPianoMidi);
            g_heldPianoMidi = -1;
            for (const auto& w : list) {
                if (w.type == SynthSDK::W_PIANO_KEYBOARD) drawSingleWidget(w);
            }
        }
        g_activeTouchWidget = -1;
    }

    g_wasTouching = touching;

    // Refresh live oscilloscope and active step playheads
    int curStep = g_engine.getCurrentSeqStep();
    bool stepChanged = (curStep != g_lastDrawnSeqStep);
    g_lastDrawnSeqStep = curStep;

    static uint32_t s_lastScopeMs = 0;
    bool scopeDue = (millis() - s_lastScopeMs > 60);
    if (scopeDue) s_lastScopeMs = millis();

    if (scopeDue || stepChanged) {
        for (const auto& w : list) {
            if (scopeDue && w.type == SynthSDK::W_OSCILLOSCOPE) drawSingleWidget(w);
            if (stepChanged && (w.type == SynthSDK::W_STEP_SEQ_GRID || w.type == SynthSDK::W_DRUM_GRID)) {
                drawSingleWidget(w);
            }
        }
    }
}


// I2S + USB AUDIO + WI-FI + LIVE ADC AUDIO-IN TASK (CORE 1)

static void initI2SDac() {
    i2s_config_t i2s_cfg = {};
    i2s_cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_cfg.sample_rate = AUDIO_SRATE;
    i2s_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_cfg.dma_buf_count = 6;
    i2s_cfg.dma_buf_len = 256;
    i2s_cfg.use_apll = false;
    i2s_cfg.tx_desc_auto_clear = true;

    i2s_pin_config_t pin_cfg = {};
#ifdef I2S_BCLK_PIN
    pin_cfg.bck_io_num   = I2S_BCLK_PIN;
    pin_cfg.ws_io_num    = I2S_LRCK_PIN;
    pin_cfg.data_out_num = I2S_DOUT_PIN;
#else
    pin_cfg.bck_io_num   = g_engine.boardPins.i2sBclk;
    pin_cfg.ws_io_num    = g_engine.boardPins.i2sLrck;
    pin_cfg.data_out_num = g_engine.boardPins.i2sDout;
#endif
    pin_cfg.data_in_num  = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pin_cfg);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

static void sdkAudioTask(void*) {
    float monoBuf[256];
    float adcInBuf[256];
    int16_t stereoBuf[512];

    while (true) {
        // Optional Hardware Audio-In via ESP32 ADC Pin (when P_SMP_ADC_PIN >= 0)
        int adcPin = static_cast<int>(g_engine.params[SynthSDK::P_SMP_ADC_PIN].curVal);
        if (adcPin >= 0 && (g_engine.isRecordingSample || g_engine.params[SynthSDK::P_SMP_THRU_MON].curVal > 0.01f)) {
            for (int i = 0; i < 256; ++i) {
                adcInBuf[i] = (static_cast<float>(analogRead(adcPin)) - 2048.0f) * (1.0f / 2048.0f);
            }
            g_engine.pushAudioInSamples(adcInBuf, 256);
        }

        g_engine.renderFramesMono(monoBuf, 256);
        WifiAudioStreamer::instance().pushMonoSamples(monoBuf, 256);

        for (int i = 0; i < 256; ++i) {
            int16_t s = static_cast<int16_t>(monoBuf[i] * 32767.0f);
            stereoBuf[i * 2]     = s;
            stereoBuf[i * 2 + 1] = s;
        }

        if (g_audioOutMode == 0 && g_usbAudio.isConnected()) {
            g_usbAudio.writePcm(stereoBuf, sizeof(stereoBuf));
        } else {
            size_t bytesWritten = 0;
            i2s_write(I2S_NUM_0, stereoBuf, sizeof(stereoBuf), &bytesWritten, portMAX_DELAY);
        }
        taskYIELD();
    }
}

void setup() {
    Serial.begin(115200);
    initWaveshare7InchBacklightAndTouch();
    g_display.init();
#if SCREEN_PROFILE == 0
    g_display.setRotation(0);
    g_display.setFont(&fonts::Font2);
#else
    g_display.setFont(&fonts::Font0);
#endif
    g_display.fillScreen(rgb565(18, 22, 29));

    g_engine.init(AUDIO_SRATE);
    loadCompiledConfigIntoEngine();
    g_audioOutMode = g_engine.boardPins.audioOutMode;
    UsbAudioPartner::s_enableUsbAudio = (g_audioOutMode == 0);

    initI2SDac();

    WiFi.persistent(false);
    WifiAudioStreamer::instance().begin(g_engine.synthName.c_str());

    g_usbAudio.begin();
    g_usbMidi.begin(
        [](int note, int vel, bool on) {
            if (on) g_engine.noteOn(note, vel);
            else    g_engine.noteOff(note);
        },
        [](int cc, int val) {
            g_engine.handleMidiCC(cc, val);
        },
        [](float bendNorm) {
            g_engine.handlePitchBend(bendNorm);
        }
    );

    redrawAllScreenWidgets();
    xTaskCreatePinnedToCore(sdkAudioTask, "SdkAudioCore1", 8192, nullptr, 2, nullptr, 1);
}

void loop() {
    handleTouchTick();
    delay(15);
}