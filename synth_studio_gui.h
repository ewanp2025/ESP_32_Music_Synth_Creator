#ifndef SYNTH_STUDIO_GUI_H
#define SYNTH_STUDIO_GUI_H

#include <QMainWindow>
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QCheckBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QFile>
#include <QProgressBar>
#include <QRegularExpression>

#include "synth_sdk_modules.h"

namespace SynthSDK {


// 8-BIT SAMPLER WAVEFORM PREVIEW WIDGET (FOR STUDIO TAB)

class SamplerWaveformPreview : public QWidget {
public:
    ModularSynthEngine* engine = nullptr;
    explicit SamplerWaveformPreview(ModularSynthEngine* eng, QWidget* parent = nullptr)
        : QWidget(parent), engine(eng) {
        setMinimumHeight(110);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(10, 15, 24));
        p.setPen(QColor(56, 189, 248));
        p.drawRect(0, 0, width() - 1, height() - 1);

        if (!engine || engine->samplerData.empty()) return;
        const auto& data = engine->samplerData;
        int n = static_cast<int>(data.size());
        int midY = height() / 2;

        // Draw waveform
        p.setPen(QColor(251, 146, 60));
        int prevX = 2, prevY = midY;
        for (int x = 2; x < width() - 2; ++x) {
            int idx = std::clamp((x - 2) * n / std::max(1, width() - 4), 0, n - 1);
            int y = midY - (static_cast<int>(data[idx]) * (height() * 42 / 100)) / 128;
            if (x > 2) p.drawLine(prevX, prevY, x, y);
            prevX = x; prevY = y;
        }

        // Draw Start & End Loop Markers
        int startX = 2 + static_cast<int>(engine->params[P_SMP_START].curVal * (width() - 4));
        int endX   = 2 + static_cast<int>(engine->params[P_SMP_END].curVal   * (width() - 4));
        p.setPen(QPen(QColor(34, 197, 94), 2));
        p.drawLine(startX, 1, startX, height() - 2);
        p.setPen(QPen(QColor(239, 68, 68), 2));
        p.drawLine(endX, 1, endX, height() - 2);

        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        QString status = engine->isRecordingSample
                             ? QString("RECORDING AUDIO-IN... (%1 / %2 bytes)").arg(engine->recordWritePos).arg(n)
                             : QString("8-Bit Signed PCM (%1 bytes) | Green = Start, Red = Loop/End").arg(n);
        p.drawText(8, 16, status);
    }
};


// INTERACTIVE ESP32 SCREEN  (DESIGN MODE + LIVE PLAY SIMULATOR)

class Esp32ScreenCanvas : public QWidget {
public:
    ModularSynthEngine* engine = nullptr;
    int activeProfile = 0;
    bool designMode = true;
    int selectedWidgetIdx = -1;

    std::function<void(int)> onWidgetSelected;
    std::function<void()> onParameterChangedLive;

