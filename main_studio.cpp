#include <QApplication>
#include <QIODevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioDevice>
#include <vector>
#include <cstring>
#include <algorithm>

#include "synth_sdk_modules.h"
#include "synth_studio_gui.h"
#include "midireader.h"

static SynthSDK::ModularSynthEngine g_sdkEngine;

class StudioAudioBridge : public QIODevice {
public:
    explicit StudioAudioBridge(QObject* parent = nullptr) : QIODevice(parent) {
        m_format.setSampleRate(22050);
        m_format.setChannelCount(2);
        m_format.setSampleFormat(QAudioFormat::Int16);

        QAudioDevice outDev = QMediaDevices::defaultAudioOutput();
        if (!outDev.isFormatSupported(m_format)) {
            m_format = outDev.preferredFormat();
        }
        g_sdkEngine.init(static_cast<float>(m_format.sampleRate()));
        g_sdkEngine.loadPresetTemplate(0); // Start with Chicago M1 House Organ preset

        open(QIODevice::ReadWrite);

        m_sink = new QAudioSink(outDev, m_format, this);
        m_sink->setBufferSize(2048);
        m_sink->start(this);

        // Optional PC Mic/Line-In bridge for 8-Bit Sampler recording & Filter Thru
        QAudioDevice inDev = QMediaDevices::defaultAudioInput();
        if (!inDev.isNull()) {
            m_inFormat.setSampleRate(m_format.sampleRate());
            m_inFormat.setChannelCount(1);
            m_inFormat.setSampleFormat(QAudioFormat::Int16);
            if (!inDev.isFormatSupported(m_inFormat)) {
                m_inFormat = inDev.preferredFormat();
            }
            m_source = new QAudioSource(inDev, m_inFormat, this);
            m_source->setBufferSize(1024);
            m_inDevice = m_source->start();
            if (m_inDevice) {
                connect(m_inDevice, &QIODevice::readyRead, this, &StudioAudioBridge::onAudioInReady);
            }
        }
    }

    ~StudioAudioBridge() override {
        if (m_source) m_source->stop();
        if (m_sink) m_sink->stop();
        close();
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override {
        return (m_sink ? m_sink->bufferSize() : 0) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        std::memset(data, 0, maxlen);
        int channels = std::max(1, m_format.channelCount());
        bool isFloat = (m_format.sampleFormat() == QAudioFormat::Float);
        int bytesPerSample = isFloat ? sizeof(float) : sizeof(qint16);
        int frames = static_cast<int>(maxlen) / (bytesPerSample * channels);
        if (frames <= 0) return maxlen;

        m_monoScratch.resize(frames);
        g_sdkEngine.renderFramesMono(m_monoScratch.data(), frames);

        if (isFloat) {
            float* outF = reinterpret_cast<float*>(data);
            for (int i = 0; i < frames; ++i) {
                for (int c = 0; c < channels; ++c) outF[i * channels + c] = m_monoScratch[i];
            }
        } else {
            qint16* outI = reinterpret_cast<qint16*>(data);
            for (int i = 0; i < frames; ++i) {
                qint16 s = static_cast<qint16>(std::clamp(m_monoScratch[i], -1.0f, 1.0f) * 32767.0f);
                for (int c = 0; c < channels; ++c) outI[i * channels + c] = s;
            }
        }
        return maxlen;
    }

    qint64 writeData(const char*, qint64 len) override { return len; }

private:
    void onAudioInReady() {
        if (!m_inDevice) return;
        QByteArray raw = m_inDevice->readAll();
        if (raw.isEmpty()) return;

        int ch = std::max(1, m_inFormat.channelCount());
        bool isFloat = (m_inFormat.sampleFormat() == QAudioFormat::Float);
        int bps = isFloat ? sizeof(float) : sizeof(qint16);
        int frames = raw.size() / (bps * ch);
        if (frames <= 0) return;

        m_inScratch.resize(frames);
        if (isFloat) {
            const float* inF = reinterpret_cast<const float*>(raw.constData());
            for (int i = 0; i < frames; ++i) m_inScratch[i] = inF[i * ch];
        } else {
            const qint16* inI = reinterpret_cast<const qint16*>(raw.constData());
            for (int i = 0; i < frames; ++i) m_inScratch[i] = static_cast<float>(inI[i * ch]) * (1.0f / 32768.0f);
        }
        g_sdkEngine.pushAudioInSamples(m_inScratch.data(), frames);
    }

    QAudioFormat m_format;
    QAudioFormat m_inFormat;
    QAudioSink* m_sink = nullptr;
    QAudioSource* m_source = nullptr;
    QIODevice* m_inDevice = nullptr;
    std::vector<float> m_monoScratch;
    std::vector<float> m_inScratch;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    StudioAudioBridge audioBridge;
    SynthSDK::SynthStudioWindow studioWin(&g_sdkEngine);

    MidiReader midiReader([](int note, int vel, bool on) {
        if (on) g_sdkEngine.noteOn(note, vel);
        else    g_sdkEngine.noteOff(note);
    }, &studioWin);

    midiReader.setCcCallback([](int cc, int val) {
        g_sdkEngine.handleMidiCC(cc, val);
    });
    midiReader.setPitchBendCallback([](float bendNorm) {
        g_sdkEngine.handlePitchBend(bendNorm);
    });
    midiReader.start();

    studioWin.show();
    int ret = app.exec();

    midiReader.stopReader();
    midiReader.wait();
    return ret;
}