    explicit Esp32ScreenCanvas(ModularSynthEngine* eng, QWidget* parent = nullptr)
        : QWidget(parent), engine(eng) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        updateCanvasDimensions();
    }

    void setProfile(int prof) {
        activeProfile = std::clamp(prof, 0, 2);
        selectedWidgetIdx = -1;
        updateCanvasDimensions();
        if (onWidgetSelected) onWidgetSelected(-1);
        update();
    }

    void updateCanvasDimensions() {
        if (activeProfile == 0) { m_scrW = 800; m_scrH = 480; m_scale = 1; }
        else if (activeProfile == 1) { m_scrW = 320; m_scrH = 240; m_scale = 2; }
        else { m_scrW = 128; m_scrH = 64; m_scale = 4; }
        setFixedSize(m_scrW * m_scale + 24, m_scrH * m_scale + 52);
    }

    static QColor from565(uint16_t c, bool mono = false) {
        int r = ((c >> 11) & 0x1F) * 255 / 31;
        int g = ((c >> 5)  & 0x3F) * 255 / 63;
        int b = (c         & 0x1F) * 255 / 31;
        if (mono) {
            int luma = (r * 77 + g * 150 + b * 29) >> 8;
            return (luma > 60) ? QColor(180, 255, 255) : QColor(12, 20, 28);
        }
        return QColor(r, g, b);
    }

    static uint16_t to565(const QColor& col) {
        return ((col.red() & 0xF8) << 8) | ((col.green() & 0xFC) << 3) | (col.blue() >> 3);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(15, 23, 42));

        p.setPen(QColor(0, 229, 255));
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        QString modeBanner = designMode
                                 ? "DESIGN MODE: Click to Select, Drag to Move, Bottom-Right Corner to Resize"
                                 : "LIVE PLAY MODE: Turn Knobs/XY/Drums/Arp, Play Piano or PC Keys (Z..M / Q..U)";
        p.drawText(12, 20, modeBanner);

        QImage fb(m_scrW, m_scrH, QImage::Format_RGB32);
        bool mono = (activeProfile == 2);
        fb.fill(mono ? QColor(12, 20, 28) : QColor(18, 22, 29));

        {
            QPainter fp(&fb);
            fp.setRenderHint(QPainter::Antialiasing, !mono);

            if (designMode && !mono) {
                fp.setPen(QColor(32, 40, 54));
                for (int gx = 0; gx < m_scrW; gx += 16) fp.drawLine(gx, 0, gx, m_scrH);
                for (int gy = 0; gy < m_scrH; gy += 16) fp.drawLine(0, gy, m_scrW, gy);
            }

            if (activeProfile != 2) {
                int barH = (activeProfile == 0) ? 34 : 22;
                fp.fillRect(0, 0, m_scrW, barH, QColor(28, 36, 48));
                fp.setPen(QColor(0, 229, 255));
                fp.setFont(QFont("Segoe UI", (activeProfile == 0) ? 9 : 7, QFont::Bold));
                fp.drawText(8, barH - 8, QString::fromStdString(engine->synthName) +
                                             (activeProfile == 0 ? "  |  8-BIT SAMPLER + 6-DRUM + ARP + USB/I2S" : " | SYNTH SDK"));
            }

            auto& widgets = engine->profileWidgets[activeProfile];
            for (size_t i = 0; i < widgets.size(); ++i) {
                drawWidgetOnFb(fp, widgets[i], mono);

                if (designMode && static_cast<int>(i) == selectedWidgetIdx) {
                    fp.setPen(QPen(QColor(255, 215, 0), 1, Qt::DashLine));
                    fp.setBrush(Qt::NoBrush);
                    fp.drawRect(widgets[i].x, widgets[i].y, widgets[i].w - 1, widgets[i].h - 1);
                    fp.fillRect(widgets[i].x + widgets[i].w - 7, widgets[i].y + widgets[i].h - 7, 7, 7, QColor(255, 215, 0));
                }
            }
        }

        QImage scaledFb = fb.scaled(m_scrW * m_scale, m_scrH * m_scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        p.drawImage(12, 32, scaledFb);
        p.setPen(QColor(0, 229, 255));
        p.drawRect(11, 31, m_scrW * m_scale + 1, m_scrH * m_scale + 1);
    }

    void mousePressEvent(QMouseEvent* ev) override {
        setFocus();
        QPoint sp = toScreenCoords(ev->pos());
        auto& widgets = engine->profileWidgets[activeProfile];

        if (designMode) {
            selectedWidgetIdx = -1;
            for (int i = static_cast<int>(widgets.size()) - 1; i >= 0; --i) {
                const auto& w = widgets[i];
                if (sp.x() >= w.x && sp.x() <= w.x + w.w && sp.y() >= w.y && sp.y() <= w.y + w.h) {
                    selectedWidgetIdx = i;
                    m_resizing = (sp.x() >= w.x + w.w - 10 && sp.y() >= w.y + w.h - 10);
                    m_dragging = !m_resizing;
                    m_dragOffset = QPoint(sp.x() - w.x, sp.y() - w.y);
                    break;
                }
            }
            if (onWidgetSelected) onWidgetSelected(selectedWidgetIdx);
            update();
            return;
        }

        for (int i = static_cast<int>(widgets.size()) - 1; i >= 0; --i) {
            auto& w = widgets[i];
            if (sp.x() >= w.x && sp.x() <= w.x + w.w && sp.y() >= w.y && sp.y() <= w.y + w.h) {
                m_activeLiveWidget = i;
                m_liveDragStart = sp;
                if (w.paramId1 >= 0 && w.paramId1 < PARAM_COUNT) {
                    m_liveStartNorm = engine->params[w.paramId1].getNormalized();
                }
                handleLiveWidgetTouch(w, sp, true);
                break;
            }
        }
        update();
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        QPoint sp = toScreenCoords(ev->pos());
        auto& widgets = engine->profileWidgets[activeProfile];

        if (designMode) {
            if (selectedWidgetIdx >= 0 && selectedWidgetIdx < static_cast<int>(widgets.size())) {
                auto& w = widgets[selectedWidgetIdx];
                int snap = (activeProfile == 0) ? 4 : 2;
                if (m_dragging) {
                    w.x = std::clamp(((sp.x() - m_dragOffset.x()) / snap) * snap, 0, m_scrW - w.w);
                    w.y = std::clamp(((sp.y() - m_dragOffset.y()) / snap) * snap, 0, m_scrH - w.h);
                    if (onWidgetSelected) onWidgetSelected(selectedWidgetIdx);
                    update();
                } else if (m_resizing) {
                    w.w = std::clamp(((sp.x() - w.x) / snap) * snap, 20, m_scrW - w.x);
                    w.h = std::clamp(((sp.y() - w.y) / snap) * snap, 14, m_scrH - w.y);
                    if (onWidgetSelected) onWidgetSelected(selectedWidgetIdx);
                    update();
                }
            }
            return;
        }

        if (m_activeLiveWidget >= 0 && m_activeLiveWidget < static_cast<int>(widgets.size())) {
            handleLiveWidgetTouch(widgets[m_activeLiveWidget], sp, false);
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent*) override {
        m_dragging = false;
        m_resizing = false;
        if (!designMode && m_activeLiveWidget >= 0) {
            auto& widgets = engine->profileWidgets[activeProfile];
            if (m_activeLiveWidget < static_cast<int>(widgets.size())) {
                const auto& w = widgets[m_activeLiveWidget];
                if (w.type == W_MOMENTARY_PAD) engine->noteOff(w.midiNote);
            }
        }
        if (m_heldPianoMidi >= 0) {
            engine->noteOff(m_heldPianoMidi);
            m_heldPianoMidi = -1;
        }
        m_activeLiveWidget = -1;
        update();
    }

    void keyPressEvent(QKeyEvent* ev) override {
        if (ev->isAutoRepeat()) return;
        int midi = pcKeyToMidi(ev->key());
        if (midi >= 0) { engine->noteOn(midi, 108); update(); }
    }

    void keyReleaseEvent(QKeyEvent* ev) override {
        if (ev->isAutoRepeat()) return;
        int midi = pcKeyToMidi(ev->key());
        if (midi >= 0) { engine->noteOff(midi); update(); }
    }

private:
    int m_scrW = 800, m_scrH = 480, m_scale = 1;
    bool m_dragging = false, m_resizing = false;
    QPoint m_dragOffset;
    int m_activeLiveWidget = -1;
    QPoint m_liveDragStart;
    float m_liveStartNorm = 0.5f;
    int m_heldPianoMidi = -1;

    QPoint toScreenCoords(const QPoint& pos) const {
        return QPoint((pos.x() - 12) / m_scale, (pos.y() - 32) / m_scale);
    }

    static int pcKeyToMidi(int key) {
        switch (key) {
        case Qt::Key_Z: return 60; case Qt::Key_S: return 61; case Qt::Key_X: return 62;
        case Qt::Key_D: return 63; case Qt::Key_C: return 64; case Qt::Key_V: return 65;
        case Qt::Key_G: return 66; case Qt::Key_B: return 67; case Qt::Key_H: return 68;
        case Qt::Key_N: return 69; case Qt::Key_J: return 70; case Qt::Key_M: return 71;
        case Qt::Key_Comma: return 72;
        case Qt::Key_Q: return 72; case Qt::Key_2: return 73; case Qt::Key_W: return 74;
        case Qt::Key_3: return 75; case Qt::Key_E: return 76; case Qt::Key_R: return 77;
        case Qt::Key_5: return 78; case Qt::Key_T: return 79; case Qt::Key_6: return 80;
        case Qt::Key_Y: return 81; case Qt::Key_7: return 82; case Qt::Key_U: return 83;
        default: return -1;
        }
    }

    void handleLiveWidgetTouch(UiWidgetSpec& w, const QPoint& sp, bool isFirstPress) {
        auto& p1 = engine->params[std::clamp(w.paramId1, 0, PARAM_COUNT - 1)];
        auto& p2 = engine->params[std::clamp(w.paramId2, 0, PARAM_COUNT - 1)];

        switch (w.type) {
        case W_ROTARY_KNOB:
            p1.setNormalized(m_liveStartNorm + static_cast<float>(m_liveDragStart.y() - sp.y()) / 80.0f);
            break;
        case W_VERT_FADER:
            p1.setNormalized(1.0f - static_cast<float>(sp.y() - (w.y + 8)) / std::max(10, w.h - 24));
            break;
        case W_HORIZ_SLIDER:
        case W_TOUCH_RIBBON:
            p1.setNormalized(static_cast<float>(sp.x() - (w.x + 6)) / std::max(10, w.w - 12));
            break;
        case W_TOGGLE_BTN:
            if (isFirstPress) p1.setNormalized(p1.getNormalized() > 0.5f ? 0.0f : 1.0f);
            break;
        case W_CYCLE_SELECT:
            if (isFirstPress) {
                float next = p1.curVal + 1.0f;
                if (next > p1.maxVal) next = p1.minVal;
                p1.curVal = next;
            }
            break;
        case W_MOMENTARY_PAD:
            if (isFirstPress) engine->noteOn(w.midiNote, 115);
            break;
        case W_XY_PAD:
        case W_SAMPLER_WAVE:
            p1.setNormalized(static_cast<float>(sp.x() - w.x) / std::max(10, w.w));
            p2.setNormalized(1.0f - static_cast<float>(sp.y() - w.y) / std::max(10, w.h));
            break;
        case W_STEP_SEQ_GRID:
            if (isFirstPress) {
                int step = std::clamp((sp.x() - w.x) * 16 / std::max(16, w.w), 0, 15);
                engine->arpSteps[step].gate = !engine->arpSteps[step].gate;
            }
            break;
        case W_DRUM_GRID:
            if (isFirstPress) {
                int step = std::clamp((sp.x() - (w.x + 4)) * 16 / std::max(16, w.w - 8), 0, 15);
                int trk  = std::clamp((sp.y() - (w.y + 14)) * ModularSynthEngine::NUM_DRUM_TRACKS / std::max(12, w.h - 16),
                                     0, ModularSynthEngine::NUM_DRUM_TRACKS - 1);
                engine->drumTracks[trk].steps[step] = !engine->drumTracks[trk].steps[step];
            }
            break;
        case W_PIANO_KEYBOARD: {
            int newMidi = hitTestWidgetPiano(w, sp.x(), sp.y());
            if (newMidi != m_heldPianoMidi) {
                if (m_heldPianoMidi >= 0) engine->noteOff(m_heldPianoMidi);
                m_heldPianoMidi = newMidi;
                if (m_heldPianoMidi >= 0) engine->noteOn(m_heldPianoMidi, 108);
            }
            break;
        }
        default: break;
        }
        if (onParameterChangedLive) onParameterChangedLive();
    }

    static bool isBlackMidi(int m) {
        int n = m % 12;
        return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
    }

    static int hitTestWidgetPiano(const UiWidgetSpec& w, int tx, int ty) {
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

    void drawWidgetOnFb(QPainter& fp, const UiWidgetSpec& w, bool mono) {
        QColor bg = from565(w.bgCol565, mono);
        QColor fg = from565(w.fgCol565, mono);
        QColor tc = from565(w.textCol565, mono);
        const auto& p1 = engine->params[std::clamp(w.paramId1, 0, PARAM_COUNT - 1)];
        const auto& p2 = engine->params[std::clamp(w.paramId2, 0, PARAM_COUNT - 1)];
        float norm1 = p1.getNormalized();
        float norm2 = p2.getNormalized();

        fp.setFont(QFont("Segoe UI", (activeProfile == 0) ? 7 : 6, QFont::Bold));

        if (w.type != W_PIANO_KEYBOARD) {
            fp.setPen(QPen(fg.darker(150), 1));
            fp.setBrush(bg);
            fp.drawRoundedRect(w.x, w.y, w.w, w.h, 4, 4);
        }

        switch (w.type) {
        case W_ROTARY_KNOB: {
            int rad = std::min(w.w, w.h - 16) / 2 - 4;
            int cx = w.x + w.w / 2;
            int cy = w.y + (w.h - 12) / 2;
            QRect dialRect(cx - rad, cy - rad, rad * 2, rad * 2);
            fp.setPen(QPen(QColor(60, 75, 95), 3));
            fp.drawArc(dialRect, 225 * 16, -270 * 16);
            fp.setPen(QPen(fg, 3));
            fp.drawArc(dialRect, 225 * 16, static_cast<int>(-270 * 16 * norm1));

            float angleRad = (225.0f - 270.0f * norm1) * M_PI_F / 180.0f;
            fp.setPen(QPen(tc, 2));
            fp.drawLine(cx, cy, cx + static_cast<int>(std::cos(angleRad) * (rad - 2)),
                        cy - static_cast<int>(std::sin(angleRad) * (rad - 2)));
            fp.setPen(tc);
            fp.drawText(QRect(w.x, w.y + w.h - 14, w.w, 14), Qt::AlignCenter, QString::fromStdString(w.label));
            break;
        }
        case W_VERT_FADER: {
            int trackH = w.h - 22;
            int fillH = static_cast<int>(trackH * norm1);
            fp.fillRect(w.x + w.w / 2 - 4, w.y + 6, 8, trackH, QColor(12, 16, 22));
            fp.fillRect(w.x + w.w / 2 - 4, w.y + 6 + (trackH - fillH), 8, fillH, fg);
            fp.fillRect(w.x + 6, w.y + 3 + (trackH - fillH), w.w - 12, 6, tc);
            fp.setPen(tc);
            fp.drawText(QRect(w.x, w.y + w.h - 14, w.w, 14), Qt::AlignCenter, QString::fromStdString(w.label));
            break;
        }
        case W_HORIZ_SLIDER:
        case W_TOUCH_RIBBON: {
            int fillW = static_cast<int>((w.w - 8) * norm1);
            fp.fillRect(w.x + 4, w.y + 4, fillW, w.h - 8, fg.darker(130));
            fp.setPen(tc);
            fp.drawText(QRect(w.x, w.y, w.w, w.h), Qt::AlignCenter, QString::fromStdString(w.label));
            break;
        }
        case W_TOGGLE_BTN:
        case W_MOMENTARY_PAD: {
            bool on = (w.type == W_TOGGLE_BTN) && (norm1 > 0.5f);
            if (on) fp.fillRect(w.x + 2, w.y + 2, w.w - 4, w.h - 4, fg.darker(140));
            fp.setPen(tc);
            fp.drawText(QRect(w.x, w.y, w.w, w.h), Qt::AlignCenter, QString::fromStdString(w.label));
            break;
        }
        case W_CYCLE_SELECT: {
            QString txt = QString::fromStdString(w.label) + ": " + QString::number(static_cast<int>(p1.curVal));
            if (w.paramId1 == P_OSC1_TYPE || w.paramId1 == P_OSC2_TYPE) {
                static const char* oscNames[] = {"VA", "M1Org", "Harm", "4OpFM", "SupSaw", "Karplus", "Vowel", "Add8", "Chip", "AST"};
                txt = QString::fromStdString(w.label) + ": " + oscNames[std::clamp(static_cast<int>(p1.curVal), 0, 9)];
            } else if (w.paramId1 == P_FILT_TYPE) {
                static const char* fltNames[] = {"LP12", "BP12", "HP12", "Lad24", "303", "Formant"};
                txt = QString::fromStdString(w.label) + ": " + fltNames[std::clamp(static_cast<int>(p1.curVal), 0, 5)];
            } else if (w.paramId1 == P_ARP_MODE) {
                static const char* arpNames[] = {"OFF", "UP", "DOWN", "UP/DN", "RAND", "16-SEQ"};
                txt = QString::fromStdString(w.label) + ": " + arpNames[std::clamp(static_cast<int>(p1.curVal), 0, 5)];
            }
            fp.setPen(tc);
            fp.drawText(QRect(w.x + 4, w.y, w.w - 8, w.h), Qt::AlignCenter, txt);
            break;
        }
        case W_XY_PAD: {
            int cx = w.x + 6 + static_cast<int>((w.w - 12) * norm1);
            int cy = w.y + 6 + static_cast<int>((w.h - 12) * (1.0f - norm2));
            fp.setPen(QColor(45, 60, 80));
            fp.drawLine(cx, w.y + 2, cx, w.y + w.h - 2);
            fp.drawLine(w.x + 2, cy, w.x + w.w - 2, cy);
            fp.setBrush(fg);
            fp.setPen(tc);
            fp.drawEllipse(QPoint(cx, cy), 5, 5);
            fp.drawText(QRect(w.x + 4, w.y + 2, w.w - 8, 14), Qt::AlignLeft, QString::fromStdString(w.label));
            break;
        }
        case W_SAMPLER_WAVE: {
            const auto& sdata = engine->samplerData;
            if (!sdata.empty()) {
                int midY = w.y + w.h / 2 + 4;
                fp.setPen(fg);
                int prevX = w.x + 4, prevY = midY;
                int n = static_cast<int>(sdata.size());
                for (int dx = 4; dx < w.w - 4; dx += 2) {
                    int idx = std::clamp((dx - 4) * n / std::max(1, w.w - 8), 0, n - 1);
                    int sy = midY - (static_cast<int>(sdata[idx]) * (w.h * 35 / 100)) / 128;
                    if (dx > 4) fp.drawLine(prevX, prevY, w.x + dx, sy);
                    prevX = w.x + dx; prevY = sy;
                }
            }
            int sx = w.x + 4 + static_cast<int>(engine->params[P_SMP_START].curVal * (w.w - 8));
            int ex = w.x + 4 + static_cast<int>(engine->params[P_SMP_END].curVal   * (w.w - 8));
            fp.setPen(QColor(34, 197, 94)); fp.drawLine(sx, w.y + 14, sx, w.y + w.h - 3);
            fp.setPen(QColor(239, 68, 68)); fp.drawLine(ex, w.y + 14, ex, w.y + w.h - 3);
            fp.setPen(tc);
            fp.drawText(QRect(w.x + 4, w.y + 2, w.w - 8, 12), Qt::AlignLeft, QString::fromStdString(w.label));
            break;
        }
        case W_ADSR_GRAPH: {
            int baseId = (w.paramId2 == 1) ? P_FLT_A : (w.paramId2 == 2 ? P_MOD_A : P_AMP_A);
            float a = engine->params[baseId].getNormalized();
            float d = engine->params[baseId + 1].getNormalized();
            float s = engine->params[baseId + 2].getNormalized();
            float r = engine->params[baseId + 3].getNormalized();

            int x0 = w.x + 6, yBot = w.y + w.h - 6, yTop = w.y + 16;
            int availW = w.w - 12;
            int x1 = x0 + static_cast<int>(availW * 0.25f * a + 4);
            int x2 = x1 + static_cast<int>(availW * 0.25f * d + 4);
            int ySus = yBot - static_cast<int>((yBot - yTop) * s);
            int x3 = x2 + static_cast<int>(availW * 0.22f);
            int x4 = std::min(w.x + w.w - 6, x3 + static_cast<int>(availW * 0.25f * r + 4));

            fp.setPen(QPen(fg, 2));
            fp.drawLine(x0, yBot, x1, yTop);
            fp.drawLine(x1, yTop, x2, ySus);
            fp.drawLine(x2, ySus, x3, ySus);
            fp.drawLine(x3, ySus, x4, yBot);
            fp.setPen(tc);
            fp.drawText(QRect(w.x + 6, w.y + 2, w.w - 12, 12), Qt::AlignLeft, QString::fromStdString(w.label));
            break;
        }
        case W_OSCILLOSCOPE: {
            float scope[ModularSynthEngine::SCOPE_SIZE];
            engine->getScopeSnapshot(scope);
            fp.setPen(QPen(fg, 1));
            int midY = w.y + w.h / 2;
            int prevX = w.x + 2, prevY = midY;
            for (int i = 0; i < ModularSynthEngine::SCOPE_SIZE; i += 2) {
                int sx = w.x + 2 + (i * (w.w - 4)) / ModularSynthEngine::SCOPE_SIZE;
                int sy = std::clamp(midY - static_cast<int>(scope[i] * (w.h * 0.42f)), w.y + 2, w.y + w.h - 2);
                if (i > 0) fp.drawLine(prevX, prevY, sx, sy);
                prevX = sx; prevY = sy;
            }
            break;
        }
        case W_STEP_SEQ_GRID: {
            int stepW = std::max(4, (w.w - 8) / 16);
            int curStep = engine->getCurrentSeqStep();
            for (int s = 0; s < 16; ++s) {
                int sx = w.x + 4 + s * stepW;
                bool gate = engine->arpSteps[s].gate;
                QColor c = (s == curStep) ? QColor(255, 215, 0) : (gate ? fg : QColor(40, 50, 65));
                int barH = std::max(6, ((engine->arpSteps[s].semitone + 24) * (w.h - 20)) / 48);
                fp.fillRect(sx, w.y + w.h - 4 - barH, stepW - 2, barH, c);
            }
            fp.setPen(tc);
            fp.drawText(QRect(w.x + 4, w.y + 2, w.w - 8, 12), Qt::AlignLeft, QString::fromStdString(w.label));
            break;
        }
        case W_DRUM_GRID: {
            int stepW = std::max(4, (w.w - 8) / 16);
            int rowH  = std::max(4, (w.h - 18) / ModularSynthEngine::NUM_DRUM_TRACKS);
            int curStep = engine->getCurrentSeqStep();
            for (int r = 0; r < ModularSynthEngine::NUM_DRUM_TRACKS; ++r) {
                for (int s = 0; s < 16; ++s) {
                    bool on = engine->drumTracks[r].steps[s];
                    QColor c = on ? ((s == curStep) ? QColor(255, 215, 0) : fg)
                                  : ((s == curStep) ? QColor(65, 80, 100) : QColor(30, 38, 50));
                    fp.fillRect(w.x + 4 + s * stepW, w.y + 14 + r * rowH, stepW - 2, rowH - 2, c);
                }
            }
            fp.setPen(tc);
            fp.drawText(QRect(w.x + 4, w.y + 1, w.w - 8, 12), Qt::AlignLeft, QString::fromStdString(w.label));
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
                fp.fillRect(kx, w.y, kw, w.h, (m == m_heldPianoMidi) ? fg : QColor(240, 242, 245));
                fp.setPen(QColor(40, 40, 40));
                fp.drawRect(kx, w.y, kw, w.h);
                wIdx++;
            }
            wIdx = 0;
            for (int i = 0; i < numKeys; ++i) {
                int m = startMidi + i;
                if (!isBlackMidi(m)) { wIdx++; continue; }
                int kx = w.x + static_cast<int>((wIdx - 0.33f) * whiteW);
                int kw = static_cast<int>(whiteW * 0.66f);
                fp.fillRect(kx, w.y, kw, blackH, (m == m_heldPianoMidi) ? QColor(255, 165, 0) : QColor(25, 28, 34));
            }
            break;
        }
        default: break;
        }
    }
};


// MAIN SYNTH STUDIO APPLICATION WINDOW

class SynthStudioWindow : public QMainWindow {
public:
    explicit SynthStudioWindow(ModularSynthEngine* eng, QWidget* parent = nullptr)
        : QMainWindow(parent), m_engine(eng) {
        setWindowTitle("ESP32-S3 Visual Synth Studio: 8-Bit Sampler, Drum Machine, Arp & Off-Screen GPIO/MIDI Matrix");
        resize(1540, 960);
        setStyleSheet(
            "QMainWindow, QWidget { background-color: #0F172A; color: #E2E8F0; font-family: 'Segoe UI'; font-size: 12px; }"
            "QGroupBox { border: 1px solid #334155; border-radius: 5px; margin-top: 8px; padding-top: 8px; font-weight: bold; color: #38BDF8; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
            "QPushButton { background-color: #1E293B; border: 1px solid #475569; border-radius: 4px; padding: 4px 9px; color: #F8FAFC; font-weight: bold; }"
            "QPushButton:hover { background-color: #334155; border-color: #38BDF8; }"
            "QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit, QTextEdit { background-color: #1E293B; border: 1px solid #475569; border-radius: 3px; padding: 2px; color: #FFFFFF; }"
            "QTableWidget { background-color: #0F172A; gridline-color: #334155; color: #F8FAFC; }"
            "QHeaderView::section { background-color: #1E293B; color: #38BDF8; font-weight: bold; padding: 4px; border: 1px solid #334155; }"
            "QTabWidget::pane { border: 1px solid #334155; }"
            "QTabBar::tab { background: #1E293B; padding: 6px 10px; border: 1px solid #334155; }"
            "QTabBar::tab:selected { background: #0284C7; color: #FFFFFF; font-weight: bold; }"
            );

        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* rootLayout = new QVBoxLayout(central);
        rootLayout->setContentsMargins(6, 6, 6, 6);

        // TOP TOOLBAR
        rootLayout->addWidget(buildTopToolbar());

        // VERTICAL SPLITTER: TOP = [MODULE RACK | ESP32 SCREEN | INSPECTOR], BOTTOM = [OFF-SCREEN MIDI/GPIO & NOTES MATRIX]
        auto* mainVertSplitter = new QSplitter(Qt::Vertical, central);

        auto* topHorizSplitter = new QSplitter(Qt::Horizontal, mainVertSplitter);
        topHorizSplitter->addWidget(buildLeftModuleRack());
        topHorizSplitter->addWidget(buildCenterCanvasPane());
        topHorizSplitter->addWidget(buildRightInspectorPane());
        topHorizSplitter->setStretchFactor(0, 4);
        topHorizSplitter->setStretchFactor(1, 5);
        topHorizSplitter->setStretchFactor(2, 3);

        mainVertSplitter->addWidget(topHorizSplitter);
        mainVertSplitter->addWidget(buildBottomHardwareRoutingMatrix());
        mainVertSplitter->setStretchFactor(0, 7);
        mainVertSplitter->setStretchFactor(1, 3);

        rootLayout->addWidget(mainVertSplitter, 1);

        // Refresh timer for Live Scope, Sampler Waveform, Audio-In Meter & MIDI Learn
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            m_canvas->update();
            if (m_samplerWaveView) m_samplerWaveView->update();
            if (m_audioInMeter) {
                m_audioInMeter->setValue(std::clamp(static_cast<int>(m_engine->liveAudioInLevel * 100.0f), 0, 100));
            }
            if (m_engine->lastLearnedCc >= 0) {
                m_midiCcSpin->setValue(m_engine->lastLearnedCc);
                m_btnMidiLearn->setText("MIDI Learn CC");
                refreshBottomRoutingTable();
                m_engine->lastLearnedCc = -1;
            }
        });
        timer->start(35);
    }

private:
    ModularSynthEngine* m_engine = nullptr;
    Esp32ScreenCanvas* m_canvas = nullptr;
    SamplerWaveformPreview* m_samplerWaveView = nullptr;
    QProgressBar* m_audioInMeter = nullptr;
    QTableWidget* m_routingTable = nullptr;
    bool m_updatingTable = false;

    std::vector<QSlider*> m_paramSliders;
    std::vector<QLabel*>  m_paramValLabels;

    QLineEdit* m_edSynthName = nullptr;
    QComboBox* m_widTypeCombo = nullptr;
    QLineEdit* m_widLabelEdit = nullptr;
    QSpinBox *m_widX = nullptr, *m_widY = nullptr, *m_widW = nullptr, *m_widH = nullptr;
    QComboBox *m_widParam1 = nullptr, *m_widParam2 = nullptr;
    QSpinBox* m_widMidiNote = nullptr;
    QSpinBox* m_midiCcSpin = nullptr;
    QPushButton* m_btnMidiLearn = nullptr;
    QComboBox* m_gpioModeCombo = nullptr;
    QSpinBox *m_gpioPinA = nullptr, *m_gpioPinB = nullptr;
    QLineEdit* m_hwNoteEdit = nullptr;

    QWidget* buildTopToolbar() {
        auto* bar = new QWidget(this);
        bar->setStyleSheet("background-color: #1E293B; border-bottom: 2px solid #0284C7;");
        auto* lay = new QHBoxLayout(bar);
        lay->setContentsMargins(10, 5, 10, 5);

        lay->addWidget(new QLabel("Synth Name:"));
        m_edSynthName = new QLineEdit(QString::fromStdString(m_engine->synthName), bar);
        m_edSynthName->setFixedWidth(185);
        connect(m_edSynthName, &QLineEdit::textChanged, this, [this](const QString& s) {
            m_engine->synthName = s.toStdString();
            m_canvas->update();
        });
        lay->addWidget(m_edSynthName);

        lay->addWidget(new QLabel("Template:"));
        auto* presetCombo = new QComboBox(bar);
        presetCombo->addItem("0: Chicago M1 Organ + 8-Bit Sampler + House Drums");
        presetCombo->addItem("1: Vintage 1970s Subtractive Analog Synth");
        presetCombo->addItem("2: Amiga 8-Bit Sampler Groovebox");
        presetCombo->addItem("3: 303 Slide Sequencer + Drum Machine");
        connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            m_engine->loadPresetTemplate(idx);
            m_edSynthName->setText(QString::fromStdString(m_engine->synthName));
            syncSlidersFromEngine();
            refreshBottomRoutingTable();
            m_canvas->update();
        });
        lay->addWidget(presetCombo);

        lay->addWidget(new QLabel("Screen:"));
        auto* profCombo = new QComboBox(bar);
        profCombo->addItem("Profile 0: 800x480 (7\" Touch)");
        profCombo->addItem("Profile 1: 320x240 (SPI Touch)");
        profCombo->addItem("Profile 2: 128x64 (OLED)");
        connect(profCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            m_canvas->setProfile(idx);
            refreshBottomRoutingTable();
        });
        lay->addWidget(profCombo);

        auto* btnMode = new QPushButton("Mode: DESIGN LAYOUT (Switch to LIVE PLAY)", bar);
        btnMode->setStyleSheet("background-color: #B45309; color: #FFFFFF;");
        connect(btnMode, &QPushButton::clicked, this, [this, btnMode]() {
            m_canvas->designMode = !m_canvas->designMode;
            if (m_canvas->designMode) {
                btnMode->setText("Mode: DESIGN LAYOUT (Switch to LIVE PLAY)");
                btnMode->setStyleSheet("background-color: #B45309; color: #FFFFFF;");
            } else {
                btnMode->setText("Mode: LIVE PLAY & TEST (Switch to DESIGN)");
                btnMode->setStyleSheet("background-color: #15803D; color: #FFFFFF;");
            }
            m_canvas->update();
        });
        lay->addWidget(btnMode);

        lay->addStretch(1);

		auto* btnLoadH = new QPushButton("Load synth_config.h", bar);
        btnLoadH->setStyleSheet("background-color: #0D9488; color: #FFFFFF; padding: 6px 14px;");
        connect(btnLoadH, &QPushButton::clicked, this, &SynthStudioWindow::loadHeaderDialog);
        lay->addWidget(btnLoadH);

        auto* btnExportH = new QPushButton("Export synth_config.h (ESP32 Firmware Header)", bar);

		
        auto* btnExportH = new QPushButton("Export synth_config.h (ESP32 Firmware Header)", bar);
        btnExportH->setStyleSheet("background-color: #0284C7; color: #FFFFFF; padding: 6px 14px;");
        connect(btnExportH, &QPushButton::clicked, this, &SynthStudioWindow::exportHeaderDialog);
        lay->addWidget(btnExportH);

        return bar;
    }


    // LEFT MODULE RACK (6 TABS INCLUDING 8-BIT SAMPLER, DRUM MACHINE & ARP)

    QWidget* buildLeftModuleRack() {
        auto* tabs = new QTabWidget(this);
        m_paramSliders.assign(PARAM_COUNT, nullptr);
        m_paramValLabels.assign(PARAM_COUNT, nullptr);

        auto makeCategoryTab = [&](const std::vector<std::string>& cats) {
            auto* scroll = new QScrollArea(tabs);
            scroll->setWidgetResizable(true);
            auto* container = new QWidget(scroll);
            auto* vLay = new QVBoxLayout(container);

            for (const auto& cat : cats) {
                auto* grp = new QGroupBox(QString::fromStdString(cat), container);
                auto* gLay = new QGridLayout(grp);
                int row = 0;
                for (int i = 0; i < PARAM_COUNT; ++i) {
                    if (m_engine->params[i].category != cat) continue;
                    const auto& p = m_engine->params[i];
                    auto* lbl = new QLabel(QString::fromStdString(p.name), grp);
                    auto* sld = new QSlider(Qt::Horizontal, grp);
                    sld->setRange(0, 1000);
                    sld->setValue(static_cast<int>(p.getNormalized() * 1000.0f));
                    auto* valLbl = new QLabel(QString::number(p.curVal, 'f', 2), grp);
                    valLbl->setFixedWidth(46);

                    m_paramSliders[i] = sld;
                    m_paramValLabels[i] = valLbl;

                    connect(sld, &QSlider::valueChanged, this, [this, i, valLbl](int v) {
                        m_engine->params[i].setNormalized(v / 1000.0f);
                        valLbl->setText(QString::number(m_engine->params[i].curVal, 'f', 2));
                        m_canvas->update();
                    });

                    gLay->addWidget(lbl, row, 0);
                    gLay->addWidget(sld, row, 1);
                    gLay->addWidget(valLbl, row, 2);
                    row++;
                }
                vLay->addWidget(grp);
            }
            vLay->addStretch(1);
            scroll->setWidget(container);
            return scroll;
        };

        tabs->addTab(makeCategoryTab({"Oscillator 1", "Oscillator 2", "Filter"}), "1. Osc & Filter");
        tabs->addTab(build8BitSamplerTab(), "2. 8-Bit Sampler & In");
        tabs->addTab(buildDrumMachineTab(), "3. Drum Machine");
        tabs->addTab(buildArpeggiatorTab(), "4. Arp Programmer");
        tabs->addTab(makeCategoryTab({"Envelopes", "LFO & Expression", "Master FX"}), "5. Env / LFO / FX");
        tabs->addTab(buildModMatrixAndAstTab(), "6. Mod & AST Code");

        return tabs;
    }

    // 8-BIT SAMPLER WITH LIVE AUDIO-IN & WAV LOADER
    QWidget* build8BitSamplerTab() {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        auto* w = new QWidget(scroll);
        auto* vLay = new QVBoxLayout(w);

        auto* waveGrp = new QGroupBox("8-Bit Signed PCM Waveform & Live Audio-In Recorder", w);
        auto* wLay = new QVBoxLayout(waveGrp);
        m_samplerWaveView = new SamplerWaveformPreview(m_engine, waveGrp);
        wLay->addWidget(m_samplerWaveView);

        auto* meterRow = new QHBoxLayout();
        meterRow->addWidget(new QLabel("Live Audio-In Level (Mic / Line / ESP32 ADC):", waveGrp));
        m_audioInMeter = new QProgressBar(waveGrp);
        m_audioInMeter->setRange(0, 100);
        m_audioInMeter->setValue(0);
        meterRow->addWidget(m_audioInMeter, 1);
        wLay->addLayout(meterRow);

        auto* btnRow = new QHBoxLayout();
        auto* btnRec = new QPushButton("Record Audio-In (8-Bit)", waveGrp);
        btnRec->setStyleSheet("background-color: #B91C1C; color: white;");
        connect(btnRec, &QPushButton::clicked, this, [this, btnRec]() {
            if (!m_engine->isRecordingSample) {
                m_engine->startRecordingAudioIn();
                btnRec->setText("Stop Recording");
            } else {
                m_engine->stopRecordingAudioIn();
                btnRec->setText("Record Audio-In (8-Bit)");
            }
        });
        btnRow->addWidget(btnRec);

        auto* btnLoadWav = new QPushButton("Load .WAV File (Auto 8-Bit)", waveGrp);
        connect(btnLoadWav, &QPushButton::clicked, this, &SynthStudioWindow::loadWavInto8BitSampler);
        btnRow->addWidget(btnLoadWav);

        auto* btnBakeAst = new QPushButton("Bake AST Formula -> 8-Bit", waveGrp);
        connect(btnBakeAst, &QPushButton::clicked, this, [this]() {
            m_engine->bakeFormulaTo8BitSample(m_engine->customOsc1Formula);
            if (m_samplerWaveView) m_samplerWaveView->update();
        });
        btnRow->addWidget(btnBakeAst);

        auto* btnTrig = new QPushButton("Audition C4", waveGrp);
        connect(btnTrig, &QPushButton::pressed, this, [this]() { m_engine->noteOn(60, 115); });
        connect(btnTrig, &QPushButton::released, this, [this]() { m_engine->noteOff(60); });
        btnRow->addWidget(btnTrig);

        wLay->addLayout(btnRow);
        vLay->addWidget(waveGrp);

        // Sampler Parameters Group
        auto* paramGrp = new QGroupBox("8-Bit Sampler & Audio-In Filter Thru Parameters", w);
        auto* gLay = new QGridLayout(paramGrp);
        int row = 0;
        for (int i = 0; i < PARAM_COUNT; ++i) {
            if (m_engine->params[i].category != "8-Bit Sampler") continue;
            const auto& p = m_engine->params[i];
            auto* lbl = new QLabel(QString::fromStdString(p.name), paramGrp);
            auto* sld = new QSlider(Qt::Horizontal, paramGrp);
            sld->setRange(0, 1000);
            sld->setValue(static_cast<int>(p.getNormalized() * 1000.0f));
            auto* valLbl = new QLabel(QString::number(p.curVal, 'f', 2), paramGrp);
            valLbl->setFixedWidth(46);

            m_paramSliders[i] = sld;
            m_paramValLabels[i] = valLbl;

            connect(sld, &QSlider::valueChanged, this, [this, i, valLbl](int v) {
                m_engine->params[i].setNormalized(v / 1000.0f);
                valLbl->setText(QString::number(m_engine->params[i].curVal, 'f', 2));
                if (m_samplerWaveView) m_samplerWaveView->update();
                m_canvas->update();
            });
            gLay->addWidget(lbl, row, 0);
            gLay->addWidget(sld, row, 1);
            gLay->addWidget(valLbl, row, 2);
            row++;
        }
        vLay->addWidget(paramGrp);
        vLay->addStretch(1);
        scroll->setWidget(w);
        return scroll;
    }

    // 6-TRACK DRUM MACHINE SEQUENCER & MIXER
    QWidget* buildDrumMachineTab() {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        auto* w = new QWidget(scroll);
        auto* vLay = new QVBoxLayout(w);

        // Top Transport & Drum Bus Controls
        auto* topGrp = new QGroupBox("Drum Machine Master Transport & 8-Bit Bus Crunch", w);
        auto* topLay = new QHBoxLayout(topGrp);
        auto* btnRun = new QPushButton("START / STOP DRUMS", topGrp);
        btnRun->setStyleSheet("background-color: #15803D; color: white; padding: 6px 12px;");
        connect(btnRun, &QPushButton::clicked, this, [this]() {
            float cur = m_engine->params[P_DRUM_ENABLE].curVal;
            m_engine->params[P_DRUM_ENABLE].curVal = (cur > 0.5f) ? 0.0f : 1.0f;
            syncSlidersFromEngine();
            m_canvas->update();
        });
        topLay->addWidget(btnRun);

        auto* btnClear = new QPushButton("Clear Grid", topGrp);
        connect(btnClear, &QPushButton::clicked, this, [this]() {
            for (auto& dt : m_engine->drumTracks) std::memset(dt.steps, 0, sizeof(dt.steps));
            refreshDrumGridButtons();
            m_canvas->update();
        });
        topLay->addWidget(btnClear);

        auto* btnRandom = new QPushButton("Generate House Groove", topGrp);
        connect(btnRandom, &QPushButton::clicked, this, [this]() {
            m_engine->initDefaultArpAndDrums();
            for (int s = 0; s < 16; ++s) {
                m_engine->drumTracks[3].steps[s] = (std::rand() % 100) < 45;
                m_engine->drumTracks[5].steps[s] = (std::rand() % 100) < 30;
            }
            refreshDrumGridButtons();
            m_canvas->update();
        });
        topLay->addWidget(btnRandom);
        vLay->addWidget(topGrp);

        // 6x16 Interactive Step Matrix + Per-Track Tune/Decay/Level
        auto* seqGrp = new QGroupBox("6-Track x 16-Step Drum Sequencer Matrix (Click Steps to Toggle)", w);
        auto* grid = new QGridLayout(seqGrp);
        grid->setSpacing(3);

        grid->addWidget(new QLabel("Track / Audition"), 0, 0);
        grid->addWidget(new QLabel("Mute"), 0, 1);
        for (int s = 0; s < 16; ++s) {
            auto* sl = new QLabel(QString::number(s + 1), seqGrp);
            sl->setAlignment(Qt::AlignCenter);
            grid->addWidget(sl, 0, 2 + s);
        }
        grid->addWidget(new QLabel("Tune"), 0, 18);
        grid->addWidget(new QLabel("Decay"), 0, 19);

        for (int t = 0; t < ModularSynthEngine::NUM_DRUM_TRACKS; ++t) {
            auto* btnTrig = new QPushButton(QString::fromStdString(m_engine->drumTracks[t].name), seqGrp);
            connect(btnTrig, &QPushButton::clicked, this, [this, t]() { m_engine->triggerDrumTrack(t, 1.0f); });
            grid->addWidget(btnTrig, t + 1, 0);

            auto* chkMute = new QCheckBox(seqGrp);
            chkMute->setChecked(m_engine->drumTracks[t].muted);
            connect(chkMute, &QCheckBox::toggled, this, [this, t](bool m) { m_engine->drumTracks[t].muted = m; });
            grid->addWidget(chkMute, t + 1, 1);

            for (int s = 0; s < 16; ++s) {
                auto* stepBtn = new QPushButton(seqGrp);
                stepBtn->setFixedSize(22, 24);
                stepBtn->setCheckable(true);
                stepBtn->setChecked(m_engine->drumTracks[t].steps[s]);
                updateDrumStepBtnStyle(stepBtn, m_engine->drumTracks[t].steps[s], (s % 4 == 0));
                m_drumStepBtns[t][s] = stepBtn;

                connect(stepBtn, &QPushButton::toggled, this, [this, t, s, stepBtn](bool on) {
                    m_engine->drumTracks[t].steps[s] = on;
                    updateDrumStepBtnStyle(stepBtn, on, (s % 4 == 0));
                    m_canvas->update();
                });
                grid->addWidget(stepBtn, t + 1, 2 + s);
            }

            auto* tuneSld = new QSlider(Qt::Horizontal, seqGrp);
            tuneSld->setRange(0, 100);
            tuneSld->setValue(static_cast<int>(m_engine->drumTracks[t].tune * 100.0f));
            tuneSld->setFixedWidth(55);
            connect(tuneSld, &QSlider::valueChanged, this, [this, t](int v) { m_engine->drumTracks[t].tune = v / 100.0f; });
            grid->addWidget(tuneSld, t + 1, 18);

            auto* decSld = new QSlider(Qt::Horizontal, seqGrp);
            decSld->setRange(0, 100);
            decSld->setValue(static_cast<int>(m_engine->drumTracks[t].decay * 100.0f));
            decSld->setFixedWidth(55);
            connect(decSld, &QSlider::valueChanged, this, [this, t](int v) { m_engine->drumTracks[t].decay = v / 100.0f; });
            grid->addWidget(decSld, t + 1, 19);
        }
        vLay->addWidget(seqGrp);

        // Master Clock / Swing / Drum Level Sliders
        auto* clkGrp = new QGroupBox("Master Clock, Swing & Drum Bus Parameters", w);
        auto* cLay = new QGridLayout(clkGrp);
        int row = 0;
        for (int i = 0; i < PARAM_COUNT; ++i) {
            if (m_engine->params[i].category != "Arp & Drums") continue;
            const auto& p = m_engine->params[i];
            auto* lbl = new QLabel(QString::fromStdString(p.name), clkGrp);
            auto* sld = new QSlider(Qt::Horizontal, clkGrp);
            sld->setRange(0, 1000);
            sld->setValue(static_cast<int>(p.getNormalized() * 1000.0f));
            auto* valLbl = new QLabel(QString::number(p.curVal, 'f', 2), clkGrp);
            valLbl->setFixedWidth(46);

            m_paramSliders[i] = sld;
            m_paramValLabels[i] = valLbl;

            connect(sld, &QSlider::valueChanged, this, [this, i, valLbl](int v) {
                m_engine->params[i].setNormalized(v / 1000.0f);
                valLbl->setText(QString::number(m_engine->params[i].curVal, 'f', 2));
                m_canvas->update();
            });
            cLay->addWidget(lbl, row, 0);
            cLay->addWidget(sld, row, 1);
            cLay->addWidget(valLbl, row, 2);
            row++;
        }
        vLay->addWidget(clkGrp);
        vLay->addStretch(1);
        scroll->setWidget(w);
        return scroll;
    }

    // TAB 4: DEDICATED 16-STEP ARPEGGIATOR & SEQUENCER PROGRAMMER
    QWidget* buildArpeggiatorTab() {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        auto* w = new QWidget(scroll);
        auto* vLay = new QVBoxLayout(w);

        auto* topGrp = new QGroupBox("Arpeggiator & 16-Step Sequencer Pattern Programmer", w);
        auto* hLay = new QHBoxLayout(topGrp);
        hLay->addWidget(new QLabel("Mode:"));
        auto* modeCombo = new QComboBox(topGrp);
        modeCombo->addItems({"0: Off (Live Polyphonic)", "1: Arp Up", "2: Arp Down", "3: Arp Up/Down", "4: Arp Random", "5: 16-Step Sequencer"});
        modeCombo->setCurrentIndex(static_cast<int>(m_engine->params[P_ARP_MODE].curVal));
        connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            m_engine->params[P_ARP_MODE].curVal = static_cast<float>(idx);
            syncSlidersFromEngine();
            m_canvas->update();
        });
        hLay->addWidget(modeCombo);

        auto* btn303 = new QPushButton("Randomize 303 Pattern", topGrp);
        connect(btn303, &QPushButton::clicked, this, [this]() {
            static const int scale[] = {0, 0, 3, 5, 7, 10, 12, -12};
            for (int s = 0; s < 16; ++s) {
                m_engine->arpSteps[s].gate     = (std::rand() % 100) < 80;
                m_engine->arpSteps[s].semitone = scale[std::rand() % 8];
                m_engine->arpSteps[s].octave   = (std::rand() % 100 < 25) ? 1 : 0;
                m_engine->arpSteps[s].velocity = (std::rand() % 100 < 35) ? 1.0f : 0.72f;
                m_engine->arpSteps[s].slide    = (std::rand() % 100) < 30;
            }
            refreshArpStepControls();
            m_canvas->update();
        });
        hLay->addWidget(btn303);
        vLay->addWidget(topGrp);

        auto* gridGrp = new QGroupBox("16-Step Note Offset, Octave, Gate, Velocity & 303 Slide/Tie", w);
        auto* grid = new QGridLayout(gridGrp);
        grid->setSpacing(4);

        grid->addWidget(new QLabel("Step"), 0, 0);
        grid->addWidget(new QLabel("Gate"), 0, 1);
        grid->addWidget(new QLabel("Semitone (-24..+24)"), 0, 2);
        grid->addWidget(new QLabel("Octave"), 0, 3);
        grid->addWidget(new QLabel("Velocity"), 0, 4);
        grid->addWidget(new QLabel("Slide/Tie"), 0, 5);

        for (int s = 0; s < 16; ++s) {
            grid->addWidget(new QLabel(QString("Step %1").arg(s + 1)), s + 1, 0);

            auto* chkGate = new QCheckBox(gridGrp);
            chkGate->setChecked(m_engine->arpSteps[s].gate);
            m_arpGateChks[s] = chkGate;
            connect(chkGate, &QCheckBox::toggled, this, [this, s](bool g) {
                m_engine->arpSteps[s].gate = g;
                m_canvas->update();
            });
            grid->addWidget(chkGate, s + 1, 1);

            auto* spSemi = new QSpinBox(gridGrp);
            spSemi->setRange(-24, 24);
            spSemi->setValue(m_engine->arpSteps[s].semitone);
            m_arpSemiSpins[s] = spSemi;
            connect(spSemi, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, s](int val) {
                m_engine->arpSteps[s].semitone = val;
                m_canvas->update();
            });
            grid->addWidget(spSemi, s + 1, 2);

            auto* spOct = new QSpinBox(gridGrp);
            spOct->setRange(-2, 2);
            spOct->setValue(m_engine->arpSteps[s].octave);
            m_arpOctSpins[s] = spOct;
            connect(spOct, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, s](int val) {
                m_engine->arpSteps[s].octave = val;
            });
            grid->addWidget(spOct, s + 1, 3);

            auto* sldVel = new QSlider(Qt::Horizontal, gridGrp);
            sldVel->setRange(10, 100);
            sldVel->setValue(static_cast<int>(m_engine->arpSteps[s].velocity * 100.0f));
            m_arpVelSliders[s] = sldVel;
            connect(sldVel, &QSlider::valueChanged, this, [this, s](int val) {
                m_engine->arpSteps[s].velocity = val / 100.0f;
            });
            grid->addWidget(sldVel, s + 1, 4);

            auto* chkSlide = new QCheckBox("Slide", gridGrp);
            chkSlide->setChecked(m_engine->arpSteps[s].slide);
            m_arpSlideChks[s] = chkSlide;
            connect(chkSlide, &QCheckBox::toggled, this, [this, s](bool sl) {
                m_engine->arpSteps[s].slide = sl;
            });
            grid->addWidget(chkSlide, s + 1, 5);
        }

        vLay->addWidget(gridGrp);
        vLay->addStretch(1);
        scroll->setWidget(w);
        return scroll;
    }

    QWidget* buildModMatrixAndAstTab() {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        auto* w = new QWidget(scroll);
        auto* lay = new QVBoxLayout(w);

        auto* modGrp = new QGroupBox("8-Slot Modulation Matrix (Source -> Parameter -> Amount)", w);
        auto* modGrid = new QGridLayout(modGrp);
        const QStringList srcNames = {"Off", "LFO 1", "LFO 2", "Mod Env", "Filter Env", "Velocity", "KeyTrack", "Bellows ADC", "ModWheel CC1"};

        for (int s = 0; s < 8; ++s) {
            auto* chk = new QCheckBox(QString("Slot %1").arg(s + 1), modGrp);
            chk->setChecked(m_engine->modMatrix[s].enabled);
            auto* srcBox = new QComboBox(modGrp);
            srcBox->addItems(srcNames);
            srcBox->setCurrentIndex(m_engine->modMatrix[s].source);

            auto* dstBox = new QComboBox(modGrp);
            for (int p = 0; p < PARAM_COUNT; ++p) {
                dstBox->addItem(QString::fromStdString(m_engine->params[p].shortName), p);
            }
            dstBox->setCurrentIndex(m_engine->modMatrix[s].destParam);

            auto* amtSpin = new QDoubleSpinBox(modGrp);
            amtSpin->setRange(-1.0, 1.0);
            amtSpin->setSingleStep(0.05);
            amtSpin->setValue(m_engine->modMatrix[s].amount);

            connect(chk, &QCheckBox::toggled, this, [this, s](bool en) { m_engine->modMatrix[s].enabled = en; });
            connect(srcBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, s](int idx) { m_engine->modMatrix[s].source = idx; });
            connect(dstBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, s](int idx) { m_engine->modMatrix[s].destParam = idx; });
            connect(amtSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, s](double v) { m_engine->modMatrix[s].amount = static_cast<float>(v); });

            modGrid->addWidget(chk, s, 0);
            modGrid->addWidget(srcBox, s, 1);
            modGrid->addWidget(dstBox, s, 2);
            modGrid->addWidget(amtSpin, s, 3);
        }
        lay->addWidget(modGrp);

        auto* astGrp = new QGroupBox("Custom LMMS Xpressive AST Formula & Cabinet Transfer Function", w);
        auto* astLay = new QVBoxLayout(astGrp);
        astLay->addWidget(new QLabel("Oscillator AST Equation (Active when Osc1Type = 9, or click 'Bake AST -> 8-Bit' in Sampler tab):"));
        auto* astEdit = new QTextEdit(QString::fromStdString(m_engine->customOsc1Formula), astGrp);
        astEdit->setFixedHeight(68);
        astLay->addWidget(astEdit);

        astLay->addWidget(new QLabel("Cabinet Transfer Function H(freq, s, j):"));
        auto* tfEdit = new QTextEdit(QString::fromStdString(m_engine->customCabinetFormula), astGrp);
        tfEdit->setFixedHeight(68);
        astLay->addWidget(tfEdit);

        auto* btnCompile = new QPushButton("Compile Equations into Live DSP Engine", astGrp);
        connect(btnCompile, &QPushButton::clicked, this, [this, astEdit, tfEdit]() {
            m_engine->customOsc1Formula = astEdit->toPlainText().toStdString();
            m_engine->customCabinetFormula = tfEdit->toPlainText().toStdString();
            m_engine->recompileCustomAst();
        });
        astLay->addWidget(btnCompile);
        lay->addWidget(astGrp);
        lay->addStretch(1);

        scroll->setWidget(w);
        return scroll;
    }


    // CENTER PANE: WIDGET PALETTE + ESP32 TOUCHSCREEN DESIGNER

    QWidget* buildCenterCanvasPane() {
        auto* container = new QWidget(this);
        auto* vLay = new QVBoxLayout(container);
        vLay->setContentsMargins(2, 2, 2, 2);

        auto* toolGrp = new QGroupBox("Add On-Screen UI Widget to Current ESP32 Display Profile", container);
        auto* hLay = new QHBoxLayout(toolGrp);
        hLay->setSpacing(4);
        static const char* wNames[] = {
            "+Knob", "+VFader", "+HSlider", "+Toggle", "+Pad",
            "+Select", "+XY", "+ADSR", "+Scope", "+16Seq", "+Ribbon", "+Piano", "+Drums", "+8BitWave"
        };
        for (int wt = 0; wt < 14; ++wt) {
            auto* b = new QPushButton(wNames[wt], toolGrp);
            connect(b, &QPushButton::clicked, this, [this, wt]() {
                auto& list = m_engine->profileWidgets[m_canvas->activeProfile];
                UiWidgetSpec nw;
                nw.uid = static_cast<int>(list.size()) + 1;
                nw.type = static_cast<WidgetType>(wt);
                nw.label = wNames[wt] + 1;
                nw.x = 20; nw.y = 40;
                nw.w = (wt == W_PIANO_KEYBOARD) ? 300 : (wt == W_DRUM_GRID ? 200 : 80);
                nw.h = (wt == W_PIANO_KEYBOARD) ? 90  : (wt == W_DRUM_GRID ? 90  : 72);
                list.push_back(nw);
                m_canvas->selectedWidgetIdx = static_cast<int>(list.size()) - 1;
                populateInspector(m_canvas->selectedWidgetIdx);
                refreshBottomRoutingTable();
                m_canvas->update();
            });
            hLay->addWidget(b);
        }
        auto* btnDel = new QPushButton("Del", toolGrp);
        btnDel->setStyleSheet("background-color: #991B1B;");
        connect(btnDel, &QPushButton::clicked, this, [this]() {
            auto& list = m_engine->profileWidgets[m_canvas->activeProfile];
            int idx = m_canvas->selectedWidgetIdx;
            if (idx >= 0 && idx < static_cast<int>(list.size())) {
                list.erase(list.begin() + idx);
                m_canvas->selectedWidgetIdx = -1;
                populateInspector(-1);
                refreshBottomRoutingTable();
                m_canvas->update();
            }
        });
        hLay->addWidget(btnDel);
        vLay->addWidget(toolGrp);

        auto* canvasScroll = new QScrollArea(container);
        canvasScroll->setWidgetResizable(true);
        canvasScroll->setAlignment(Qt::AlignCenter);
        m_canvas = new Esp32ScreenCanvas(m_engine, canvasScroll);
        m_canvas->onWidgetSelected = [this](int idx) { populateInspector(idx); };
        m_canvas->onParameterChangedLive = [this]() { syncSlidersFromEngine(); };
        canvasScroll->setWidget(m_canvas);
        vLay->addWidget(canvasScroll, 1);

        return container;
    }


    // RIGHT PANE: SELECTED WIDGET & PARAMETER INSPECTOR

    QWidget* buildRightInspectorPane() {
        auto* container = new QWidget(this);
        auto* vLay = new QVBoxLayout(container);

        auto* widGrp = new QGroupBox("Selected On-Screen Widget Inspector", container);
        auto* form = new QFormLayout(widGrp);

        m_widTypeCombo = new QComboBox(widGrp);
        m_widTypeCombo->addItems({
            "Rotary Knob", "Vertical Fader", "Horizontal Slider", "Toggle Button",
            "Momentary Pad", "Cycle Selector", "2D XY Morph Pad", "ADSR Graph",
            "Live Oscilloscope", "16-Step Arp Grid", "Touch Ribbon", "Piano Keyboard",
            "6x16 Drum Grid", "8-Bit Sampler Waveform"
        });
        m_widLabelEdit = new QLineEdit(widGrp);
        m_widX = new QSpinBox(widGrp); m_widX->setRange(0, 800);
        m_widY = new QSpinBox(widGrp); m_widY->setRange(0, 480);
        m_widW = new QSpinBox(widGrp); m_widW->setRange(16, 800);
        m_widH = new QSpinBox(widGrp); m_widH->setRange(12, 480);

        m_widParam1 = new QComboBox(widGrp);
        m_widParam2 = new QComboBox(widGrp);
        for (int i = 0; i < PARAM_COUNT; ++i) {
            QString label = QString("%1: %2").arg(i).arg(QString::fromStdString(m_engine->params[i].name));
            m_widParam1->addItem(label, i);
            m_widParam2->addItem(label, i);
        }
        m_widMidiNote = new QSpinBox(widGrp);
        m_widMidiNote->setRange(12, 108);
        m_widMidiNote->setValue(60);

        auto* btnColor = new QPushButton("Pick Accent Color (RGB565)", widGrp);
        connect(btnColor, &QPushButton::clicked, this, [this]() {
            auto* w = getSelectedWidget();
            if (!w) return;
            QColor c = QColorDialog::getColor(Esp32ScreenCanvas::from565(w->fgCol565), this, "Select Widget Color");
            if (c.isValid()) {
                w->fgCol565 = Esp32ScreenCanvas::to565(c);
                m_canvas->update();
            }
        });

        form->addRow("Widget Type:", m_widTypeCombo);
        form->addRow("Display Label:", m_widLabelEdit);
        form->addRow("X / Y Pos:", m_widX);
        form->addRow("Y Position:", m_widY);
        form->addRow("Width (px):", m_widW);
        form->addRow("Height (px):", m_widH);
        form->addRow("Primary Param:", m_widParam1);
        form->addRow("Second Param (XY):", m_widParam2);
        form->addRow("MIDI Note:", m_widMidiNote);
        form->addRow("Color Theme:", btnColor);
        vLay->addWidget(widGrp);

        auto applyWidEdits = [this]() {
            if (m_updatingInspector) return;
            auto* w = getSelectedWidget();
            if (!w) return;
            w->type = static_cast<WidgetType>(m_widTypeCombo->currentIndex());
            w->label = m_widLabelEdit->text().toStdString();
            w->x = m_widX->value();
            w->y = m_widY->value();
            w->w = m_widW->value();
            w->h = m_widH->value();
            w->paramId1 = m_widParam1->currentIndex();
            w->paramId2 = m_widParam2->currentIndex();
            w->midiNote = m_widMidiNote->value();
            populateHardwareBindingForParam(w->paramId1);
            refreshBottomRoutingTable();
            m_canvas->update();
        };

        connect(m_widTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyWidEdits);
        connect(m_widLabelEdit, &QLineEdit::textChanged, this, applyWidEdits);
        connect(m_widX, QOverload<int>::of(&QSpinBox::valueChanged), this, applyWidEdits);
        connect(m_widY, QOverload<int>::of(&QSpinBox::valueChanged), this, applyWidEdits);
        connect(m_widW, QOverload<int>::of(&QSpinBox::valueChanged), this, applyWidEdits);
        connect(m_widH, QOverload<int>::of(&QSpinBox::valueChanged), this, applyWidEdits);
        connect(m_widParam1, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyWidEdits);
        connect(m_widParam2, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyWidEdits);
        connect(m_widMidiNote, QOverload<int>::of(&QSpinBox::valueChanged), this, applyWidEdits);

        auto* hwGrp = new QGroupBox("MIDI CC, ESP32 GPIO & Build Note (Primary Param)", container);
        auto* hwForm = new QFormLayout(hwGrp);

        m_midiCcSpin = new QSpinBox(hwGrp);
        m_midiCcSpin->setRange(-1, 127);
        m_btnMidiLearn = new QPushButton("MIDI Learn CC", hwGrp);
        connect(m_btnMidiLearn, &QPushButton::clicked, this, [this]() {
            int pid = m_widParam1->currentIndex();
            m_engine->midiLearnParamId = pid;
            m_btnMidiLearn->setText("Listening for MIDI CC...");
        });

        m_gpioModeCombo = new QComboBox(hwGrp);
        m_gpioModeCombo->addItems({
            "0: Unassigned",
            "1: ESP32 ADC Potentiometer",
            "2: Digital Button (Toggle)",
            "3: Digital Button (Momentary)",
            "4: Rotary Encoder (Pin A/B)",
            "5: Bellows / Hall Sensor (ADC)"
        });
        m_gpioPinA = new QSpinBox(hwGrp); m_gpioPinA->setRange(-1, 48);
        m_gpioPinB = new QSpinBox(hwGrp); m_gpioPinB->setRange(-1, 48);
        m_hwNoteEdit = new QLineEdit(hwGrp);
        m_hwNoteEdit->setPlaceholderText("e.g. Panel Pot #3 on GPIO 4, or Foot Pedal");

        auto applyHwEdits = [this]() {
            if (m_updatingInspector) return;
            int pid = std::clamp(m_widParam1->currentIndex(), 0, PARAM_COUNT - 1);
            auto& p = m_engine->params[pid];
            p.midiCc = m_midiCcSpin->value();
            p.gpioMode = m_gpioModeCombo->currentIndex();
            p.gpioPinA = m_gpioPinA->value();
            p.gpioPinB = m_gpioPinB->value();
            p.hardwareNote = m_hwNoteEdit->text().toStdString();
            refreshBottomRoutingTable();
        };
        connect(m_midiCcSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, applyHwEdits);
        connect(m_gpioModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyHwEdits);
        connect(m_gpioPinA, QOverload<int>::of(&QSpinBox::valueChanged), this, applyHwEdits);
        connect(m_gpioPinB, QOverload<int>::of(&QSpinBox::valueChanged), this, applyHwEdits);
        connect(m_hwNoteEdit, &QLineEdit::textChanged, this, applyHwEdits);

        hwForm->addRow("MIDI CC (-1=Off):", m_midiCcSpin);
        hwForm->addRow("Live Learn:", m_btnMidiLearn);
        hwForm->addRow("ESP32 GPIO Mode:", m_gpioModeCombo);
        hwForm->addRow("GPIO Pin A / B:", m_gpioPinA);
        hwForm->addRow("GPIO Pin B (Enc):", m_gpioPinB);
        hwForm->addRow("Hardware Note:", m_hwNoteEdit);
        vLay->addWidget(hwGrp);



	auto* pinGrp = new QGroupBox("ESP32 Board Pinout & Default Audio Out", container);
        auto* pinForm = new QFormLayout(pinGrp);

        auto* spBclk = new QSpinBox(pinGrp); spBclk->setRange(-1, 48); spBclk->setValue(m_engine->boardPins.i2sBclk);
        auto* spLrck = new QSpinBox(pinGrp); spLrck->setRange(-1, 48); spLrck->setValue(m_engine->boardPins.i2sLrck);
        auto* spDout = new QSpinBox(pinGrp); spDout->setRange(-1, 48); spDout->setValue(m_engine->boardPins.i2sDout);
        auto* spSda  = new QSpinBox(pinGrp); spSda->setRange(-1, 48);  spSda->setValue(m_engine->boardPins.i2cSda);
        auto* spScl  = new QSpinBox(pinGrp); spScl->setRange(-1, 48);  spScl->setValue(m_engine->boardPins.i2cScl);
        auto* cbAud  = new QComboBox(pinGrp);
        cbAud->addItems({"0: USB Audio Class", "1: I2S External DAC", "2: Wi-Fi Stream"});
        cbAud->setCurrentIndex(std::clamp(m_engine->boardPins.audioOutMode, 0, 2));

        connect(spBclk, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { m_engine->boardPins.i2sBclk = v; });
        connect(spLrck, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { m_engine->boardPins.i2sLrck = v; });
        connect(spDout, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { m_engine->boardPins.i2sDout = v; });
        connect(spSda,  QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { m_engine->boardPins.i2cSda  = v; });
        connect(spScl,  QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { m_engine->boardPins.i2cScl  = v; });
        connect(cbAud,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int v) { m_engine->boardPins.audioOutMode = v; });

        pinForm->addRow("I2S BCLK Pin:", spBclk);
        pinForm->addRow("I2S LRCK Pin:", spLrck);
        pinForm->addRow("I2S DOUT Pin:", spDout);
        pinForm->addRow("I2C SDA Pin:",  spSda);
        pinForm->addRow("I2C SCL Pin:",  spScl);
        pinForm->addRow("Boot Audio Mode:", cbAud);
        vLay->addWidget(pinGrp);

        vLay->addStretch(1);
        return container;
    }


    QWidget* buildBottomHardwareRoutingMatrix() {
        auto* grp = new QGroupBox(
            "Off-Screen & On-Screen Hardware / MIDI CC / ESP32 GPIO Routing Matrix (Route knobs/sensors not shown on LCD & add build notes)",
            this);
        auto* vLay = new QVBoxLayout(grp);
        vLay->setContentsMargins(6, 10, 6, 6);

        m_routingTable = new QTableWidget(PARAM_COUNT, 8, grp);
        m_routingTable->setHorizontalHeaderLabels({
            "ID & Parameter Name", "LCD Visibility", "Current Val",
            "MIDI CC (-1=Off)", "MIDI Learn", "ESP32 GPIO Mode", "GPIO Pin A / B",
            "Hardware Wiring / Build Note (Saved to synth_config.h)"
        });
        m_routingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_routingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_routingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_routingTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_routingTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        m_routingTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        m_routingTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
        m_routingTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
        m_routingTable->verticalHeader()->setVisible(false);

        const QStringList gpioModes = {
            "0: Unassigned", "1: ADC Potentiometer", "2: Button (Toggle)",
            "3: Button (Momentary)", "4: Rotary Encoder", "5: Hall/Bellows ADC"
        };

        for (int i = 0; i < PARAM_COUNT; ++i) {
            const auto& p = m_engine->params[i];
            m_routingTable->setItem(i, 0, new QTableWidgetItem(QString("%1: %2").arg(i).arg(QString::fromStdString(p.name))));
            m_routingTable->setItem(i, 1, new QTableWidgetItem(""));
            m_routingTable->setItem(i, 2, new QTableWidgetItem(QString::number(p.curVal, 'f', 2)));

            // MIDI CC SpinBox
            auto* ccSpin = new QSpinBox(m_routingTable);
            ccSpin->setRange(-1, 127);
            ccSpin->setValue(p.midiCc);
            connect(ccSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
                if (m_updatingTable) return;
                m_engine->params[i].midiCc = v;
            });
            m_routingTable->setCellWidget(i, 3, ccSpin);

            // MIDI Learn Button per row
            auto* btnLearn = new QPushButton("Learn", m_routingTable);
            connect(btnLearn, &QPushButton::clicked, this, [this, i, btnLearn]() {
                m_engine->midiLearnParamId = i;
                btnLearn->setText("Wait CC..");
            });
            m_routingTable->setCellWidget(i, 4, btnLearn);

            // GPIO Mode Combo
            auto* modeCombo = new QComboBox(m_routingTable);
            modeCombo->addItems(gpioModes);
            modeCombo->setCurrentIndex(p.gpioMode);
            connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int m) {
                if (m_updatingTable) return;
                m_engine->params[i].gpioMode = m;
            });
            m_routingTable->setCellWidget(i, 5, modeCombo);

            // GPIO Pin A & B Widget
            auto* pinW = new QWidget(m_routingTable);
            auto* pinLay = new QHBoxLayout(pinW);
            pinLay->setContentsMargins(2, 0, 2, 0);
            auto* spA = new QSpinBox(pinW); spA->setRange(-1, 48); spA->setValue(p.gpioPinA);
            auto* spB = new QSpinBox(pinW); spB->setRange(-1, 48); spB->setValue(p.gpioPinB);
            connect(spA, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
                if (!m_updatingTable) m_engine->params[i].gpioPinA = v;
            });
            connect(spB, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, i](int v) {
                if (!m_updatingTable) m_engine->params[i].gpioPinB = v;
            });
            pinLay->addWidget(spA);
            pinLay->addWidget(spB);
            m_routingTable->setCellWidget(i, 6, pinW);

            // Free-form Note Text Box
            auto* noteEdit = new QLineEdit(QString::fromStdString(p.hardwareNote), m_routingTable);
            noteEdit->setPlaceholderText("Add note for this MIDI CC / GPIO control...");
            connect(noteEdit, &QLineEdit::textChanged, this, [this, i](const QString& txt) {
                if (!m_updatingTable) m_engine->params[i].hardwareNote = txt.toStdString();
            });
            m_routingTable->setCellWidget(i, 7, noteEdit);
        }

        vLay->addWidget(m_routingTable);
        refreshBottomRoutingTable();
        return grp;
    }


    // SYNC ROUTINES

    QPushButton* m_drumStepBtns[ModularSynthEngine::NUM_DRUM_TRACKS][16] = {{nullptr}};
    QCheckBox*   m_arpGateChks[16]   = {nullptr};
    QSpinBox*    m_arpSemiSpins[16]  = {nullptr};
    QSpinBox*    m_arpOctSpins[16]   = {nullptr};
    QSlider*     m_arpVelSliders[16] = {nullptr};
    QCheckBox*   m_arpSlideChks[16]  = {nullptr};
    bool m_updatingInspector = false;

    static void updateDrumStepBtnStyle(QPushButton* b, bool on, bool isBeat) {
        if (on) {
            b->setStyleSheet("background-color: #0284C7; border: 1px solid #38BDF8;");
        } else if (isBeat) {
            b->setStyleSheet("background-color: #334155; border: 1px solid #475569;");
        } else {
            b->setStyleSheet("background-color: #1E293B; border: 1px solid #334155;");
        }
    }

    void refreshDrumGridButtons() {
        for (int t = 0; t < ModularSynthEngine::NUM_DRUM_TRACKS; ++t) {
            for (int s = 0; s < 16; ++s) {
                if (m_drumStepBtns[t][s]) {
                    m_drumStepBtns[t][s]->blockSignals(true);
                    m_drumStepBtns[t][s]->setChecked(m_engine->drumTracks[t].steps[s]);
                    updateDrumStepBtnStyle(m_drumStepBtns[t][s], m_engine->drumTracks[t].steps[s], (s % 4 == 0));
                    m_drumStepBtns[t][s]->blockSignals(false);
                }
            }
        }
    }

    void refreshArpStepControls() {
        for (int s = 0; s < 16; ++s) {
            if (m_arpGateChks[s])   m_arpGateChks[s]->setChecked(m_engine->arpSteps[s].gate);
            if (m_arpSemiSpins[s])  m_arpSemiSpins[s]->setValue(m_engine->arpSteps[s].semitone);
            if (m_arpOctSpins[s])   m_arpOctSpins[s]->setValue(m_engine->arpSteps[s].octave);
            if (m_arpVelSliders[s]) m_arpVelSliders[s]->setValue(static_cast<int>(m_engine->arpSteps[s].velocity * 100.0f));
            if (m_arpSlideChks[s])  m_arpSlideChks[s]->setChecked(m_engine->arpSteps[s].slide);
        }
    }

    void refreshBottomRoutingTable() {
        if (!m_routingTable) return;
        m_updatingTable = true;
        int prof = m_canvas ? m_canvas->activeProfile : 0;
        const auto& wList = m_engine->profileWidgets[prof];

        for (int i = 0; i < PARAM_COUNT; ++i) {
            bool onScreen = false;
            for (const auto& w : wList) {
                if (w.paramId1 == i || (w.type == W_XY_PAD && w.paramId2 == i)) {
                    onScreen = true;
                    break;
                }
            }
            auto* visItem = m_routingTable->item(i, 1);
            if (visItem) {
                visItem->setText(onScreen ? "On-Screen Widget" : "Off-Screen (HW/MIDI)");
                visItem->setForeground(onScreen ? QColor(52, 211, 153) : QColor(251, 191, 36));
            }
            auto* valItem = m_routingTable->item(i, 2);
            if (valItem) valItem->setText(QString::number(m_engine->params[i].curVal, 'f', 2));

            if (auto* ccSpin = qobject_cast<QSpinBox*>(m_routingTable->cellWidget(i, 3))) {
                ccSpin->setValue(m_engine->params[i].midiCc);
            }
            if (auto* btnLearn = qobject_cast<QPushButton*>(m_routingTable->cellWidget(i, 4))) {
                btnLearn->setText("Learn");
            }
            if (auto* modeCombo = qobject_cast<QComboBox*>(m_routingTable->cellWidget(i, 5))) {
                modeCombo->setCurrentIndex(m_engine->params[i].gpioMode);
            }
            if (auto* noteEd = qobject_cast<QLineEdit*>(m_routingTable->cellWidget(i, 7))) {
                if (noteEd->text().toStdString() != m_engine->params[i].hardwareNote) {
                    noteEd->setText(QString::fromStdString(m_engine->params[i].hardwareNote));
                }
            }
        }
        m_updatingTable = false;
    }

    UiWidgetSpec* getSelectedWidget() {
        auto& list = m_engine->profileWidgets[m_canvas->activeProfile];
        int idx = m_canvas->selectedWidgetIdx;
        if (idx >= 0 && idx < static_cast<int>(list.size())) return &list[idx];
        return nullptr;
    }

    void populateInspector(int idx) {
        m_updatingInspector = true;
        auto& list = m_engine->profileWidgets[m_canvas->activeProfile];
        if (idx >= 0 && idx < static_cast<int>(list.size())) {
            const auto& w = list[idx];
            m_widTypeCombo->setCurrentIndex(static_cast<int>(w.type));
            m_widLabelEdit->setText(QString::fromStdString(w.label));
            m_widX->setValue(w.x);
            m_widY->setValue(w.y);
            m_widW->setValue(w.w);
            m_widH->setValue(w.h);
            m_widParam1->setCurrentIndex(w.paramId1);
            m_widParam2->setCurrentIndex(w.paramId2);
            m_widMidiNote->setValue(w.midiNote);
            populateHardwareBindingForParam(w.paramId1);
        }
        m_updatingInspector = false;
    }

    void populateHardwareBindingForParam(int paramId) {
        paramId = std::clamp(paramId, 0, PARAM_COUNT - 1);
        const auto& p = m_engine->params[paramId];
        m_midiCcSpin->setValue(p.midiCc);
        m_gpioModeCombo->setCurrentIndex(p.gpioMode);
        m_gpioPinA->setValue(p.gpioPinA);
        m_gpioPinB->setValue(p.gpioPinB);
        m_hwNoteEdit->setText(QString::fromStdString(p.hardwareNote));
    }

    void syncSlidersFromEngine() {
        for (int i = 0; i < PARAM_COUNT; ++i) {
            if (m_paramSliders[i]) {
                m_paramSliders[i]->blockSignals(true);
                m_paramSliders[i]->setValue(static_cast<int>(m_engine->params[i].getNormalized() * 1000.0f));
                m_paramSliders[i]->blockSignals(false);
            }
            if (m_paramValLabels[i]) {
                m_paramValLabels[i]->setText(QString::number(m_engine->params[i].curVal, 'f', 2));
            }
        }
    }

    void loadWavInto8BitSampler() {
        QString path = QFileDialog::getOpenFileName(this, "Load .WAV File into 8-Bit Sampler", "", "WAV Audio (*.wav)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return;
        QByteArray raw = f.readAll();
        f.close();
        if (raw.size() <= 44) return;

        // Parse basic 16-bit or 8-bit PCM WAV data chunk
        uint16_t channels = *reinterpret_cast<const uint16_t*>(raw.constData() + 22);
        uint16_t bits     = *reinterpret_cast<const uint16_t*>(raw.constData() + 34);
        const uint8_t* pcmPtr = reinterpret_cast<const uint8_t*>(raw.constData() + 44);
        int pcmBytes = raw.size() - 44;

        std::vector<int8_t> out8;
        if (bits == 16) {
            int frames = pcmBytes / (2 * std::max<int>(1, channels));
            int step = std::max(1, frames / ModularSynthEngine::MAX_SAMPLE_BYTES);
            const int16_t* s16 = reinterpret_cast<const int16_t*>(pcmPtr);
            for (int i = 0; i < frames && static_cast<int>(out8.size()) < ModularSynthEngine::MAX_SAMPLE_BYTES; i += step) {
                out8.push_back(static_cast<int8_t>(s16[i * channels] >> 8));
            }
        } else {
            int frames = pcmBytes / std::max<int>(1, channels);
            int step = std::max(1, frames / ModularSynthEngine::MAX_SAMPLE_BYTES);
            for (int i = 0; i < frames && static_cast<int>(out8.size()) < ModularSynthEngine::MAX_SAMPLE_BYTES; i += step) {
                int v = static_cast<int>(pcmPtr[i * channels]) - 128;
                out8.push_back(static_cast<int8_t>(std::clamp(v, -128, 127)));
            }
        }
        if (!out8.empty()) {
            m_engine->samplerData = std::move(out8);
            if (m_samplerWaveView) m_samplerWaveView->update();
        }
    }
void loadHeaderDialog() {
        QString fileName = QFileDialog::getOpenFileName(
            this, "Load ESP32 Firmware Configuration Header", "", "C++ Header (*.h)");
        if (fileName.isEmpty()) return;

        QFile f(fileName);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QString content = QString::fromUtf8(f.readAll());
        f.close();

        // 1. Parse Synth Name & Formulas
        auto extractStr = [&](const QString& key, std::string& target) {
            QRegularExpression re(key + R"(\s*=\s*"((?:[^"\\]|\\.)*)"\s*;)");
            auto m = re.match(content);
            if (m.hasMatch()) {
                QString val = m.captured(1);
                val.replace("\\\"", "\"").replace("\\\\", "\\");
                target = val.toStdString();
            }
        };
        extractStr("SYNTH_NAME", m_engine->synthName);
        extractStr("CUSTOM_OSC1_AST", m_engine->customOsc1Formula);
        extractStr("CUSTOM_CABINET_TF", m_engine->customCabinetFormula);
        m_edSynthName->setText(QString::fromStdString(m_engine->synthName));

        // 2. Parse Board Pins
        auto extractInt = [&](const QString& key, int& target) {
            QRegularExpression re(key + R"(\s*=\s*(-?\d+)\s*;)");
            auto m = re.match(content);
            if (m.hasMatch()) target = m.captured(1).toInt();
        };
        extractInt("DEFAULT_I2S_BCLK", m_engine->boardPins.i2sBclk);
        extractInt("DEFAULT_I2S_LRCK", m_engine->boardPins.i2sLrck);
        extractInt("DEFAULT_I2S_DOUT", m_engine->boardPins.i2sDout);
        extractInt("DEFAULT_I2C_SDA",  m_engine->boardPins.i2cSda);
        extractInt("DEFAULT_I2C_SCL",  m_engine->boardPins.i2cScl);
        extractInt("DEFAULT_AUDIO_MODE", m_engine->boardPins.audioOutMode);

        // 3. Parse PARAMS Table
        int pStart = content.indexOf("PARAMS[");
        int pEnd = (pStart >= 0) ? content.indexOf("};", pStart) : -1;
        if (pStart >= 0 && pEnd > pStart) {
            QString pBlock = content.mid(pStart, pEnd - pStart);
            QRegularExpression reParam(
                R"(\{\s*(\d+)\s*,\s*"[^"]*"\s*,\s*([-\d.]+)f\s*,\s*([-\d.]+)f\s*,\s*([-\d.]+)f\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\})");
            auto it = reParam.globalMatch(pBlock);
            while (it.hasNext()) {
                auto m = it.next();
                int id = m.captured(1).toInt();
                if (id >= 0 && id < PARAM_COUNT) {
                    auto& ep = m_engine->params[id];
                    ep.minVal       = m.captured(2).toFloat();
                    ep.maxVal       = m.captured(3).toFloat();
                    ep.curVal       = m.captured(4).toFloat();
                    ep.defaultVal   = ep.curVal;
                    ep.midiCc       = m.captured(5).toInt();
                    ep.gpioMode     = m.captured(6).toInt();
                    ep.gpioPinA     = m.captured(7).toInt();
                    ep.gpioPinB     = m.captured(8).toInt();
                    QString note    = m.captured(9);
                    note.replace("\\\"", "\"").replace("\\\\", "\\");
                    ep.hardwareNote = note.toStdString();
                }
            }
        }

        // 4. Parse ARP_STEPS
        int aStart = content.indexOf("ARP_STEPS[");
        int aEnd = (aStart >= 0) ? content.indexOf("};", aStart) : -1;
        if (aStart >= 0 && aEnd > aStart) {
            QString aBlock = content.mid(aStart, aEnd - aStart);
            QRegularExpression reArp(R"(\{\s*(true|false)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*([-\d.]+)f\s*,\s*(true|false)\s*\})");
            auto it = reArp.globalMatch(aBlock);
            int idx = 0;
            while (it.hasNext() && idx < 16) {
                auto m = it.next();
                m_engine->arpSteps[idx].gate     = (m.captured(1) == "true");
                m_engine->arpSteps[idx].semitone = m.captured(2).toInt();
                m_engine->arpSteps[idx].octave   = m.captured(3).toInt();
                m_engine->arpSteps[idx].velocity = m.captured(4).toFloat();
                m_engine->arpSteps[idx].slide    = (m.captured(5) == "true");
                idx++;
            }
        }

        // 5. Parse DRUM_TRACKS
        int dStart = content.indexOf("DRUM_TRACKS[");
        int dEnd = (dStart >= 0) ? content.indexOf("};", dStart) : -1;
        if (dStart >= 0 && dEnd > dStart) {
            QString dBlock = content.mid(dStart, dEnd - dStart);
            QRegularExpression reDrum(R"(\{\s*(true|false)\s*,\s*([-\d.]+)f\s*,\s*([-\d.]+)f\s*,\s*([-\d.]+)f\s*,\s*0x([0-9A-Fa-f]+)\s*\})");
            auto it = reDrum.globalMatch(dBlock);
            int t = 0;
            while (it.hasNext() && t < ModularSynthEngine::NUM_DRUM_TRACKS) {
                auto m = it.next();
                m_engine->drumTracks[t].muted = (m.captured(1) == "true");
                m_engine->drumTracks[t].level = m.captured(2).toFloat();
                m_engine->drumTracks[t].tune  = m.captured(3).toFloat();
                m_engine->drumTracks[t].decay = m.captured(4).toFloat();
                uint16_t mask = static_cast<uint16_t>(m.captured(5).toUInt(nullptr, 16));
                for (int s = 0; s < 16; ++s) {
                    m_engine->drumTracks[t].steps[s] = (mask & (1u << s)) != 0;
                }
                t++;
            }
        }

        //  Parse Screen Widgets for all 3 Profiles
        const char* profNames[3] = {"WIDGETS_800x480", "WIDGETS_320x240", "WIDGETS_128x64"};
        QRegularExpression reWid(
            R"(\{\s*(\d+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*\})");
        for (int prof = 0; prof < 3; ++prof) {
            int wStart = content.indexOf(QString(profNames[prof]) + "[");
            int wEnd = (wStart >= 0) ? content.indexOf("};", wStart) : -1;
            if (wStart >= 0 && wEnd > wStart) {
                QString wBlock = content.mid(wStart, wEnd - wStart);
                m_engine->profileWidgets[prof].clear();
                auto it = reWid.globalMatch(wBlock);
                int uid = 1;
                while (it.hasNext()) {
                    auto m = it.next();
                    UiWidgetSpec w;
                    w.uid        = uid++;
                    w.type       = static_cast<WidgetType>(m.captured(1).toInt());
                    QString lbl  = m.captured(2);
                    lbl.replace("\\\"", "\"").replace("\\\\", "\\");
                    w.label      = lbl.toStdString();
                    w.x          = m.captured(3).toInt();
                    w.y          = m.captured(4).toInt();
                    w.w          = m.captured(5).toInt();
                    w.h          = m.captured(6).toInt();
                    w.paramId1   = m.captured(7).toInt();
                    w.paramId2   = m.captured(8).toInt();
                    w.midiNote   = m.captured(9).toInt();
                    w.bgCol565   = static_cast<uint16_t>(m.captured(10).toUInt(nullptr, 16));
                    w.fgCol565   = static_cast<uint16_t>(m.captured(11).toUInt(nullptr, 16));
                    w.textCol565 = static_cast<uint16_t>(m.captured(12).toUInt(nullptr, 16));
                    if (w.w > 0 && w.h > 0) {
                        m_engine->profileWidgets[prof].push_back(w);
                    }
                }
            }
        }

        m_engine->recompileCustomAst();
        syncSlidersFromEngine();
        refreshDrumGridButtons();
        refreshArpStepControls();
        refreshBottomRoutingTable();
        m_canvas->selectedWidgetIdx = -1;
        populateInspector(-1);
        m_canvas->update();

        QMessageBox::information(this, "Config Loaded", "Loaded " + fileName + " into Studio!");
    }
    void exportHeaderDialog() {
        QString fileName = QFileDialog::getSaveFileName(
            this, "Export ESP32 Firmware Configuration Header", "synth_config.h", "C++ Header (*.h)");
        if (fileName.isEmpty()) return;

        QFile f(fileName);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            std::string code = m_engine->generateCppHeader();
            f.write(code.c_str(), static_cast<qint64>(code.size()));
            f.close();
            QMessageBox::information(this, "Export Complete",
                                     "Saved " + fileName + " (including 8-bit sample PCM, Drum patterns, Arp steps, and Off-Screen GPIO/MIDI Notes)!");
        }
    }
};

} // namespace SynthSDK

#endif // SYNTH_STUDIO_GUI_H
